// SPDX-License-Identifier: MIT
#pragma once

#include "Injector.h"

#include <QDBusContext>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <functional>
#include <optional>

namespace opendial {

class InjectorManager;

// ------------------------------------------------------------------ Audio
// PipeWire via wpctl (WirePlumber) with pactl (PulseAudio / pipewire-pulse)
// fallback. Rapid rotation is coalesced: one helper process in flight per
// target, accumulated delta applied when it finishes.
class AudioController : public QObject {
    Q_OBJECT
public:
    explicit AudioController(QObject *parent = nullptr);
    CapState state() const;
    QString backend() const { return m_backend; }
    void adjust(bool source, double deltaPercent, double maxPercent);
    void toggleMute(bool source);
    // last known volume (0..1.5) and mute state, refreshed after each change
    std::optional<double> volume(bool source) const { return source ? m_srcVol : m_sinkVol; }
    bool muted(bool source) const { return source ? m_srcMuted : m_sinkMuted; }
    void refresh(bool source);
    QJsonObject toJson() const;
Q_SIGNALS:
    void volumeChanged(bool source, double volume, bool muted);
private:
    void pump(bool source);
    void run(const QStringList &argv, std::function<void(int, const QByteArray &)> done);
    QString m_backend;
    double m_pending[2] = {0, 0};
    double m_max[2] = {100, 100};
    bool m_busy[2] = {false, false};
    std::optional<double> m_sinkVol, m_srcVol;
    bool m_sinkMuted = false, m_srcMuted = false;
};

// ------------------------------------------------------------------ MPRIS
class MediaController : public QObject, protected QDBusContext {
    Q_OBJECT
public:
    explicit MediaController(QObject *parent = nullptr);
    CapState state() const { return m_players.isEmpty() ? CapState::FallbackAvailable : CapState::Supported; }
    bool command(const QString &cmd, qint64 offsetMs = 0, double rateDelta = 0);
    QString activePlayer() const;
    QStringList players() const { return m_players; }
    QJsonObject toJson() const;
    QString nowPlaying() const;
Q_SIGNALS:
    void playersChanged();
private Q_SLOTS:
    void onNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void onPropertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &invalidated);
private:
    void scan();
    QStringList m_players;
    QString m_lastPlaying;
};

// ------------------------------------------------------------- Brightness
// logind Session.SetBrightness (unprivileged for the active session),
// brightnessctl fallback; external monitors via ddcutil only if enabled.
class BrightnessController : public QObject {
    Q_OBJECT
public:
    explicit BrightnessController(QObject *parent = nullptr);
    CapState state() const;
    void setDdcEnabled(bool on) { m_ddc = on; }
    bool adjust(double deltaPercent);
    std::optional<double> percent() const;
    QJsonObject toJson() const;
private:
    QString m_device;   // e.g. intel_backlight
    QString m_subsystem = QStringLiteral("backlight");
    int m_max = 0;
    bool m_logind = false;
    bool m_ddc = false;
    QString m_detail;
};

// -------------------------------------------------------------- Workspace
class WorkspaceController : public QObject {
    Q_OBJECT
public:
    WorkspaceController(InjectorManager *inj, QObject *parent = nullptr);
    CapState state() const;
    QString method() const { return m_method; }
    bool step(int direction);
private:
    InjectorManager *m_inj;
    QString m_method;
};

// ----------------------------------------------------------- Notifications
class Notifier : public QObject {
    Q_OBJECT
public:
    explicit Notifier(QObject *parent = nullptr);
    void setEnabled(bool on, bool quiet) { m_enabled = on; m_quiet = quiet; }
    void notify(const QString &summary, const QString &body, bool important = false, const QString &icon = QStringLiteral("io.github.joshshearer.opendial.OpenDial"));
private:
    bool m_enabled = true;
    bool m_quiet = false;
    uint m_lastId = 0;
};

// --------------------------------------------------------- Session/locking
class SessionMonitor : public QObject {
    Q_OBJECT
public:
    explicit SessionMonitor(QObject *parent = nullptr);
    bool locked() const { return m_locked; }
    QJsonObject toJson() const;
Q_SIGNALS:
    void lockedChanged(bool locked);
    void aboutToSleep();
    void resumed();
private Q_SLOTS:
    void onLogindProps(const QString &iface, const QVariantMap &changed, const QStringList &invalidated);
    void onScreenSaver(bool active);
    void onPrepareForSleep(bool start);
private:
    void setLocked(bool l);
    QString m_sessionPath;
    bool m_locked = false;
    bool m_logindLocked = false;
    bool m_saverActive = false;
};

// ------------------------------------------------------------------ BlueZ
class BluezMonitor : public QObject, protected QDBusContext {
    Q_OBJECT
public:
    explicit BluezMonitor(QObject *parent = nullptr);
    QJsonObject toJson(bool redact) const;
    bool available() const { return m_available; }
    std::optional<int> battery() const;
    void connectDevice(const QString &path);
    void disconnectDevice(const QString &path);
    void trust(const QString &path);
    void pair(const QString &path);
    void remove(const QString &path);
    void startDiscovery();
    void stopDiscovery();
    QString primaryPath() const;
Q_SIGNALS:
    void changed();
    void actionResult(const QString &action, bool ok, const QString &message);
private Q_SLOTS:
    void onInterfacesAdded(const QDBusObjectPath &path, const QMap<QString, QVariantMap> &ifaces);
    void onInterfacesRemoved(const QDBusObjectPath &path, const QStringList &ifaces);
    void onProps(const QString &iface, const QVariantMap &changed, const QStringList &invalidated);
private:
    void scan();
    void watch(const QString &path);
    void callDevice(const QString &path, const QString &method, const QString &label);
    bool isDial(const QVariantMap &props) const;
    struct Dev {
        QString path, name, address, adapter;
        bool paired = false, connected = false, trusted = false, bonded = false;
        int rssi = 0;
        std::optional<int> battery;
    };
    QHash<QString, Dev> m_devs;
    QString m_adapter;
    bool m_adapterPowered = false;
    bool m_available = false;
    bool m_discovering = false;
};

} // namespace opendial
