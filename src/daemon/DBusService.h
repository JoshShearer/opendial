// SPDX-License-Identifier: MIT
#pragma once

#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QObject>

namespace opendial {

class Engine;

inline const QString kDBusService = QStringLiteral("io.github.opendial");
inline const QString kDBusPath = QStringLiteral("/io/github/opendial/Daemon");
inline const QString kDBusInterface = QStringLiteral("io.github.opendial.Daemon1");

// Session-bus API (docs/dbus-api.md). Structured values are JSON strings so
// clients in any language need no custom D-Bus type marshalling.
class DBusService : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.opendial.Daemon1")
    Q_PROPERTY(QString Version READ version)
    Q_PROPERTY(int ApiVersion READ apiVersion)
public:
    explicit DBusService(Engine *engine, QObject *parent = nullptr);
    bool registerOnBus(QString *error);
    QString version() const;
    int apiVersion() const { return 1; }

public Q_SLOTS:
    QString GetState();
    QString GetConfig();
    QString SetConfig(const QString &json);
    QString ValidateConfig(const QString &json);
    QString ResetToDefaults();
    QString RollbackConfig();
    QString ImportProfile(const QString &json);
    QString ExportProfile(const QString &id);

    bool SetActiveTool(const QString &id);
    bool LockProfile(const QString &id);
    void UnlockProfile();
    void OpenMenu();
    void CloseMenu();
    void SetPaused(bool paused);
    void CompleteFirstRun();
    bool TestHaptics(const QString &waveform);
    void Reconnect();
    void RequestPortalPermission();
    QString Bluetooth(const QString &action, const QString &path);

    QString Simulate(const QString &json);
    bool SimulateContext(const QString &json);
    void ReportFocus(const QString &json);
    void SetRawStreaming(bool on);

    QString GetDiagnostics(bool redact);
    QString ExportDiagnostics(const QString &path, bool redact);

    QString ListApprovals();
    bool ApproveAction(const QString &fingerprint);
    bool RevokeApproval(const QString &fingerprint);

    QString RegisterApp(const QString &json);
    bool UnregisterApp(const QString &appId);
    bool ApprovePlugin(const QString &appId, bool approved);
    bool PluginFeedback(const QString &appId, const QString &json);

    QString ListTools();
    QString ListProfiles();

Q_SIGNALS:
    void StateChanged(const QString &json);
    void ConfigChanged(const QString &json);
    void RawEvent(const QString &json);
    void GestureEvent(const QString &json);
    void PluginEvent(const QString &appId, const QString &json);
    void MenuChanged(const QString &json);

private:
    Engine *m_engine;
    QDBusServiceWatcher m_watcher;
};

} // namespace opendial
