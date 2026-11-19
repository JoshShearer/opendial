// SPDX-License-Identifier: MIT
// opendiald: the OpenDial user service (systemd --user unit opendial.service).
#include "DBusService.h"
#include "DialDecoder.h"
#include "DialDevice.h"
#include "Engine.h"
#include "HttpApi.h"
#include "Overlay.h"
#include "Tray.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QFile>
#include <QLoggingCategory>
#include <QSocketNotifier>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <cstdio>
#include <functional>
#include <vector>

#include <csignal>
#include <pthread.h>
#include <sys/signalfd.h>
#include <unistd.h>

using namespace opendial;

namespace {

// Feed a recorded fixture (JSON lines or evtest text) through the real
// decoder and engine, preserving the recorded timing (scaled by speed).
void replay(Engine *engine, const QString &path, double speed, bool quitAfter, int guardMs)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qCritical("cannot open fixture %s", qPrintable(path));
        // the event loop is not running yet: queue the exit
        QMetaObject::invokeMethod(qApp, [] { QCoreApplication::exit(2); }, Qt::QueuedConnection);
        return;
    }
    QString error;
    const auto lines = parseFixture(f.readAll(), &error);
    if (!error.isEmpty()) {
        qCritical("fixture error: %s", qPrintable(error));
        // the event loop is not running yet: queue the exit
        QMetaObject::invokeMethod(qApp, [] { QCoreApplication::exit(2); }, Qt::QueuedConnection);
        return;
    }
    // Events are chained: each one is scheduled after the previous one ran,
    // so recorded gaps (e.g. a long-press hold) survive a busy event loop.
    struct Step {
        FixtureLine line;
        int delayMs; // after the previous step
    };
    auto steps = std::make_shared<std::vector<Step>>();
    qint64 prevUs = -1;
    for (const auto &l : lines) {
        int delay = 0;
        if (l.kind == FixtureLine::Event) {
            if (prevUs >= 0)
                delay = int(qBound<qint64>(0, qint64((l.event.timeUs - prevUs) / 1000.0 / speed), qint64(60000)));
            prevUs = l.event.timeUs;
        }
        steps->push_back({l, delay});
    }
    auto decoder = std::make_shared<DialDecoder>();
    // Pending timers hold the strong reference; the function itself only a
    // weak one, so the chain frees itself after the last step.
    auto next = std::make_shared<std::function<void(size_t)>>();
    std::weak_ptr<std::function<void(size_t)>> weak = next;
    *next = [engine, decoder, steps, weak, quitAfter, guardMs](size_t i) {
        if (i >= steps->size()) {
            if (quitAfter)
                QTimer::singleShot(1500, qApp, [engine] {
                    // The event log on stdout (JSON lines) makes replays assertable in tests.
                    for (const auto &e : engine->diagnostics(false).value(QStringLiteral("eventLog")).toArray())
                        std::fputs((QJsonDocument(e.toObject()).toJson(QJsonDocument::Compact) + '\n').constData(), stdout);
                    std::fflush(stdout);
                    QCoreApplication::quit();
                });
            return;
        }
        const auto &l = steps->at(i).line;
        if (l.kind == FixtureLine::Connect)
            decoder->onConnected(l.event.timeUs, guardMs);
        else if (l.kind == FixtureLine::Disconnect)
            decoder->reset();
        else
            for (const auto &s : decoder->feed(l.event))
                engine->onDialSignal(s);
        const int delay = i + 1 < steps->size() ? steps->at(i + 1).delayMs : 0;
        if (auto self = weak.lock())
            QTimer::singleShot(delay, engine, [self, i] { (*self)(i + 1); });
    };
    // start once the event loop runs and startup work is done
    QTimer::singleShot(300, engine, [next] { (*next)(0); });
}

sigset_t terminationSignals()
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGHUP);
    return mask;
}

void installSignalHandlers(QObject *ctx)
{
    sigset_t mask = terminationSignals();
    const int fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (fd < 0)
        return;
    auto *n = new QSocketNotifier(fd, QSocketNotifier::Read, ctx);
    QObject::connect(n, &QSocketNotifier::activated, ctx, [fd] {
        signalfd_siginfo si;
        while (::read(fd, &si, sizeof si) == sizeof si) {
            // graceful: Engine/InjectorManager destructors release held keys
            QCoreApplication::quit();
        }
    });
}

} // namespace

int main(int argc, char **argv)
{
    // Block termination signals before any thread exists so every thread
    // inherits the mask and they are only consumed through the signalfd.
    {
        sigset_t mask = terminationSignals();
        pthread_sigmask(SIG_BLOCK, &mask, nullptr);
    }
    // Never let the overlay steal focus or appear in task switchers.
    qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
    // --help/--version and display-less starts (service started before the
    // session exported WAYLAND_DISPLAY/DISPLAY, ssh, containers) must not abort
    // in the platform plugin: run headless, the device and D-Bus still work.
    bool infoOnly = false;
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        infoOnly |= a == "-h" || a == "--help" || a == "--help-all" || a == "-v" || a == "--version";
    }
    const bool noDisplay = qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("DISPLAY");
    if (infoOnly || (noDisplay && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")))
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qunsetenv("QT_QPA_PLATFORMTHEME"); // e.g. gtk3 exits without a display
    }
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("opendiald"));
    QApplication::setOrganizationDomain(QStringLiteral("joshshearer.github.io"));
    QApplication::setApplicationVersion(QStringLiteral(OPENDIAL_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("io.github.joshshearer.opendial.OpenDial"));

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("OpenDial Surface Dial service"));
    p.addHelpOption();
    p.addVersionOption();
    QCommandLineOption noDevice(QStringLiteral("no-device"), QStringLiteral("Do not open the Surface Dial (simulation only)."));
    QCommandLineOption noContext(QStringLiteral("no-context"), QStringLiteral("Do not track the focused window."));
    QCommandLineOption dryRun(QStringLiteral("dry-run"), QStringLiteral("Log actions instead of injecting input."));
    QCommandLineOption noOverlay(QStringLiteral("no-overlay"), QStringLiteral("Do not show the radial menu or OSD."));
    QCommandLineOption noTray(QStringLiteral("no-tray"), QStringLiteral("Do not show a tray icon."));
    QCommandLineOption config(QStringLiteral("config"), QStringLiteral("Configuration file."), QStringLiteral("path"));
    QCommandLineOption state(QStringLiteral("state"), QStringLiteral("State file."), QStringLiteral("path"));
    QCommandLineOption replayOpt(QStringLiteral("replay"), QStringLiteral("Replay a recorded event fixture."), QStringLiteral("file"));
    QCommandLineOption speed(QStringLiteral("replay-speed"), QStringLiteral("Replay speed factor."), QStringLiteral("x"), QStringLiteral("1"));
    QCommandLineOption exitAfter(QStringLiteral("exit-after-replay"), QStringLiteral("Quit when the replay finished."));
    QCommandLineOption verbose(QStringLiteral("verbose"), QStringLiteral("Debug logging."));
    p.addOptions({noDevice, noContext, dryRun, noOverlay, noTray, config, state, replayOpt, speed, exitAfter, verbose});
    p.process(app);

    if (p.isSet(verbose))
        QLoggingCategory::setFilterRules(QStringLiteral("opendial.*.debug=true"));

    EngineOptions opts;
    opts.noDevice = p.isSet(noDevice) || p.isSet(replayOpt);
    opts.noContext = p.isSet(noContext);
    opts.noInjection = p.isSet(dryRun);
    opts.configPath = p.value(config);
    opts.statePath = p.value(state);

    Engine engine(opts);
    DBusService dbus(&engine);
    QString err;
    if (!dbus.registerOnBus(&err)) {
        // Replays are offline (tests, bug reports): no bus needed.
        if (!p.isSet(replayOpt)) {
            qCritical("opendiald: %s", qPrintable(err));
            return err.contains(QLatin1String("another")) ? 0 : 1;
        }
        qWarning("opendiald: replaying without D-Bus (%s)", qPrintable(err));
    }
    installSignalHandlers(&app);
    engine.start();

    std::unique_ptr<Overlay> overlay;
    if (!p.isSet(noOverlay) && QGuiApplication::platformName() != QLatin1String("offscreen"))
        overlay = std::make_unique<Overlay>(&engine);
    std::unique_ptr<Tray> tray;
    auto updateTray = [&] {
        const bool want = !p.isSet(noTray) && engine.settings().value(QStringLiteral("ui")).toObject().value(QStringLiteral("tray")).toBool(true);
        if (want && !tray)
            tray = std::make_unique<Tray>(&engine);
        else if (!want)
            tray.reset();
    };
    // Tray hosts (waybar, KDE, GNOME AppIndicator) may start after us.
    QTimer::singleShot(0, &app, updateTray);
    QTimer::singleShot(5000, &app, [&] {
        if (!tray || !tray->available()) {
            tray.reset();
            updateTray();
        }
    });

    HttpApi http(&engine);
    auto updateHttp = [&] {
        const QJsonObject h = engine.settings().value(QStringLiteral("integrations")).toObject().value(QStringLiteral("httpApi")).toObject();
        if (h.value(QStringLiteral("enabled")).toBool(false)) {
            QString e;
            if (!http.start(quint16(h.value(QStringLiteral("port")).toInt(47311)), &e))
                qWarning("http api: %s", qPrintable(e));
        } else if (http.isRunning()) {
            http.stop();
        }
    };
    updateHttp();
    QObject::connect(&engine, &Engine::configChanged, &app, [&] {
        updateHttp();
        updateTray();
    });

    if (p.isSet(replayOpt))
        replay(&engine, p.value(replayOpt), qBound(0.1, p.value(speed).toDouble(), 100.0), p.isSet(exitAfter),
               engine.settings().value(QStringLiteral("device")).toObject().value(QStringLiteral("connectGuardMs")).toInt(800));

    const int rc = app.exec();
    http.stop();
    return rc;
}
