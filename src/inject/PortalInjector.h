// SPDX-License-Identifier: MIT
#pragma once

#include "Injector.h"

#include <QDBusObjectPath>
#include <QVariantMap>
#include <functional>

namespace opendial {

// org.freedesktop.portal.RemoteDesktop (GNOME/Mutter, KDE/KWin, and any
// portal backend implementing it). The user grants access once in a system
// dialog; the restore token keeps the grant across restarts (persist_mode 2).
class PortalInjector : public Injector {
    Q_OBJECT
public:
    explicit PortalInjector(const QString &restoreToken, QObject *parent = nullptr);
    QString id() const override { return QStringLiteral("xdg-portal-remotedesktop"); }
    QString label() const override { return QStringLiteral("XDG RemoteDesktop portal"); }
    CapState state() const override;

    bool modifier(Modifier m, bool down) override;
    bool key(quint32 keysym, bool down) override;
    bool scroll(Qt::Orientation o, double clicks, bool smooth) override;
    bool button(int code, bool down) override;
    void requestPermission() override;

    bool available() const { return m_available; }
    QString restoreToken() const { return m_restoreToken; }

Q_SIGNALS:
    void restoreTokenChanged(const QString &token);

private Q_SLOTS:
    void onResponse(uint response, const QVariantMap &results);

private:
    enum class Phase { Idle, Creating, Selecting, Starting, Active, Denied };
    void createSession();
    void call(const QString &method, const QVariantList &args, const QString &token);
    QString newToken();
    void notify(const QString &method, const QVariantList &args);

    bool m_available = false;
    uint m_version = 0;
    Phase m_phase = Phase::Idle;
    QString m_session;
    QString m_restoreToken;
    QString m_pendingRequest;
    int m_tokenSeq = 0;
};

} // namespace opendial
