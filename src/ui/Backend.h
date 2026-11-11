// SPDX-License-Identifier: MIT
#pragma once

#include <QDBusServiceWatcher>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariant>

namespace opendial {

// Bridge between the QML settings app and opendiald (D-Bus). Holds an
// editable working copy of the configuration; edits are validated and
// applied after a short debounce, like the Windows wheel settings.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool daemonRunning READ daemonRunning NOTIFY daemonRunningChanged)
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap config READ config NOTIFY configChanged)
    Q_PROPERTY(QStringList errors READ errors NOTIFY validationChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY validationChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY validationChanged)
    Q_PROPERTY(QVariantList events READ events NOTIFY eventsChanged)
    Q_PROPERTY(QStringList icons READ icons CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY messageChanged)
public:
    explicit Backend(QObject *parent = nullptr);

    bool daemonRunning() const { return m_running; }
    QVariantMap state() const { return m_state.toVariantMap(); }
    QVariantMap config() const { return m_config.toVariantMap(); }
    QStringList errors() const { return m_errors; }
    QStringList warnings() const { return m_warnings; }
    bool dirty() const { return m_dirty; }
    QVariantList events() const { return m_events; }
    QStringList icons() const;
    QString version() const;
    QString lastMessage() const { return m_message; }

    // ---- config editing (dot paths: "settings.menu.size", "profiles.2.tools.0.name")
    Q_INVOKABLE QVariant get(const QString &path) const;
    Q_INVOKABLE void set(const QString &path, const QVariant &value);
    Q_INVOKABLE void remove(const QString &path);              // array element or key
    Q_INVOKABLE void insert(const QString &arrayPath, int index, const QVariant &value);
    Q_INVOKABLE void move(const QString &arrayPath, int from, int to);
    Q_INVOKABLE void applyNow();
    Q_INVOKABLE void revert();
    Q_INVOKABLE void resetDefaults();
    Q_INVOKABLE void rollback();

    // ---- helpers for editors
    Q_INVOKABLE QString describeAction(const QVariantMap &action) const;
    Q_INVOKABLE QString validateKeys(const QString &keys) const;     // "" when valid
    Q_INVOKABLE QString chordFromKeyEvent(int key, int modifiers, quint32 nativeScanCode) const;
    Q_INVOKABLE QVariantList builtinTools() const;                  // catalogue to re-add
    Q_INVOKABLE QVariantMap newTool(const QString &name) const;
    Q_INVOKABLE QVariantMap newProfile(const QString &name, const QString &appId) const;
    Q_INVOKABLE QString uniqueId(const QString &base) const;
    Q_INVOKABLE QVariantList installedApps() const;
    Q_INVOKABLE QStringList actionTypes() const;
    Q_INVOKABLE QString riskOf(const QVariantMap &action) const;     // "safe" | "approval"
    Q_INVOKABLE int profileIndex(const QString &id) const;

    // ---- daemon control
    Q_INVOKABLE void startDaemon();
    Q_INVOKABLE bool autostartEnabled() const;
    Q_INVOKABLE void setAutostart(bool on);
    Q_INVOKABLE void setActiveTool(const QString &id);
    Q_INVOKABLE void lockProfile(const QString &id);
    Q_INVOKABLE void unlockProfile();
    Q_INVOKABLE void openMenu();
    Q_INVOKABLE void closeMenu();
    Q_INVOKABLE bool testHaptics(const QString &waveform);
    Q_INVOKABLE void simulate(const QVariantMap &cmd);
    Q_INVOKABLE void simulateContext(const QString &appId);
    Q_INVOKABLE void setPaused(bool p);
    Q_INVOKABLE void completeFirstRun();
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void requestPortalPermission();
    Q_INVOKABLE QVariantList approvals();
    Q_INVOKABLE bool approve(const QString &fingerprint);
    Q_INVOKABLE bool revokeApproval(const QString &fingerprint);
    Q_INVOKABLE bool approvePlugin(const QString &appId, bool allow);
    Q_INVOKABLE QVariantMap bluetooth(const QString &action, const QString &path = {});
    Q_INVOKABLE QVariantMap diagnostics(bool redact);
    Q_INVOKABLE QString exportDiagnostics(const QUrl &file, bool redact);
    Q_INVOKABLE QString exportProfile(const QString &id, const QUrl &file);
    Q_INVOKABLE QString importProfile(const QUrl &file);
    Q_INVOKABLE void setRawStreaming(bool on);
    Q_INVOKABLE void clearEvents();
    Q_INVOKABLE void copyToClipboard(const QString &text);
    Q_INVOKABLE void openUrl(const QString &url);

Q_SIGNALS:
    void daemonRunningChanged();
    void stateChanged();
    void configChanged();
    void validationChanged();
    void eventsChanged();
    void messageChanged();
    void gesture(const QVariantMap &event);

private Q_SLOTS:
    void onState(const QString &json);
    void onConfig(const QString &json);
    void onGesture(const QString &json);
    void onRaw(const QString &json);

private:
    void connectDaemon();
    void refresh();
    void scheduleApply();
    void setMessage(const QString &m);
    QString callString(const char *method, const QVariantList &args = {}) const;
    bool callBool(const char *method, const QVariantList &args = {}) const;
    void appendEvent(const QVariantMap &e);

    bool m_running = false;
    bool m_dirty = false;
    QJsonObject m_state;
    QJsonObject m_config;
    QJsonObject m_applied;
    QStringList m_errors, m_warnings;
    QVariantList m_events;
    QTimer m_apply;
    QDBusServiceWatcher m_watcher;
    QString m_message;
};

} // namespace opendial
