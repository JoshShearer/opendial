// SPDX-License-Identifier: MIT
#pragma once

#include "Config.h"
#include "DialDecoder.h"
#include "GestureEngine.h"
#include "Profiles.h"
#include "RadialMenu.h"
#include "Rotation.h"

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <deque>
#include <memory>

namespace opendial {

class ActionRunner;
class AudioController;
class BluezMonitor;
class BrightnessController;
class ContextManager;
class DialDevice;
class InjectorManager;
class MediaController;
class Notifier;
class SessionMonitor;
class WorkspaceController;
struct ActionContext;
struct CommandEvent;

struct EngineOptions {
    bool noDevice = false;      // do not open hardware (tests / simulation)
    bool noContext = false;     // do not start focus trackers
    bool noInjection = false;   // never inject (dry-run): actions are logged only
    QString configPath;         // override $XDG_CONFIG_HOME/opendial/config.json
    QString statePath;
};

// Registered third-party integrations (Krita plugin, scripts, ...). Tools are
// shown only after the user approved the app (consent persisted in state).
struct PluginApp {
    QString appId;
    QString name;
    QString busName;           // unique D-Bus name of the registering process
    QStringList matchAppIds;   // focus apps where the tools appear (empty = always)
    QJsonArray tools;
    bool approved = false;
};

class Engine : public QObject {
    Q_OBJECT
public:
    explicit Engine(const EngineOptions &opts, QObject *parent = nullptr);
    ~Engine() override;
    void start();

    // ---- inputs
    void onDialSignal(const DialSignal &s);
    // Simulation (D-Bus Simulate / tests): {"button":"down"|"up"} |
    // {"rotate":degrees} | {"click":n} | {"longPress":true} | {"sequence":[...]}
    bool simulate(const QJsonObject &cmd, QString *error);
    void reportFocus(const QJsonObject &payload);
    bool simulateContext(const QJsonObject &ctx); // empty = stop simulating

    // ---- config
    QJsonObject config() const { return m_config; }
    ValidationResult setConfig(const QJsonObject &cfg, QString *error);
    bool resetToDefaults(QString *error);
    int rollbackConfig(QString *error);
    QJsonObject importProfile(const QJsonObject &doc);
    QJsonObject exportProfile(const QString &id) const;

    // ---- control (D-Bus, tray, actions)
    bool setActiveTool(const QString &id);
    void toolOp(const QString &op, const QString &id, int steps = 1);
    void profileOp(const QString &op, const QString &id);
    bool lockProfile(const QString &id);
    void unlockProfile();
    void menuOp(const QString &op);
    void haptic(hid::Waveform w, int repeat = 1);
    bool testHaptics(const QString &which);
    void setPaused(bool p);
    void completeFirstRun();
    bool paused() const { return m_paused; }
    void reconnect();
    void requestPortalPermission();
    QJsonObject bluetooth(const QString &action, const QString &path);
    void setRawStreaming(bool on);

    // ---- approvals
    bool isApproved(const QString &fingerprint) const { return m_approvals.contains(fingerprint); }
    void requestApproval(const QJsonObject &action, const QString &toolId, const QString &profileId);
    QJsonArray pendingApprovals() const;
    bool approve(const QString &fingerprint);
    bool revokeApproval(const QString &fingerprint);

    // ---- plugins
    QJsonObject registerApp(const QJsonObject &reg, const QString &busName);
    bool unregisterApp(const QString &appId, const QString &busName);
    bool approvePlugin(const QString &appId, bool approved);
    bool emitPluginEvent(const QString &appId, const QJsonObject &ev);
    void busNameVanished(const QString &busName);
    bool pluginFeedback(const QString &appId, const QJsonObject &fb, const QString &busName);

    // ---- variables (value tools)
    QJsonValue variable(const QString &name) const { return m_variables.value(name); }
    void setVariable(const QString &name, double v);

    // ---- presentation
    void showOsd(const QJsonObject &osd);
    QJsonObject stateJson() const;
    QJsonObject diagnostics(bool redact) const;
    QJsonObject menuJson() const;
    QJsonArray menuTools() const { return m_menuTools; }
    QString activeToolId() const { return m_activeToolId; }
    QString activeProfileId() const { return m_resolution.profileId; }
    QJsonObject activeTool() const;
    QJsonObject settings() const { return m_config.value(QStringLiteral("settings")).toObject(); }

    DialDevice *device() const { return m_device; }
    ContextManager *context() const { return m_context; }
    InjectorManager *injector() const { return m_inj; }

Q_SIGNALS:
    void stateChanged(const QJsonObject &state);
    void configChanged(const QJsonObject &config);
    void rawEvent(const QJsonObject &ev);
    void gestureEvent(const QJsonObject &ev);
    void pluginEvent(const QString &appId, const QJsonObject &ev);
    void menuChanged(const QJsonObject &menu);   // {"open":false} when closed
    void osdRequested(const QJsonObject &osd);
    void approvalsChanged();
    void notifyChanged();

private:
    // gesture pipeline
    void feedButton(bool down, qint64 nowMs);
    void feedRotation(double degrees, qint64 nowMs);
    void handleGestures(const std::vector<GestureEvent> &evs);
    void handleGesture(const GestureEvent &e);
    void scheduleGestureTimer();
    void runSlot(const QString &slotName, int steps, int direction, bool fine = false);
    bool toolHasSlot(const QString &slotName) const;
    void onCommandEvent(const CommandEvent &e);
    void logEvent(const QJsonObject &e);

    // menu
    void openMenu();
    void closeMenu(const QString &reason);
    void menuRotate(double degrees, qint64 nowMs);
    void menuSelect();
    void publishMenu();

    // configuration
    void loadConfiguration();
    void applySettings();
    void saveState();
    void loadState();
    void resolveContext(bool force = false);
    void rebuildMenuTools();
    void applyHardwareMode();
    void updateSuppression();
    void scheduleStateChanged();
    QJsonArray filterAvailable(const QJsonArray &tools) const;
    QJsonArray pluginTools() const;
    int clickCountNeeded() const;
    double unitsToDegrees() const;

    EngineOptions m_opts;
    std::unique_ptr<JsonStore> m_store;
    std::unique_ptr<JsonStore> m_stateStore;
    QJsonObject m_config;
    LoadedConfig m_loaded;
    QFileSystemWatcher m_watcher;
    QString m_lastWrittenHash;

    GestureEngine m_gestures;
    RotationProcessor m_rotation;
    RotationProcessor m_menuRotation;
    RadialMenuModel m_menu;
    QElapsedTimer m_clock;
    QTimer m_gestureTimer;
    QTimer m_menuTimeout;
    QTimer m_stateTimer;
    QTimer m_varSaveTimer;
    bool m_menuHeldOpen = false; // opened by long press, button still down

    DialDevice *m_device = nullptr;
    InjectorManager *m_inj = nullptr;
    ContextManager *m_context = nullptr;
    AudioController *m_audio = nullptr;
    MediaController *m_media = nullptr;
    BrightnessController *m_brightness = nullptr;
    WorkspaceController *m_workspace = nullptr;
    Notifier *m_notifier = nullptr;
    SessionMonitor *m_session = nullptr;
    BluezMonitor *m_bluez = nullptr;
    ActionRunner *m_runner = nullptr;

    // state
    Resolution m_resolution;
    FocusContext m_focus;
    QJsonArray m_menuTools;
    QString m_activeToolId;
    QJsonObject m_activeTools;       // profileId -> toolId
    QString m_lockedProfile;
    QSet<QString> m_approvals;
    QHash<QString, QJsonObject> m_pending; // fingerprint -> {action, toolId, profileId, description}
    QJsonObject m_variables;
    QJsonObject m_pluginConsent;      // appId -> bool
    QHash<QString, PluginApp> m_plugins;
    QString m_portalToken;
    bool m_firstRun = true;
    bool m_paused = false;
    bool m_suppressed = false;
    QString m_suppressReason;
    bool m_lowBatteryWarned = false;
    std::deque<QJsonObject> m_log;
    QJsonObject m_lastGesture;
    QString m_lastActionResult;
    qint64 m_lastActionLatencyUs = 0;
    quint64 m_lastCommandId = 0;            // command behind m_lastActionResult, 0 = none
    QHash<QString, qint64> m_commandNotified; // fingerprint -> last failure notification (ms)
    int m_signalsReceived = 0;
};

} // namespace opendial
