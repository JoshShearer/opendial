// SPDX-License-Identifier: MIT
#include "FocusTrackers.h"
#include "Config.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTimer>
#include <QtEndian>

Q_LOGGING_CATEGORY(lcContext, "opendial.context")

namespace opendial {

// ================================================================ Hyprland

HyprlandTracker::HyprlandTracker(QObject *parent) : FocusTracker(parent) {}

bool HyprlandTracker::detected() { return !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"); }

QString HyprlandTracker::socketDir()
{
    const QString sig = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    const QString a = qEnvironmentVariable("XDG_RUNTIME_DIR") + QStringLiteral("/hypr/") + sig;
    if (QFileInfo::exists(a + QStringLiteral("/.socket.sock")))
        return a;
    return QStringLiteral("/tmp/hypr/") + sig;
}

QByteArray HyprlandTracker::request(const QByteArray &cmd, int timeoutMs)
{
    QLocalSocket s;
    s.connectToServer(socketDir() + QStringLiteral("/.socket.sock"));
    if (!s.waitForConnected(timeoutMs))
        return {};
    s.write(cmd);
    s.flush();
    QByteArray out;
    while (s.state() == QLocalSocket::ConnectedState && s.waitForReadyRead(timeoutMs))
        out += s.readAll();
    out += s.readAll();
    return out;
}

bool HyprlandTracker::dispatch(const QByteArray &legacy, const QByteArray &lua)
{
    static bool preferLua = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const bool useLua = preferLua != (attempt == 1);
        const QByteArray reply = request("dispatch " + (useLua ? lua : legacy)).trimmed();
        if (reply == "ok") {
            preferLua = useLua;
            return true;
        }
        qCInfo(lcContext) << "hyprland dispatch" << (useLua ? "lua" : "legacy") << "failed:" << reply.left(200);
    }
    return false;
}

void HyprlandTracker::start()
{
    connect(&m_events, &QLocalSocket::readyRead, this, &HyprlandTracker::onEvents);
    connect(&m_events, &QLocalSocket::disconnected, this, [this] {
        m_ok = false;
        m_detail = QStringLiteral("event socket closed; retrying");
        Q_EMIT stateChanged();
        QTimer::singleShot(2000, this, [this] { m_events.connectToServer(socketDir() + QStringLiteral("/.socket2.sock")); });
    });
    connect(&m_events, &QLocalSocket::connected, this, [this] {
        m_ok = true;
        m_detail = QStringLiteral("connected to %1").arg(socketDir());
        Q_EMIT stateChanged();
        refresh();
    });
    m_events.connectToServer(socketDir() + QStringLiteral("/.socket2.sock"));
}

void HyprlandTracker::onEvents()
{
    m_buf += m_events.readAll();
    int nl;
    bool need = false;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(nl);
        m_buf.remove(0, nl + 1);
        if (line.startsWith("activewindowv2>>") || line.startsWith("activewindow>>") || line.startsWith("workspace>>")
            || line.startsWith("workspacev2>>") || line.startsWith("focusedmon>>") || line.startsWith("windowtitlev2>>")
            || line.startsWith("closewindow>>"))
            need = true;
    }
    if (m_buf.size() > 65536)
        m_buf.clear();
    if (need && !m_refreshQueued) {
        m_refreshQueued = true;
        // coalesce bursts (title updates while typing, etc.)
        QTimer::singleShot(30, this, [this] {
            m_refreshQueued = false;
            refresh();
        });
    }
}

void HyprlandTracker::refresh()
{
    const QByteArray j = request("j/activewindow");
    const QJsonObject w = QJsonDocument::fromJson(j).object();
    FocusContext c;
    c.source = id();
    c.appId = w.value(QLatin1String("class")).toString();
    c.windowClass = w.value(QLatin1String("initialClass")).toString();
    c.title = w.value(QLatin1String("title")).toString();
    c.pid = w.value(QLatin1String("pid")).toInteger();
    c.workspace = w.value(QLatin1String("workspace")).toObject().value(QLatin1String("name")).toString();
    if (c.workspace.isEmpty()) {
        const auto ws = QJsonDocument::fromJson(request("j/activeworkspace")).object();
        c.workspace = ws.value(QLatin1String("name")).toString();
    }
    Q_EMIT focusChanged(c);
}

std::optional<QPoint> HyprlandTracker::pointerPosition()
{
    const auto o = QJsonDocument::fromJson(request("j/cursorpos")).object();
    if (!o.contains(QLatin1String("x")))
        return std::nullopt;
    return QPoint(o.value(QLatin1String("x")).toInt(), o.value(QLatin1String("y")).toInt());
}

// ==================================================================== Sway

SwayTracker::SwayTracker(QObject *parent) : FocusTracker(parent) {}

bool SwayTracker::detected() { return !qEnvironmentVariableIsEmpty("SWAYSOCK") || !qEnvironmentVariableIsEmpty("I3SOCK"); }

QByteArray SwayTracker::frame(quint32 type, const QByteArray &payload)
{
    QByteArray b("i3-ipc");
    quint32 len = quint32(payload.size());
    b.append(reinterpret_cast<const char *>(&len), 4); // native endian per i3 protocol
    b.append(reinterpret_cast<const char *>(&type), 4);
    b.append(payload);
    return b;
}

bool SwayTracker::command(const QByteArray &cmd)
{
    QLocalSocket s;
    const QString p = qEnvironmentVariableIsEmpty("SWAYSOCK") ? qEnvironmentVariable("I3SOCK") : qEnvironmentVariable("SWAYSOCK");
    s.connectToServer(p);
    if (!s.waitForConnected(300))
        return false;
    s.write(frame(0, cmd));
    s.flush();
    s.waitForReadyRead(300);
    return true;
}

void SwayTracker::start()
{
    const QString p = qEnvironmentVariableIsEmpty("SWAYSOCK") ? qEnvironmentVariable("I3SOCK") : qEnvironmentVariable("SWAYSOCK");
    connect(&m_sock, &QLocalSocket::connected, this, [this] {
        m_ok = true;
        m_detail = QStringLiteral("subscribed to window events");
        m_sock.write(frame(2, R"(["window","workspace"])")); // SUBSCRIBE
        m_sock.write(frame(4, {}));                           // GET_TREE (initial focus)
        Q_EMIT stateChanged();
    });
    connect(&m_sock, &QLocalSocket::readyRead, this, &SwayTracker::onReadable);
    m_sock.connectToServer(p);
}

void SwayTracker::handleNode(const QJsonObject &n)
{
    if (n.value(QLatin1String("focused")).toBool() && n.value(QLatin1String("type")).toString().contains(QLatin1String("con"))) {
        FocusContext c;
        c.source = id();
        c.appId = n.value(QLatin1String("app_id")).toString();
        c.windowClass = n.value(QLatin1String("window_properties")).toObject().value(QLatin1String("class")).toString();
        if (c.appId.isEmpty())
            c.appId = c.windowClass;
        c.title = n.value(QLatin1String("name")).toString();
        c.pid = n.value(QLatin1String("pid")).toInteger();
        Q_EMIT focusChanged(c);
        return;
    }
    for (const auto &k : n.value(QLatin1String("nodes")).toArray())
        handleNode(k.toObject());
    for (const auto &k : n.value(QLatin1String("floating_nodes")).toArray())
        handleNode(k.toObject());
}

void SwayTracker::onReadable()
{
    m_buf += m_sock.readAll();
    while (m_buf.size() >= 14) {
        if (!m_buf.startsWith("i3-ipc")) {
            m_buf.clear();
            return;
        }
        quint32 len, type;
        memcpy(&len, m_buf.constData() + 6, 4);
        memcpy(&type, m_buf.constData() + 10, 4);
        if (len > 16 * 1024 * 1024) {
            m_buf.clear();
            return;
        }
        if (quint32(m_buf.size()) < 14 + len)
            return;
        const QJsonDocument d = QJsonDocument::fromJson(m_buf.mid(14, int(len)));
        m_buf.remove(0, 14 + int(len));
        if (type == 4) {
            handleNode(d.object());
        } else if (type == 0x80000003u) { // window event
            const auto o = d.object();
            if (o.value(QLatin1String("change")).toString() == QLatin1String("focus")
                || o.value(QLatin1String("change")).toString() == QLatin1String("title"))
                handleNode(o.value(QLatin1String("container")).toObject());
        }
    }
}

// ================================================================== Bridge

BridgeTracker::BridgeTracker(Kind kind, QObject *parent) : FocusTracker(parent), m_kind(kind) {}

BridgeTracker::~BridgeTracker()
{
    if (m_kind == Kind::KWin && m_installed) {
        QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                                                        QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("unloadScript"));
        m << QStringLiteral("opendial-focus");
        QDBusConnection::sessionBus().call(m, QDBus::NoBlock);
    }
}

bool BridgeTracker::kwinDetected()
{
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("KDE"), Qt::CaseInsensitive)
           && QDBusConnection::sessionBus().interface()
           && QDBusConnection::sessionBus().interface()->isServiceRegistered(QStringLiteral("org.kde.KWin"));
}

bool BridgeTracker::gnomeDetected()
{
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("GNOME"), Qt::CaseInsensitive);
}

QString BridgeTracker::id() const
{
    switch (m_kind) {
    case Kind::KWin: return QStringLiteral("kwin-script");
    case Kind::Gnome: return QStringLiteral("gnome-shell-extension");
    default: return QStringLiteral("dbus-bridge");
    }
}

QString BridgeTracker::label() const
{
    switch (m_kind) {
    case Kind::KWin: return QStringLiteral("KWin script bridge");
    case Kind::Gnome: return QStringLiteral("GNOME Shell extension bridge");
    default: return QStringLiteral("External ReportFocus bridge");
    }
}

CapState BridgeTracker::state() const
{
    if (m_active)
        return CapState::Supported;
    if (m_kind == Kind::Gnome)
        return m_installed ? CapState::PermissionNeeded : CapState::FallbackAvailable;
    return m_installed ? CapState::FallbackAvailable : CapState::NotSupported;
}

void BridgeTracker::start()
{
    if (m_kind == Kind::KWin)
        loadKWinScript();
    else if (m_kind == Kind::Gnome)
        checkGnomeExtension();
}

void BridgeTracker::loadKWinScript()
{
    QDir().mkpath(runtimeDir());
    const QString path = runtimeDir() + QStringLiteral("/kwin-focus.js");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_detail = QStringLiteral("cannot write KWin script");
        return;
    }
    f.write(R"JS(
// OpenDial focus bridge: reports the active window to the OpenDial service.
function opendialReport(w) {
    var p = { source: "kwin" };
    if (w) {
        p.appId = w.desktopFileName || w.resourceClass || "";
        p.windowClass = w.resourceClass || "";
        p.title = w.caption || "";
        p.pid = w.pid || 0;
        try { p.workspace = String(workspace.currentDesktop.name || workspace.currentDesktop); } catch (e) {}
    }
    try { p.pointer = { x: workspace.cursorPos.x, y: workspace.cursorPos.y }; } catch (e) {}
    callDBus("io.github.opendial", "/io/github/opendial/Daemon", "io.github.opendial.Daemon1",
             "ReportFocus", JSON.stringify(p));
}
if (workspace.windowActivated) {
    workspace.windowActivated.connect(opendialReport);
    opendialReport(workspace.activeWindow);
} else {
    workspace.clientActivated.connect(opendialReport);
    opendialReport(workspace.activeClient);
}
)JS");
    f.close();
    auto bus = QDBusConnection::sessionBus();
    QDBusMessage unload = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                                                         QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("unloadScript"));
    unload << QStringLiteral("opendial-focus");
    bus.call(unload, QDBus::Block, 1000);
    QDBusMessage load = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                                                       QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("loadScript"));
    load << path << QStringLiteral("opendial-focus");
    QDBusReply<int> id = bus.call(load, QDBus::Block, 2000);
    if (!id.isValid()) {
        m_detail = QStringLiteral("KWin refused the script: %1").arg(id.error().message());
        return;
    }
    // KWin 6: /Scripting/Script<N>, KWin 5: /<N>
    for (const QString &p : {QStringLiteral("/Scripting/Script%1").arg(id.value()), QStringLiteral("/%1").arg(id.value())}) {
        QDBusMessage run = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), p, QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run"));
        if (bus.call(run, QDBus::Block, 1000).type() != QDBusMessage::ErrorMessage) {
            m_installed = true;
            break;
        }
    }
    m_detail = m_installed ? QStringLiteral("KWin script loaded; waiting for first report") : QStringLiteral("could not run the KWin script");
    Q_EMIT stateChanged();
}

void BridgeTracker::checkGnomeExtension()
{
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.gnome.Shell"), QStringLiteral("/org/gnome/Shell"),
                                                    QStringLiteral("org.gnome.Shell.Extensions"), QStringLiteral("GetExtensionInfo"));
    m << QStringLiteral("opendial@opendial.github.io");
    QDBusMessage r = QDBusConnection::sessionBus().call(m, QDBus::Block, 1500);
    QVariantMap info;
    if (r.type() == QDBusMessage::ReplyMessage && !r.arguments().isEmpty())
        info = qdbus_cast<QVariantMap>(r.arguments().first());
    m_installed = !info.isEmpty();
    const int st = info.value(QStringLiteral("state")).toInt();
    m_detail = !m_installed ? QStringLiteral("install & enable the OpenDial GNOME Shell extension for per-app profiles")
               : st == 1   ? QStringLiteral("extension enabled; waiting for first report")
                           : QStringLiteral("extension installed but not enabled (gnome-extensions enable opendial@opendial.github.io)");
    Q_EMIT stateChanged();
}

void BridgeTracker::report(const QJsonObject &p)
{
    if (!m_active) {
        m_active = true;
        m_detail = QStringLiteral("receiving focus reports");
        Q_EMIT stateChanged();
    }
    if (p.value(QLatin1String("pointer")).isObject()) {
        const auto pt = p.value(QLatin1String("pointer")).toObject();
        m_lastPointer = QPoint(pt.value(QLatin1String("x")).toInt(), pt.value(QLatin1String("y")).toInt());
    }
    FocusContext c = FocusContext::fromJson(p);
    c.source = id();
    Q_EMIT focusChanged(c);
}

std::optional<QPoint> BridgeTracker::pointerPosition()
{
    if (m_kind == Kind::Gnome) {
        QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.gnome.Shell"), QStringLiteral("/io/github/opendial/Shell"),
                                                        QStringLiteral("io.github.opendial.Shell"), QStringLiteral("GetPointer"));
        QDBusMessage r = QDBusConnection::sessionBus().call(m, QDBus::Block, 200);
        if (r.type() == QDBusMessage::ReplyMessage && r.arguments().size() == 2)
            return QPoint(r.arguments().at(0).toInt(), r.arguments().at(1).toInt());
    }
    return m_lastPointer;
}

// ========================================================= ContextManager

ContextManager::ContextManager(QObject *parent) : QObject(parent) { qRegisterMetaType<opendial::FocusContext>(); }

void ContextManager::start(const QString &preferred)
{
    auto use = [&](FocusTracker *t) {
        m_all << t;
        connect(t, &FocusTracker::focusChanged, this, &ContextManager::onFocus);
        t->start();
        if (!m_tracker)
            m_tracker = t;
    };
    const bool any = preferred.isEmpty() || preferred == QLatin1String("auto");
    if ((any || preferred == QLatin1String("hyprland")) && HyprlandTracker::detected())
        use(new HyprlandTracker(this));
    if ((any || preferred == QLatin1String("sway")) && SwayTracker::detected())
        use(new SwayTracker(this));
    if ((any || preferred == QLatin1String("kwin-script")) && BridgeTracker::kwinDetected()) {
        m_bridge = new BridgeTracker(BridgeTracker::Kind::KWin, this);
        use(m_bridge);
    }
    if ((any || preferred == QLatin1String("gnome-shell-extension")) && BridgeTracker::gnomeDetected() && !m_bridge) {
        m_bridge = new BridgeTracker(BridgeTracker::Kind::Gnome, this);
        use(m_bridge);
    }
#ifdef OPENDIAL_HAVE_WAYLAND
    if (!m_tracker && (any || preferred == QLatin1String("wlr-foreign-toplevel")) && WlrToplevelTracker::detected())
        use(new WlrToplevelTracker(this));
#endif
#ifdef OPENDIAL_HAVE_X11
    if (!m_tracker && (any || preferred == QLatin1String("x11")) && X11Tracker::detected())
        use(new X11Tracker(this));
#endif
    if (!m_bridge) {
        // Generic bridge: any integration may report focus via D-Bus.
        m_bridge = new BridgeTracker(BridgeTracker::Kind::Generic, this);
        m_all << m_bridge;
        connect(m_bridge, &FocusTracker::focusChanged, this, &ContextManager::onFocus);
    }
    if (!m_tracker)
        qCWarning(lcContext) << "no focus tracker for this session; app profiles need manual switching";
    else
        qCInfo(lcContext) << "focus tracker:" << m_tracker->id();
}

void ContextManager::enrich(FocusContext &c)
{
    if (c.pid <= 0)
        return;
    const QString proc = QStringLiteral("/proc/%1").arg(c.pid);
    const QString exe = QFileInfo(proc + QStringLiteral("/exe")).symLinkTarget();
    if (!exe.isEmpty())
        c.exe = QFileInfo(exe).fileName();
    QFile cg(proc + QStringLiteral("/cgroup"));
    if (cg.open(QIODevice::ReadOnly)) {
        static const QRegularExpression re(QStringLiteral("app-flatpak-([A-Za-z0-9_.-]+)-\\d+\\.scope"));
        auto m = re.match(QString::fromUtf8(cg.read(4096)));
        if (m.hasMatch())
            c.flatpakId = m.captured(1);
    }
    if (c.flatpakId.isEmpty() && c.exe == QLatin1String("bwrap")) {
        QFile info(proc + QStringLiteral("/root/.flatpak-info"));
        if (info.open(QIODevice::ReadOnly)) {
            static const QRegularExpression re(QStringLiteral("^name=(.+)$"), QRegularExpression::MultilineOption);
            auto m = re.match(QString::fromUtf8(info.read(4096)));
            if (m.hasMatch())
                c.flatpakId = m.captured(1).trimmed();
        }
    }
}

bool ContextManager::isSecure(const FocusContext &c) const
{
    static const QStringList builtin = {
        QStringLiteral("pinentry*"), QStringLiteral("*polkit*"), QStringLiteral("gcr-prompter"), QStringLiteral("org.gnome.keyring*"),
        QStringLiteral("*askpass*"), QStringLiteral("kwalletd*"), QStringLiteral("org.kde.kwalletd*"), QStringLiteral("hyprpolkitagent"),
        QStringLiteral("lxqt-policykit-agent"), QStringLiteral("org.kde.polkit-kde-authentication-agent-1"), QStringLiteral("xfce-polkit"),
        QStringLiteral("gnome-shell-polkit"), QStringLiteral("org.freedesktop.impl.portal.desktop.*"),
    };
    QStringList pats = builtin + m_userSecure;
    for (const QString &cand : appIdCandidates(c))
        for (const QString &p : pats) {
            const auto re = QRegularExpression(QRegularExpression::wildcardToRegularExpression(p.toLower()));
            if (re.match(cand).hasMatch())
                return true;
        }
    return false;
}

void ContextManager::onFocus(const FocusContext &in)
{
    FocusContext c = in;
    enrich(c);
    c.secure = c.secure || isSecure(c);
    if (c == m_current)
        return;
    m_current = c;
    if (!m_simulated)
        Q_EMIT contextChanged(c);
}

void ContextManager::simulate(const std::optional<FocusContext> &ctx)
{
    m_simulated = ctx;
    if (ctx) {
        m_simulated->source = QStringLiteral("simulated");
        m_simulated->secure = m_simulated->secure || isSecure(*m_simulated);
    }
    Q_EMIT contextChanged(current());
}

std::optional<QPoint> ContextManager::pointerPosition()
{
    if (m_tracker)
        if (auto p = m_tracker->pointerPosition())
            return p;
    if (m_bridge)
        return m_bridge->pointerPosition();
    return std::nullopt;
}

QJsonObject ContextManager::toJson(bool redact) const
{
    QJsonArray trackers;
    for (auto *t : m_all)
        trackers.append(t->toJson());
    return QJsonObject{{QStringLiteral("active"), m_tracker ? m_tracker->id() : QString()},
                       {QStringLiteral("trackers"), trackers},
                       {QStringLiteral("simulated"), isSimulated()},
                       {QStringLiteral("focus"), current().toJson(redact)}};
}

} // namespace opendial
