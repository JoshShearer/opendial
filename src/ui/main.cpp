// SPDX-License-Identifier: MIT
// OpenDial settings app.
#include "Backend.h"
#include "IconProvider.h"
#include "SystemTheme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>

static void initUiResources() { Q_INIT_RESOURCE(ui); }

namespace opendial {
// io.github.joshshearer.opendial.Settings1 on "/": lets a second launch raise this window.
class SettingsRaiser : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.joshshearer.opendial.Settings1")
public:
    SettingsRaiser(QQuickWindow *w, QObject *parent) : QObject(parent), m_win(w) {}
public Q_SLOTS:
    void Raise(const QString &page)
    {
        if (!m_win)
            return;
        if (!page.isEmpty())
            QMetaObject::invokeMethod(m_win, "go", Q_ARG(QVariant, page));
        m_win->show();
        m_win->raise();
        m_win->requestActivate();
    }

private:
    QPointer<QQuickWindow> m_win;
};
} // namespace opendial

int main(int argc, char **argv)
{
    QApplication app(argc, argv); // widgets: native file dialogs fall back cleanly
    QApplication::setApplicationName(QStringLiteral("opendial"));
    QApplication::setApplicationDisplayName(QStringLiteral("OpenDial"));
    QApplication::setOrganizationName(QStringLiteral("opendial"));
    QApplication::setApplicationVersion(QStringLiteral(OPENDIAL_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.joshshearer.opendial.OpenDial"));

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("Configure the Surface Dial"));
    p.addHelpOption();
    p.addVersionOption();
    QCommandLineOption page(QStringLiteral("page"), QStringLiteral("Open on a page (dashboard, menu, profiles, rotation, device, integrations, diagnostics, settings)."),
                            QStringLiteral("page"));
    p.addOption(page);
    p.process(app);

    // Single window: if the settings app already runs, raise it instead.
    auto bus = QDBusConnection::sessionBus();
    const QString svc = QStringLiteral("io.github.joshshearer.opendial.Settings");
    if (bus.isConnected() && !bus.registerService(svc)) {
        QDBusInterface other(svc, QStringLiteral("/"), QStringLiteral("io.github.joshshearer.opendial.Settings1"), bus);
        other.call(QStringLiteral("Raise"), p.value(page));
        return 0;
    }

    opendial::initIconResources();
    initUiResources();
    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("io.github.joshshearer.opendial.OpenDial"), QIcon(QStringLiteral(":/opendial/icons/app.svg"))));
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("Material"));

    opendial::Backend backend;
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("opendial"), new opendial::IconProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    opendial::SystemTheme systemTheme;
    engine.rootContext()->setContextProperty(QStringLiteral("systemTheme"), &systemTheme);
    engine.setInitialProperties({{QStringLiteral("initialPage"), p.value(page)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/opendial/ui/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    // Raise requests from a second launch (also used by the tray's "Settings…").
    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (bus.isConnected()) {
        auto *raiser = new opendial::SettingsRaiser(win, &app);
        bus.registerObject(QStringLiteral("/"), raiser, QDBusConnection::ExportAllSlots);
    }
    // Test hook: render offscreen, save a screenshot and quit (docs + CI smoke tests).
    if (const QString shot = qEnvironmentVariable("OPENDIAL_SCREENSHOT"); !shot.isEmpty() && win) {
        if (const QByteArray size = qgetenv("OPENDIAL_SCREENSHOT_SIZE"); !size.isEmpty()) {
            const auto wh = size.split('x');
            if (wh.size() == 2)
                win->resize(wh[0].toInt(), wh[1].toInt());
        }
        QTimer::singleShot(qEnvironmentVariableIntValue("OPENDIAL_SCREENSHOT_DELAY_MS") > 0 ? qEnvironmentVariableIntValue("OPENDIAL_SCREENSHOT_DELAY_MS") : 2500, win, [win, shot] {
            const bool ok = win->grabWindow().save(shot);
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}

#include "main.moc"
