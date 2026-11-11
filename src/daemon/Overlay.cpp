// SPDX-License-Identifier: MIT
#include "Overlay.h"
#include "Engine.h"
#include "IconProvider.h"
#ifdef OPENDIAL_HAVE_WAYLAND
#include "WaylandClient.h"
#endif

#include <QCursor>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPalette>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QStyleHints>

#ifdef OPENDIAL_HAVE_LAYERSHELL
#include <LayerShellQt/window.h>
#endif

static void opendialInitOverlay() { Q_INIT_RESOURCE(overlay); }

Q_LOGGING_CATEGORY(lcOverlay, "opendial.overlay")

namespace opendial {

Overlay::Overlay(Engine *engine, QObject *parent) : QObject(parent), m_engine(engine)
{
    opendialInitOverlay();
    initIconResources();
#if defined(OPENDIAL_HAVE_LAYERSHELL) && defined(OPENDIAL_HAVE_WAYLAND)
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        m_layerShell = WaylandClient::instance()->hasGlobal(QStringLiteral("zwlr_layer_shell_v1"));
#endif
    m_detail = m_layerShell ? QStringLiteral("layer-shell overlay") : QStringLiteral("frameless always-on-top window");
    qCInfo(lcOverlay) << "overlay:" << m_detail;
    m_osdTimer.setSingleShot(true);
    connect(&m_osdTimer, &QTimer::timeout, this, [this] {
        if (m_osd)
            m_osd->hide();
    });
    connect(engine, &Engine::menuChanged, this, [this](const QJsonObject &m) {
        if (m.value(QStringLiteral("open")).toBool())
            showMenu(m);
        else
            hideMenu();
    });
    connect(engine, &Engine::osdRequested, this, &Overlay::showOsd);
}

Overlay::~Overlay()
{
    delete m_menu;
    delete m_osd;
}

bool Overlay::systemDark() const
{
    const QString theme = m_engine->settings().value(QStringLiteral("menu")).toObject().value(QStringLiteral("theme")).toString(QStringLiteral("system"));
    if (theme == QLatin1String("dark"))
        return true;
    if (theme == QLatin1String("light"))
        return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown)
        return scheme == Qt::ColorScheme::Dark;
#endif
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

QQuickView *Overlay::createView(const QString &qml, const QString &scope)
{
    auto *v = new QQuickView;
    v->engine()->addImageProvider(QStringLiteral("opendial"), new IconProvider);
    v->setColor(Qt::transparent);
    v->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput
                | (m_layerShell ? Qt::Window : Qt::ToolTip));
    v->setResizeMode(QQuickView::SizeViewToRootObject);
    v->setTitle(QStringLiteral("OpenDial"));
    v->setSource(QUrl(QStringLiteral("qrc:/opendial/overlay/") + qml));
    if (v->status() == QQuickView::Error)
        for (const auto &e : v->errors())
            qCWarning(lcOverlay) << e.toString();
#ifdef OPENDIAL_HAVE_LAYERSHELL
    if (m_layerShell) {
        auto *lw = LayerShellQt::Window::get(v);
        lw->setLayer(LayerShellQt::Window::LayerOverlay);
        lw->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        lw->setExclusiveZone(-1);
        lw->setScope(scope);
        lw->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft));
        lw->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
        lw->setActivateOnShow(false);
    }
#else
    Q_UNUSED(scope);
#endif
    return v;
}

void Overlay::applyTheme(QQuickView *v)
{
    const QJsonObject ms = m_engine->settings().value(QStringLiteral("menu")).toObject();
    if (auto *r = v->rootObject()) {
        r->setProperty("dark", systemDark());
        r->setProperty("accent", QColor(ms.value(QStringLiteral("accent")).toString(QStringLiteral("#4c8dff"))));
        r->setProperty("panelOpacity", qBound(0.3, ms.value(QStringLiteral("opacity")).toDouble(0.94), 1.0));
    }
}

void Overlay::place(QQuickView *v, QScreen *screen, QPoint topLeft, bool osd, const QString &position)
{
    const QRect g = screen->geometry();
    const QSize s = v->size();
    if (osd) {
        const int margin = 64;
        const int x = g.x() + (g.width() - s.width()) / 2;
        const int y = position == QLatin1String("top") ? g.y() + margin
                      : position == QLatin1String("center") ? g.y() + (g.height() - s.height()) / 2
                                                             : g.y() + g.height() - s.height() - margin;
        topLeft = QPoint(x, y);
    }
    topLeft.setX(qBound(g.left(), topLeft.x(), g.right() - s.width()));
    topLeft.setY(qBound(g.top(), topLeft.y(), g.bottom() - s.height()));
    if (v->screen() != screen) {
        // a layer surface is bound to its output: re-create it on the new one
        if (v->isVisible())
            v->hide();
        v->setScreen(screen);
    }
#ifdef OPENDIAL_HAVE_LAYERSHELL
    if (m_layerShell) {
        LayerShellQt::Window::get(v)->setMargins(QMargins(topLeft.x() - g.x(), topLeft.y() - g.y(), 0, 0));
        return;
    }
#endif
    v->setPosition(topLeft);
}

void Overlay::showMenu(const QJsonObject &menu)
{
    if (!m_menu)
        m_menu = createView(QStringLiteral("RadialMenu.qml"), QStringLiteral("opendial-menu"));
    auto *root = m_menu->rootObject();
    if (!root)
        return;
    const QJsonObject ms = menu.value(QStringLiteral("settings")).toObject();
    const int size = qBound(200, ms.value(QStringLiteral("size")).toInt(360), 800);
    applyTheme(m_menu);
    root->setProperty("menuSize", size);
    root->setProperty("reduceMotion", ms.value(QStringLiteral("reduceMotion")).toBool(false));
    root->setProperty("showHints", ms.value(QStringLiteral("showHints")).toBool(true));
    root->setProperty("menu", menu.toVariantMap());
    m_menu->resize(size, size);
    if (m_menu->isVisible())
        return; // stays where it opened

    // where: pointer (default), centre of the screen, or a saved position
    const QString placement = ms.value(QStringLiteral("placement")).toString(QStringLiteral("pointer"));
    QScreen *screen = QGuiApplication::primaryScreen();
    QPoint center;
    if (placement == QLatin1String("pointer") && menu.contains(QStringLiteral("pointer"))) {
        const QJsonObject p = menu.value(QStringLiteral("pointer")).toObject();
        center = QPoint(p.value(QStringLiteral("x")).toInt(), p.value(QStringLiteral("y")).toInt());
        if (auto *s = QGuiApplication::screenAt(center))
            screen = s;
        // handedness: "beside" keeps the area under the pointer visible
        if (ms.value(QStringLiteral("pointerOffset")).toString() == QLatin1String("beside")) {
            const bool left = menu.value(QStringLiteral("handedness")).toString() == QLatin1String("left");
            center.rx() += (left ? 1 : -1) * (size / 2 + 32);
        }
    } else if (placement == QLatin1String("saved")) {
        const QJsonObject sp = ms.value(QStringLiteral("savedPosition")).toObject();
        const QString display = ms.value(QStringLiteral("display")).toString();
        for (auto *s : QGuiApplication::screens())
            if (s->name() == display)
                screen = s;
        const QRect g = screen->geometry();
        center = QPoint(g.x() + int(g.width() * qBound(0.0, sp.value(QStringLiteral("x")).toDouble(0.5), 1.0)),
                        g.y() + int(g.height() * qBound(0.0, sp.value(QStringLiteral("y")).toDouble(0.5), 1.0)));
    } else {
        const QString display = ms.value(QStringLiteral("display")).toString();
        for (auto *s : QGuiApplication::screens())
            if (s->name() == display)
                screen = s;
        center = screen->geometry().center();
    }
    if (!screen)
        return;
    qCDebug(lcOverlay) << "menu at" << center << "on" << screen->name() << screen->geometry() << "placement" << placement;
    place(m_menu, screen, center - QPoint(size / 2, size / 2), false, {});
    m_menu->show();
}

void Overlay::hideMenu()
{
    if (m_menu)
        m_menu->hide();
}

void Overlay::showOsd(const QJsonObject &osd)
{
    if (!m_osd)
        m_osd = createView(QStringLiteral("Osd.qml"), QStringLiteral("opendial-osd"));
    auto *root = m_osd->rootObject();
    if (!root)
        return;
    applyTheme(m_osd);
    root->setProperty("osd", osd.toVariantMap());
    if (!m_osd->isVisible()) {
        // the screen is chosen once per appearance (pointer lookup is an IPC round trip)
        QScreen *screen = QGuiApplication::primaryScreen();
        if (auto p = m_engine->context() ? m_engine->menuJson().value(QStringLiteral("pointer")).toObject() : QJsonObject(); !p.isEmpty())
            if (auto *s = QGuiApplication::screenAt(QPoint(p.value(QStringLiteral("x")).toInt(), p.value(QStringLiteral("y")).toInt())))
                screen = s;
        if (!screen)
            return;
        place(m_osd, screen, {}, true, osd.value(QStringLiteral("position")).toString(QStringLiteral("bottom")));
    }
    m_osd->show();
    m_osdTimer.start(qBound(300, osd.value(QStringLiteral("durationMs")).toInt(1200), 10000));
}

} // namespace opendial
