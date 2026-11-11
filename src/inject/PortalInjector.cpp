// SPDX-License-Identifier: MIT
#include "PortalInjector.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QLoggingCategory>
#include <QRandomGenerator>

Q_LOGGING_CATEGORY(lcPortal, "opendial.inject.portal")

namespace opendial {

namespace {
const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kIface = QStringLiteral("org.freedesktop.portal.RemoteDesktop");
constexpr uint kKeyboard = 1, kPointer = 2;
} // namespace

PortalInjector::PortalInjector(const QString &restoreToken, QObject *parent) : Injector(parent), m_restoreToken(restoreToken)
{
    auto bus = QDBusConnection::sessionBus();
    QDBusMessage m = QDBusMessage::createMethodCall(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    m << kIface << QStringLiteral("version");
    QDBusReply<QDBusVariant> r = bus.call(m, QDBus::Block, 1500);
    if (r.isValid()) {
        m_version = r.value().variant().toUInt();
        m_available = m_version >= 1;
    }
    if (m_available) {
        QDBusMessage d = QDBusMessage::createMethodCall(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        d << kIface << QStringLiteral("AvailableDeviceTypes");
        QDBusReply<QDBusVariant> dr = bus.call(d, QDBus::Block, 1500);
        const uint types = dr.isValid() ? dr.value().variant().toUInt() : 0;
        if (!(types & kKeyboard))
            m_available = false;
    }
    m_detail = m_available ? QStringLiteral("portal version %1; %2").arg(m_version).arg(
                                 m_restoreToken.isEmpty() ? QStringLiteral("permission not granted yet") : QStringLiteral("restore token stored"))
                           : QStringLiteral("RemoteDesktop portal not available in this session");
}

CapState PortalInjector::state() const
{
    if (!m_available)
        return CapState::NotSupported;
    if (m_phase == Phase::Active)
        return CapState::Supported;
    if (m_phase == Phase::Denied)
        return CapState::PermissionNeeded;
    return m_restoreToken.isEmpty() ? CapState::PermissionNeeded : CapState::FallbackAvailable;
}

QString PortalInjector::newToken()
{
    return QStringLiteral("opendial%1_%2").arg(++m_tokenSeq).arg(QRandomGenerator::global()->bounded(1u << 30));
}

void PortalInjector::requestPermission()
{
    if (!m_available || m_phase == Phase::Active || m_phase == Phase::Creating || m_phase == Phase::Selecting
        || m_phase == Phase::Starting)
        return;
    createSession();
}

void PortalInjector::call(const QString &method, const QVariantList &args, const QString &token)
{
    auto bus = QDBusConnection::sessionBus();
    // Request object path is predictable: subscribe before calling (portal spec).
    QString sender = bus.baseService().mid(1).replace(QLatin1Char('.'), QLatin1Char('_'));
    m_pendingRequest = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    bus.connect(kService, m_pendingRequest, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                this, SLOT(onResponse(uint, QVariantMap)));
    QDBusMessage m = QDBusMessage::createMethodCall(kService, kPath, kIface, method);
    m.setArguments(args);
    bus.asyncCall(m);
}

void PortalInjector::createSession()
{
    m_phase = Phase::Creating;
    const QString t = newToken();
    call(QStringLiteral("CreateSession"),
         {QVariantMap{{QStringLiteral("handle_token"), t}, {QStringLiteral("session_handle_token"), newToken()}}}, t);
}

void PortalInjector::onResponse(uint response, const QVariantMap &results)
{
    QDBusConnection::sessionBus().disconnect(kService, m_pendingRequest, QStringLiteral("org.freedesktop.portal.Request"),
                                             QStringLiteral("Response"), this, SLOT(onResponse(uint, QVariantMap)));
    if (response != 0) {
        qCWarning(lcPortal) << "portal request denied/cancelled at phase" << int(m_phase) << "response" << response;
        m_phase = Phase::Denied;
        m_detail = QStringLiteral("permission denied in the portal dialog");
        if (!m_restoreToken.isEmpty()) {
            m_restoreToken.clear(); // stale token
            Q_EMIT restoreTokenChanged(QString());
        }
        Q_EMIT stateChanged();
        return;
    }
    if (m_phase == Phase::Creating) {
        m_session = results.value(QStringLiteral("session_handle")).toString();
        m_phase = Phase::Selecting;
        const QString t = newToken();
        QVariantMap opts{{QStringLiteral("handle_token"), t}, {QStringLiteral("types"), kKeyboard | kPointer}};
        if (m_version >= 2) {
            opts.insert(QStringLiteral("persist_mode"), uint(2));
            if (!m_restoreToken.isEmpty())
                opts.insert(QStringLiteral("restore_token"), m_restoreToken);
        }
        call(QStringLiteral("SelectDevices"), {QVariant::fromValue(QDBusObjectPath(m_session)), opts}, t);
    } else if (m_phase == Phase::Selecting) {
        m_phase = Phase::Starting;
        const QString t = newToken();
        call(QStringLiteral("Start"), {QVariant::fromValue(QDBusObjectPath(m_session)), QString(), QVariantMap{{QStringLiteral("handle_token"), t}}}, t);
    } else if (m_phase == Phase::Starting) {
        m_phase = Phase::Active;
        const QString tok = results.value(QStringLiteral("restore_token")).toString();
        if (!tok.isEmpty() && tok != m_restoreToken) {
            m_restoreToken = tok;
            Q_EMIT restoreTokenChanged(tok);
        }
        m_detail = QStringLiteral("portal session active (version %1)").arg(m_version);
        qCInfo(lcPortal) << "RemoteDesktop session active";
        Q_EMIT stateChanged();
    }
}

void PortalInjector::notify(const QString &method, const QVariantList &args)
{
    QDBusMessage m = QDBusMessage::createMethodCall(kService, kPath, kIface, method);
    QVariantList a{QVariant::fromValue(QDBusObjectPath(m_session)), QVariantMap{}};
    a.append(args);
    m.setArguments(a);
    QDBusConnection::sessionBus().asyncCall(m);
}

bool PortalInjector::modifier(Modifier m, bool down)
{
    return key(keysymForModifier(m), down);
}

bool PortalInjector::key(quint32 keysym, bool down)
{
    if (m_phase != Phase::Active) {
        if (!m_restoreToken.isEmpty())
            requestPermission(); // silent restore
        return false;
    }
    // The compositor resolves keysym -> keycode (+ level modifiers) itself.
    notify(QStringLiteral("NotifyKeyboardKeysym"), {int(keysym), uint(down ? 1 : 0)});
    return true;
}

bool PortalInjector::scroll(Qt::Orientation o, double clicks, bool smooth)
{
    if (m_phase != Phase::Active)
        return false;
    if (smooth) {
        QDBusMessage m = QDBusMessage::createMethodCall(kService, kPath, kIface, QStringLiteral("NotifyPointerAxis"));
        const double dx = o == Qt::Horizontal ? clicks * 10.0 : 0.0;
        const double dy = o == Qt::Vertical ? clicks * 10.0 : 0.0;
        m.setArguments({QVariant::fromValue(QDBusObjectPath(m_session)), QVariantMap{{QStringLiteral("finish"), true}}, dx, dy});
        QDBusConnection::sessionBus().asyncCall(m);
    } else {
        const int n = int(std::lround(clicks));
        if (n == 0)
            return false;
        notify(QStringLiteral("NotifyPointerAxisDiscrete"), {uint(o == Qt::Vertical ? 0 : 1), n});
    }
    return true;
}

bool PortalInjector::button(int code, bool down)
{
    if (m_phase != Phase::Active)
        return false;
    notify(QStringLiteral("NotifyPointerButton"), {code, uint(down ? 1 : 0)});
    return true;
}

} // namespace opendial
