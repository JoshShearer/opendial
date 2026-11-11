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
#include <QTimer>

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
        QCoreApplication::exit(2);
        return;
    }
    QString error;
    const auto lines = parseFixture(f.readAll(), &error);
    if (!error.isEmpty()) {
        qCritical("fixture error: %s", qPrintable(error));
        QCoreApplication::exit(2);
        return;
    }
    auto decoder = std::make_shared<DialDecoder>();
    qint64 t0 = -1, last = 0;
    for (const auto &l : lines) {
        const qint64 t = l.event.timeUs;
        if (l.kind == FixtureLine::Event && t0 < 0)
            t0 = t;
        const qint64 at = l.kind == FixtureLine::Event ? qint64((t - t0) / 1000.0 / speed) : last;
        last = at;
        QTimer::singleShot(int(qMax<qint64>(0, at)), engine, [engine, decoder, l, guardMs] {
            if (l.kind == FixtureLine::Connect)
                decoder->onConnected(l.event.timeUs, guardMs);
            else if (l.kind == FixtureLine::Disconnect)
                decoder->reset();
            else
                for (const auto &s : decoder->feed(l.event))
                    engine->onDialSignal(s);
        });
    }
    if (quitAfter)
        QTimer::singleShot(int(last) + 1500, qApp, [] { QCoreApplication::quit(); });
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
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("opendiald"));
    QApplication::setOrganizationDomain(QStringLiteral("opendial.github.io"));
    QApplication::setApplicationVersion(QStringLiteral(OPENDIAL_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("io.github.opendial.OpenDial"));

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
        qCritical("opendiald: %s", qPrintable(err));
        return err.contains(QLatin1String("another")) ? 0 : 1;
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
