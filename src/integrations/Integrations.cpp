// SPDX-License-Identifier: MIT
#include "Integrations.h"
#include "FocusTrackers.h"
#include "InjectorManager.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

Q_LOGGING_CATEGORY(lcIntegr, "opendial.integrations")

using InterfaceMap = QMap<QString, QVariantMap>;
using ManagedObjects = QMap<QDBusObjectPath, InterfaceMap>;
Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ManagedObjects)

namespace opendial {

// ================================================================== Audio

AudioController::AudioController(QObject *parent) : QObject(parent)
{
    if (!QStandardPaths::findExecutable(QStringLiteral("wpctl")).isEmpty())
        m_backend = QStringLiteral("wpctl");
    else if (!QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty())
        m_backend = QStringLiteral("pactl");
}

CapState AudioController::state() const { return m_backend.isEmpty() ? CapState::NotSupported : CapState::Supported; }

QJsonObject AudioController::toJson() const
{
    QJsonObject o{{QStringLiteral("backend"), m_backend}, {QStringLiteral("state"), capStateName(state())}};
    if (m_sinkVol)
        o.insert(QStringLiteral("volume"), *m_sinkVol);
    o.insert(QStringLiteral("muted"), m_sinkMuted);
    return o;
}

void AudioController::run(const QStringList &argv, std::function<void(int, const QByteArray &)> done)
{
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [p, done](int code, QProcess::ExitStatus) {
        done(code, p->readAllStandardOutput());
        p->deleteLater();
    });
    connect(p, &QProcess::errorOccurred, this, [p, done](QProcess::ProcessError) {
        if (p->state() == QProcess::NotRunning) {
            done(-1, {});
            p->deleteLater();
        }
    });
    p->start(argv.first(), argv.mid(1));
}

void AudioController::adjust(bool source, double delta, double maxPercent)
{
    if (m_backend.isEmpty())
        return;
    m_pending[source] += delta;
    m_max[source] = qBound(10.0, maxPercent, 150.0);
    pump(source);
}

void AudioController::pump(bool source)
{
    if (m_busy[source] || m_pending[source] == 0.0)
        return;
    const double d = m_pending[source];
    m_pending[source] = 0;
    m_busy[source] = true;
    QStringList argv;
    const QString pct = QString::number(std::abs(d), 'f', 1) + QLatin1Char('%');
    if (m_backend == QLatin1String("wpctl")) {
        argv = {QStringLiteral("wpctl"), QStringLiteral("set-volume"), QStringLiteral("-l"), QString::number(m_max[source] / 100.0, 'f', 2),
                source ? QStringLiteral("@DEFAULT_AUDIO_SOURCE@") : QStringLiteral("@DEFAULT_AUDIO_SINK@"),
                pct + (d > 0 ? QLatin1Char('+') : QLatin1Char('-'))};
    } else {
        argv = {QStringLiteral("pactl"), source ? QStringLiteral("set-source-volume") : QStringLiteral("set-sink-volume"),
                source ? QStringLiteral("@DEFAULT_SOURCE@") : QStringLiteral("@DEFAULT_SINK@"),
                (d > 0 ? QStringLiteral("+") : QStringLiteral("-")) + pct};
    }
    run(argv, [this, source](int, const QByteArray &) {
        refresh(source);
        m_busy[source] = false;
        pump(source);
    });
}

void AudioController::toggleMute(bool source)
{
    if (m_backend.isEmpty())
        return;
    QStringList argv = m_backend == QLatin1String("wpctl")
                           ? QStringList{QStringLiteral("wpctl"), QStringLiteral("set-mute"),
                                         source ? QStringLiteral("@DEFAULT_AUDIO_SOURCE@") : QStringLiteral("@DEFAULT_AUDIO_SINK@"), QStringLiteral("toggle")}
                           : QStringList{QStringLiteral("pactl"), source ? QStringLiteral("set-source-mute") : QStringLiteral("set-sink-mute"),
                                         source ? QStringLiteral("@DEFAULT_SOURCE@") : QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("toggle")};
    run(argv, [this, source](int, const QByteArray &) { refresh(source); });
}

void AudioController::refresh(bool source)
{
    if (m_backend == QLatin1String("wpctl")) {
        run({QStringLiteral("wpctl"), QStringLiteral("get-volume"), source ? QStringLiteral("@DEFAULT_AUDIO_SOURCE@") : QStringLiteral("@DEFAULT_AUDIO_SINK@")},
            [this, source](int code, const QByteArray &out) {
                // "Volume: 0.45 [MUTED]"
                static const QRegularExpression re(QStringLiteral("Volume:\\s*([0-9.]+)"));
                auto m = re.match(QString::fromUtf8(out));
                if (code != 0 || !m.hasMatch())
                    return;
                const double v = m.captured(1).toDouble();
                const bool muted = out.contains("MUTED");
                (source ? m_srcVol : m_sinkVol) = v;
                (source ? m_srcMuted : m_sinkMuted) = muted;
                Q_EMIT volumeChanged(source, v, muted);
            });
    } else if (m_backend == QLatin1String("pactl")) {
        run({QStringLiteral("pactl"), source ? QStringLiteral("get-source-volume") : QStringLiteral("get-sink-volume"),
             source ? QStringLiteral("@DEFAULT_SOURCE@") : QStringLiteral("@DEFAULT_SINK@")},
            [this, source](int code, const QByteArray &out) {
                static const QRegularExpression re(QStringLiteral("(\\d+)%"));
                auto m = re.match(QString::fromUtf8(out));
                if (code != 0 || !m.hasMatch())
                    return;
                const double v = m.captured(1).toDouble() / 100.0;
                (source ? m_srcVol : m_sinkVol) = v;
                run({QStringLiteral("pactl"), source ? QStringLiteral("get-source-mute") : QStringLiteral("get-sink-mute"),
                     source ? QStringLiteral("@DEFAULT_SOURCE@") : QStringLiteral("@DEFAULT_SINK@")},
                    [this, source, v](int, const QByteArray &o2) {
                        const bool muted = o2.contains("yes");
                        (source ? m_srcMuted : m_sinkMuted) = muted;
                        Q_EMIT volumeChanged(source, v, muted);
                    });
            });
    }
}

// ================================================================== MPRIS

namespace {
const QString kMprisPrefix = QStringLiteral("org.mpris.MediaPlayer2.");
const QString kMprisPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kPlayerIface = QStringLiteral("org.mpris.MediaPlayer2.Player");

QVariant mprisProp(const QString &svc, const QString &prop)
{
    QDBusMessage m = QDBusMessage::createMethodCall(svc, kMprisPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    m << kPlayerIface << prop;
    QDBusReply<QDBusVariant> r = QDBusConnection::sessionBus().call(m, QDBus::Block, 300);
    return r.isValid() ? r.value().variant() : QVariant();
}
} // namespace

MediaController::MediaController(QObject *parent) : QObject(parent)
{
    auto bus = QDBusConnection::sessionBus();
    bus.connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
                QStringLiteral("NameOwnerChanged"), this, SLOT(onNameOwnerChanged(QString, QString, QString)));
    bus.connect(QString(), kMprisPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
                SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    scan();
}

void MediaController::scan()
{
    m_players.clear();
    const auto names = QDBusConnection::sessionBus().interface()->registeredServiceNames().value();
    for (const auto &n : names)
        if (n.startsWith(kMprisPrefix))
            m_players << n;
    Q_EMIT playersChanged();
}

void MediaController::onNameOwnerChanged(const QString &name, const QString &, const QString &)
{
    if (name.startsWith(kMprisPrefix))
        scan();
}

void MediaController::onPropertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &)
{
    if (iface != kPlayerIface)
        return;
    // Remember the most recently started player so it wins ties when several
    // report "Playing" (e.g. a paused browser tab plus a music player).
    if (changed.value(QStringLiteral("PlaybackStatus")).toString() == QLatin1String("Playing")) {
        const QString sender = message().service();
        // sender is a unique name (:1.42); map it back to the well-known name
        for (const auto &p : std::as_const(m_players))
            if (QDBusConnection::sessionBus().interface()->serviceOwner(p).value() == sender)
                m_lastPlaying = p;
    }
}

QString MediaController::activePlayer() const
{
    // Prefer a playing player, then paused, then any. Ignore our own names.
    QString paused, any;
    if (!m_lastPlaying.isEmpty() && m_players.contains(m_lastPlaying)
        && mprisProp(m_lastPlaying, QStringLiteral("PlaybackStatus")).toString() == QLatin1String("Playing"))
        return m_lastPlaying;
    for (const auto &p : m_players) {
        const QString st = mprisProp(p, QStringLiteral("PlaybackStatus")).toString();
        if (st == QLatin1String("Playing"))
            return p;
        if (st == QLatin1String("Paused") && paused.isEmpty())
            paused = p;
        if (any.isEmpty())
            any = p;
    }
    return !paused.isEmpty() ? paused : any;
}

QString MediaController::nowPlaying() const
{
    const QString p = activePlayer();
    if (p.isEmpty())
        return {};
    const QVariantMap meta = qdbus_cast<QVariantMap>(mprisProp(p, QStringLiteral("Metadata")));
    const QString title = meta.value(QStringLiteral("xesam:title")).toString();
    return title.left(80);
}

bool MediaController::command(const QString &cmd, qint64 offsetMs, double rateDelta)
{
    const QString p = activePlayer();
    if (p.isEmpty())
        return false;
    QString method;
    QVariantList args;
    if (cmd == QLatin1String("playpause")) method = QStringLiteral("PlayPause");
    else if (cmd == QLatin1String("play")) method = QStringLiteral("Play");
    else if (cmd == QLatin1String("pause")) method = QStringLiteral("Pause");
    else if (cmd == QLatin1String("stop")) method = QStringLiteral("Stop");
    else if (cmd == QLatin1String("next")) method = QStringLiteral("Next");
    else if (cmd == QLatin1String("previous")) method = QStringLiteral("Previous");
    else if (cmd == QLatin1String("seek")) {
        method = QStringLiteral("Seek");
        args << QVariant::fromValue(qlonglong(offsetMs) * 1000);
    } else if (cmd == QLatin1String("rate") || cmd == QLatin1String("volume")) {
        const QString prop = cmd == QLatin1String("rate") ? QStringLiteral("Rate") : QStringLiteral("Volume");
        double v = mprisProp(p, prop).toDouble() + rateDelta;
        v = cmd == QLatin1String("rate") ? qBound(0.25, v, 4.0) : qBound(0.0, v, 1.0);
        QDBusMessage m = QDBusMessage::createMethodCall(p, kMprisPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"));
        m << kPlayerIface << prop << QVariant::fromValue(QDBusVariant(v));
        QDBusConnection::sessionBus().asyncCall(m);
        return true;
    } else
        return false;
    QDBusMessage m = QDBusMessage::createMethodCall(p, kMprisPath, kPlayerIface, method);
    m.setArguments(args);
    QDBusConnection::sessionBus().asyncCall(m);
    return true;
}

QJsonObject MediaController::toJson() const
{
    return QJsonObject{{QStringLiteral("players"), QJsonArray::fromStringList(m_players)},
                       {QStringLiteral("active"), activePlayer()},
                       {QStringLiteral("state"), capStateName(state())}};
}

// ============================================================= Brightness

BrightnessController::BrightnessController(QObject *parent) : QObject(parent)
{
    QDir d(QStringLiteral("/sys/class/backlight"));
    const auto list = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System);
    // Prefer firmware/platform interfaces over raw (kernel guidance).
    QString pick;
    for (const QString &pref : {QStringLiteral("firmware"), QStringLiteral("platform"), QStringLiteral("raw")})
        for (const auto &n : list) {
            QFile t(d.filePath(n) + QStringLiteral("/type"));
            if (pick.isEmpty() && t.open(QIODevice::ReadOnly) && t.readAll().trimmed() == pref.toUtf8())
                pick = n;
        }
    if (pick.isEmpty() && !list.isEmpty())
        pick = list.first();
    m_device = pick;
    if (!m_device.isEmpty()) {
        QFile mx(d.filePath(m_device) + QStringLiteral("/max_brightness"));
        if (mx.open(QIODevice::ReadOnly))
            m_max = mx.readAll().trimmed().toInt();
    }
    // logind SetBrightness exists since systemd 243 and is authorised for the active session.
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1/session/auto"),
                                                    QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
    QDBusReply<QString> r = QDBusConnection::systemBus().call(m, QDBus::Block, 1000);
    m_logind = r.isValid() && r.value().contains(QLatin1String("SetBrightness"));
    if (m_device.isEmpty())
        m_detail = QStringLiteral("no backlight device (external monitors need DDC/CI via ddcutil)");
    else
        m_detail = QStringLiteral("%1 via %2").arg(m_device, m_logind ? QStringLiteral("logind") : QStringLiteral("brightnessctl"));
}

CapState BrightnessController::state() const
{
    if (!m_device.isEmpty() && m_max > 0 && (m_logind || !QStandardPaths::findExecutable(QStringLiteral("brightnessctl")).isEmpty()))
        return CapState::Supported;
    if (m_ddc && !QStandardPaths::findExecutable(QStringLiteral("ddcutil")).isEmpty())
        return CapState::FallbackAvailable;
    return CapState::NotSupported;
}

std::optional<double> BrightnessController::percent() const
{
    if (m_device.isEmpty() || m_max <= 0)
        return std::nullopt;
    QFile f(QStringLiteral("/sys/class/backlight/%1/brightness").arg(m_device));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return 100.0 * f.readAll().trimmed().toInt() / m_max;
}

bool BrightnessController::adjust(double delta)
{
    if (!m_device.isEmpty() && m_max > 0) {
        const double cur = percent().value_or(50);
        // never go fully dark from the Dial: 1% floor
        const double target = qBound(1.0, cur + delta, 100.0);
        const uint raw = uint(std::lround(target / 100.0 * m_max));
        if (m_logind) {
            QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1/session/auto"),
                                                            QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("SetBrightness"));
            m << m_subsystem << m_device << raw;
            QDBusConnection::systemBus().asyncCall(m);
            return true;
        }
        return QProcess::startDetached(QStringLiteral("brightnessctl"), {QStringLiteral("-q"), QStringLiteral("set"), QString::number(raw)});
    }
    if (m_ddc && !QStandardPaths::findExecutable(QStringLiteral("ddcutil")).isEmpty()) {
        const int d = int(std::lround(delta));
        return QProcess::startDetached(QStringLiteral("ddcutil"), {QStringLiteral("--noverify"), QStringLiteral("setvcp"), QStringLiteral("10"),
                                                                  d >= 0 ? QStringLiteral("+") : QStringLiteral("-"), QString::number(std::abs(d))});
    }
    return false;
}

QJsonObject BrightnessController::toJson() const
{
    QJsonObject o{{QStringLiteral("device"), m_device}, {QStringLiteral("logind"), m_logind}, {QStringLiteral("ddc"), m_ddc},
                  {QStringLiteral("state"), capStateName(state())}, {QStringLiteral("detail"), m_detail}};
    if (auto p = percent())
        o.insert(QStringLiteral("percent"), *p);
    return o;
}

// ============================================================== Workspace

WorkspaceController::WorkspaceController(InjectorManager *inj, QObject *parent) : QObject(parent), m_inj(inj)
{
    const QString desk = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    if (HyprlandTracker::detected())
        m_method = QStringLiteral("hyprland-dispatch");
    else if (SwayTracker::detected())
        m_method = QStringLiteral("sway-ipc");
    else if (desk.contains(QLatin1String("KDE"), Qt::CaseInsensitive))
        m_method = QStringLiteral("kwin-dbus");
    else if (desk.contains(QLatin1String("GNOME"), Qt::CaseInsensitive))
        m_method = QStringLiteral("gnome-keys");
    else if (desk.contains(QLatin1String("COSMIC"), Qt::CaseInsensitive))
        m_method = QStringLiteral("cosmic-keys");
    else
        m_method = QStringLiteral("keys");
}

CapState WorkspaceController::state() const
{
    if (m_method == QLatin1String("hyprland-dispatch") || m_method == QLatin1String("sway-ipc") || m_method == QLatin1String("kwin-dbus"))
        return CapState::Supported;
    return m_inj->keyboardCapability() == QLatin1String("supported") ? CapState::FallbackAvailable : CapState::NotSupported;
}

bool WorkspaceController::step(int dir)
{
    if (m_method == QLatin1String("hyprland-dispatch"))
        return !HyprlandTracker::request(dir > 0 ? "dispatch workspace e+1" : "dispatch workspace e-1").isEmpty();
    if (m_method == QLatin1String("sway-ipc"))
        return SwayTracker::command(dir > 0 ? "workspace next_on_output" : "workspace prev_on_output");
    if (m_method == QLatin1String("kwin-dbus")) {
        QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"),
                                                        dir > 0 ? QStringLiteral("nextDesktop") : QStringLiteral("previousDesktop"));
        QDBusConnection::sessionBus().asyncCall(m);
        return true;
    }
    // Documented default shortcuts (GNOME: Super+PageDown/Up; COSMIC: Super+Ctrl+Down/Up).
    QString keys;
    if (m_method == QLatin1String("gnome-keys"))
        keys = dir > 0 ? QStringLiteral("Super+Page_Down") : QStringLiteral("Super+Page_Up");
    else if (m_method == QLatin1String("cosmic-keys"))
        keys = dir > 0 ? QStringLiteral("Super+Ctrl+Down") : QStringLiteral("Super+Ctrl+Up");
    else
        keys = dir > 0 ? QStringLiteral("Ctrl+Alt+Right") : QStringLiteral("Ctrl+Alt+Left");
    auto r = parseKeySequence(keys);
    return r.ok() && m_inj->tapSequence(r.sequence);
}

// =========================================================== Notifications

Notifier::Notifier(QObject *parent) : QObject(parent) {}

void Notifier::notify(const QString &summary, const QString &body, bool important, const QString &icon)
{
    if (!m_enabled || (m_quiet && !important))
        return;
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                                                    QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    m << QStringLiteral("OpenDial") << m_lastId << icon << summary << body << QStringList() << QVariantMap{{QStringLiteral("urgency"), QVariant::fromValue(uchar(important ? 2 : 1))}}
      << int(important ? -1 : 4000);
    QDBusConnection::sessionBus().asyncCall(m);
}

// ========================================================== SessionMonitor

SessionMonitor::SessionMonitor(QObject *parent) : QObject(parent)
{
    auto sys = QDBusConnection::systemBus();
    QDBusMessage get = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
                                                      QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("GetSessionByPID"));
    get << uint(QCoreApplication::applicationPid());
    QDBusReply<QDBusObjectPath> r = sys.call(get, QDBus::Block, 1000);
    if (!r.isValid()) {
        // systemd --user services are not inside the session scope: use "auto"
        QDBusMessage disp = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1/user/self"),
                                                           QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        disp << QStringLiteral("org.freedesktop.login1.User") << QStringLiteral("Display");
        QDBusMessage dr = sys.call(disp, QDBus::Block, 1000);
        if (dr.type() == QDBusMessage::ReplyMessage && !dr.arguments().isEmpty()) {
            const QDBusArgument arg = qvariant_cast<QDBusVariant>(dr.arguments().first()).variant().value<QDBusArgument>();
            QString sid;
            QDBusObjectPath p;
            arg.beginStructure();
            arg >> sid >> p;
            arg.endStructure();
            m_sessionPath = p.path();
        }
    } else {
        m_sessionPath = r.value().path();
    }
    if (!m_sessionPath.isEmpty() && m_sessionPath != QLatin1String("/")) {
        sys.connect(QStringLiteral("org.freedesktop.login1"), m_sessionPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                    QStringLiteral("PropertiesChanged"), this, SLOT(onLogindProps(QString, QVariantMap, QStringList)));
        QDBusMessage lh = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"), m_sessionPath,
                                                         QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        lh << QStringLiteral("org.freedesktop.login1.Session") << QStringLiteral("LockedHint");
        QDBusReply<QDBusVariant> lr = sys.call(lh, QDBus::Block, 1000);
        m_logindLocked = lr.isValid() && lr.value().variant().toBool();
    }
    sys.connect(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"), QStringLiteral("org.freedesktop.login1.Manager"),
                QStringLiteral("PrepareForSleep"), this, SLOT(onPrepareForSleep(bool)));
    for (const QString &svc : {QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("org.gnome.ScreenSaver")})
        QDBusConnection::sessionBus().connect(svc, QStringLiteral("/org/freedesktop/ScreenSaver"), svc, QStringLiteral("ActiveChanged"), this,
                                              SLOT(onScreenSaver(bool)));
    QDBusConnection::sessionBus().connect(QStringLiteral("org.gnome.ScreenSaver"), QStringLiteral("/org/gnome/ScreenSaver"),
                                          QStringLiteral("org.gnome.ScreenSaver"), QStringLiteral("ActiveChanged"), this, SLOT(onScreenSaver(bool)));
    setLocked(m_logindLocked);
}

void SessionMonitor::onLogindProps(const QString &, const QVariantMap &changed, const QStringList &)
{
    if (changed.contains(QStringLiteral("LockedHint"))) {
        m_logindLocked = changed.value(QStringLiteral("LockedHint")).toBool();
        setLocked(m_logindLocked || m_saverActive);
    }
}

void SessionMonitor::onScreenSaver(bool active)
{
    m_saverActive = active;
    setLocked(m_logindLocked || m_saverActive);
}

void SessionMonitor::onPrepareForSleep(bool start)
{
    if (start)
        Q_EMIT aboutToSleep();
    else
        Q_EMIT resumed();
}

void SessionMonitor::setLocked(bool l)
{
    if (l == m_locked)
        return;
    m_locked = l;
    qCInfo(lcIntegr) << "session" << (l ? "locked: automation disabled" : "unlocked");
    Q_EMIT lockedChanged(l);
}

QJsonObject SessionMonitor::toJson() const
{
    return QJsonObject{{QStringLiteral("locked"), m_locked}, {QStringLiteral("logindSession"), !m_sessionPath.isEmpty()}};
}

// ================================================================== BlueZ

BluezMonitor::BluezMonitor(QObject *parent) : QObject(parent)
{
    qDBusRegisterMetaType<InterfaceMap>();
    qDBusRegisterMetaType<ManagedObjects>();
    auto sys = QDBusConnection::systemBus();
    sys.connect(QStringLiteral("org.bluez"), QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("InterfacesAdded"),
                this, SLOT(onInterfacesAdded(QDBusObjectPath, QMap<QString, QVariantMap>)));
    sys.connect(QStringLiteral("org.bluez"), QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("InterfacesRemoved"),
                this, SLOT(onInterfacesRemoved(QDBusObjectPath, QStringList)));
    scan();
}

bool BluezMonitor::isDial(const QVariantMap &p) const
{
    const QString modalias = p.value(QStringLiteral("Modalias")).toString().toLower();
    if (modalias.contains(QLatin1String("v045ep091b")))
        return true;
    return p.value(QStringLiteral("Name")).toString().contains(QLatin1String("Surface Dial"), Qt::CaseInsensitive);
}

void BluezMonitor::scan()
{
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                                    QStringLiteral("GetManagedObjects"));
    QDBusReply<ManagedObjects> r = QDBusConnection::systemBus().call(m, QDBus::Block, 2000);
    m_available = r.isValid();
    if (!r.isValid())
        return;
    for (auto it = r.value().cbegin(); it != r.value().cend(); ++it)
        onInterfacesAdded(it.key(), it.value());
}

void BluezMonitor::onInterfacesAdded(const QDBusObjectPath &path, const QMap<QString, QVariantMap> &ifaces)
{
    const QString p = path.path();
    if (ifaces.contains(QStringLiteral("org.bluez.Adapter1")) && m_adapter.isEmpty()) {
        m_adapter = p;
        m_adapterPowered = ifaces.value(QStringLiteral("org.bluez.Adapter1")).value(QStringLiteral("Powered")).toBool();
        watch(p);
    }
    if (ifaces.contains(QStringLiteral("org.bluez.Device1"))) {
        const auto d = ifaces.value(QStringLiteral("org.bluez.Device1"));
        if (!isDial(d) && !m_devs.contains(p))
            return;
        Dev &dev = m_devs[p];
        dev.path = p;
        dev.name = d.value(QStringLiteral("Name"), dev.name).toString();
        dev.address = d.value(QStringLiteral("Address"), dev.address).toString();
        dev.adapter = d.value(QStringLiteral("Adapter")).value<QDBusObjectPath>().path();
        dev.paired = d.value(QStringLiteral("Paired"), dev.paired).toBool();
        dev.bonded = d.value(QStringLiteral("Bonded"), dev.bonded).toBool();
        dev.connected = d.value(QStringLiteral("Connected"), dev.connected).toBool();
        dev.trusted = d.value(QStringLiteral("Trusted"), dev.trusted).toBool();
        dev.rssi = d.value(QStringLiteral("RSSI"), dev.rssi).toInt();
        watch(p);
    }
    if (ifaces.contains(QStringLiteral("org.bluez.Battery1")) && m_devs.contains(p))
        m_devs[p].battery = ifaces.value(QStringLiteral("org.bluez.Battery1")).value(QStringLiteral("Percentage")).toInt();
    Q_EMIT changed();
}

void BluezMonitor::onInterfacesRemoved(const QDBusObjectPath &path, const QStringList &ifaces)
{
    if (ifaces.contains(QStringLiteral("org.bluez.Device1")))
        m_devs.remove(path.path());
    else if (ifaces.contains(QStringLiteral("org.bluez.Battery1")) && m_devs.contains(path.path()))
        m_devs[path.path()].battery.reset();
    Q_EMIT changed();
}

void BluezMonitor::watch(const QString &path)
{
    QDBusConnection::systemBus().connect(QStringLiteral("org.bluez"), path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                         QStringLiteral("PropertiesChanged"), this, SLOT(onProps(QString, QVariantMap, QStringList)));
}

void BluezMonitor::onProps(const QString &iface, const QVariantMap &c, const QStringList &)
{
    const QString path = message().path();
    if (iface == QLatin1String("org.bluez.Adapter1") && path == m_adapter) {
        if (c.contains(QStringLiteral("Powered")))
            m_adapterPowered = c.value(QStringLiteral("Powered")).toBool();
        if (c.contains(QStringLiteral("Discovering")))
            m_discovering = c.value(QStringLiteral("Discovering")).toBool();
    } else if (iface == QLatin1String("org.bluez.Device1")) {
        if (!m_devs.contains(path)) {
            if (!isDial(c))
                return;
        }
        Dev &d = m_devs[path];
        d.path = path;
        if (c.contains(QStringLiteral("Connected"))) d.connected = c.value(QStringLiteral("Connected")).toBool();
        if (c.contains(QStringLiteral("Paired"))) d.paired = c.value(QStringLiteral("Paired")).toBool();
        if (c.contains(QStringLiteral("Bonded"))) d.bonded = c.value(QStringLiteral("Bonded")).toBool();
        if (c.contains(QStringLiteral("Trusted"))) d.trusted = c.value(QStringLiteral("Trusted")).toBool();
        if (c.contains(QStringLiteral("RSSI"))) d.rssi = c.value(QStringLiteral("RSSI")).toInt();
        if (c.contains(QStringLiteral("Name"))) d.name = c.value(QStringLiteral("Name")).toString();
    } else if (iface == QLatin1String("org.bluez.Battery1") && m_devs.contains(path)) {
        if (c.contains(QStringLiteral("Percentage")))
            m_devs[path].battery = c.value(QStringLiteral("Percentage")).toInt();
    } else {
        return;
    }
    Q_EMIT changed();
}

QString BluezMonitor::primaryPath() const
{
    for (const auto &d : m_devs)
        if (d.connected)
            return d.path;
    for (const auto &d : m_devs)
        if (d.paired)
            return d.path;
    return m_devs.isEmpty() ? QString() : m_devs.cbegin()->path;
}

std::optional<int> BluezMonitor::battery() const
{
    const QString p = primaryPath();
    return p.isEmpty() ? std::nullopt : m_devs.value(p).battery;
}

QJsonObject BluezMonitor::toJson(bool redact) const
{
    QJsonArray devs;
    for (const auto &d : m_devs) {
        QJsonObject o{{QStringLiteral("path"), d.path},
                      {QStringLiteral("name"), d.name},
                      {QStringLiteral("address"), redact ? QStringLiteral("<redacted>") : d.address},
                      {QStringLiteral("paired"), d.paired},
                      {QStringLiteral("bonded"), d.bonded},
                      {QStringLiteral("connected"), d.connected},
                      {QStringLiteral("trusted"), d.trusted},
                      {QStringLiteral("rssi"), d.rssi}};
        o.insert(QStringLiteral("battery"), d.battery ? QJsonValue(*d.battery) : QJsonValue(QStringLiteral("not reported")));
        devs.append(o);
    }
    return QJsonObject{{QStringLiteral("available"), m_available},
                       {QStringLiteral("adapter"), m_adapter},
                       {QStringLiteral("adapterPowered"), m_adapterPowered},
                       {QStringLiteral("discovering"), m_discovering},
                       {QStringLiteral("devices"), devs},
                       {QStringLiteral("primary"), primaryPath()}};
}

void BluezMonitor::callDevice(const QString &path, const QString &method, const QString &label)
{
    if (!m_devs.contains(path) && method != QLatin1String("RemoveDevice")) {
        Q_EMIT actionResult(label, false, QStringLiteral("unknown device"));
        return;
    }
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), path, QStringLiteral("org.bluez.Device1"), method);
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(m, 30000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, label] {
        QDBusPendingReply<> r = *w;
        Q_EMIT actionResult(label, !r.isError(), r.isError() ? r.error().message() : QStringLiteral("ok"));
        w->deleteLater();
    });
}

void BluezMonitor::connectDevice(const QString &p) { callDevice(p, QStringLiteral("Connect"), QStringLiteral("connect")); }
void BluezMonitor::disconnectDevice(const QString &p) { callDevice(p, QStringLiteral("Disconnect"), QStringLiteral("disconnect")); }
void BluezMonitor::pair(const QString &p) { callDevice(p, QStringLiteral("Pair"), QStringLiteral("pair")); }

void BluezMonitor::trust(const QString &p)
{
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), p, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"));
    m << QStringLiteral("org.bluez.Device1") << QStringLiteral("Trusted") << QVariant::fromValue(QDBusVariant(true));
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(m), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        QDBusPendingReply<> r = *w;
        Q_EMIT actionResult(QStringLiteral("trust"), !r.isError(), r.isError() ? r.error().message() : QStringLiteral("ok"));
        w->deleteLater();
    });
}

void BluezMonitor::remove(const QString &p)
{
    if (!m_devs.contains(p))
        return;
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), m_devs.value(p).adapter.isEmpty() ? m_adapter : m_devs.value(p).adapter,
                                                    QStringLiteral("org.bluez.Adapter1"), QStringLiteral("RemoveDevice"));
    m << QVariant::fromValue(QDBusObjectPath(p));
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(m), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        QDBusPendingReply<> r = *w;
        Q_EMIT actionResult(QStringLiteral("remove"), !r.isError(), r.isError() ? r.error().message() : QStringLiteral("ok"));
        w->deleteLater();
    });
}

void BluezMonitor::startDiscovery()
{
    if (m_adapter.isEmpty()) {
        Q_EMIT actionResult(QStringLiteral("discover"), false, QStringLiteral("no Bluetooth adapter"));
        return;
    }
    QDBusMessage f = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), m_adapter, QStringLiteral("org.bluez.Adapter1"), QStringLiteral("SetDiscoveryFilter"));
    f << QVariantMap{{QStringLiteral("Transport"), QStringLiteral("le")}};
    QDBusConnection::systemBus().call(f, QDBus::Block, 1000);
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), m_adapter, QStringLiteral("org.bluez.Adapter1"), QStringLiteral("StartDiscovery"));
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(m), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        QDBusPendingReply<> r = *w;
        Q_EMIT actionResult(QStringLiteral("discover"), !r.isError(), r.isError() ? r.error().message() : QStringLiteral("scanning: hold the Dial's pairing button (under the base) until the LED blinks"));
        w->deleteLater();
    });
}

void BluezMonitor::stopDiscovery()
{
    if (m_adapter.isEmpty())
        return;
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), m_adapter, QStringLiteral("org.bluez.Adapter1"), QStringLiteral("StopDiscovery"));
    QDBusConnection::systemBus().asyncCall(m);
}

} // namespace opendial
