// SPDX-License-Identifier: MIT
#include "Engine.h"
#include "ActionRunner.h"
#include "DialDevice.h"
#include "FocusTrackers.h"
#include "InjectorManager.h"
#include "Integrations.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSysInfo>
#include <cmath>

Q_LOGGING_CATEGORY(lcEngine, "opendial.engine")

namespace opendial {

namespace {
constexpr int kLogSize = 300;

QJsonObject obj(const QJsonObject &o, const char *k) { return o.value(QLatin1String(k)).toObject(); }

QString hashOf(const QJsonObject &o) { return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(o).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex()); }

// Actions considered harmless enough to run on the lock screen / over a
// password prompt (same as hardware media keys).
bool lockSafe(const QJsonObject &a)
{
    static const QSet<QString> safe{QStringLiteral("volume"), QStringLiteral("mute"), QStringLiteral("media"),
                                    QStringLiteral("brightness"), QStringLiteral("haptic"), QStringLiteral("none")};
    return safe.contains(a.value(QStringLiteral("type")).toString());
}
} // namespace

Engine::Engine(const EngineOptions &opts, QObject *parent) : QObject(parent), m_opts(opts)
{
    qRegisterMetaType<DialSignal>();
    qRegisterMetaType<RawEvent>();
    qRegisterMetaType<FocusContext>();
    m_clock.start();
    m_gestureTimer.setSingleShot(true);
    m_gestureTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_gestureTimer, &QTimer::timeout, this, [this] { handleGestures(m_gestures.onTimer(m_clock.elapsed())); });
    m_menuTimeout.setSingleShot(true);
    connect(&m_menuTimeout, &QTimer::timeout, this, [this] { closeMenu(QStringLiteral("timeout")); });
    m_varSaveTimer.setSingleShot(true);
    m_varSaveTimer.setInterval(2000);
    connect(&m_varSaveTimer, &QTimer::timeout, this, &Engine::saveState);
    m_stateTimer.setSingleShot(true);
    m_stateTimer.setInterval(30);
    connect(&m_stateTimer, &QTimer::timeout, this, [this] { Q_EMIT stateChanged(stateJson()); });

    const QString cfgPath = opts.configPath.isEmpty() ? configDir() + QStringLiteral("/config.json") : opts.configPath;
    const QString statePath = opts.statePath.isEmpty() ? stateDir() + QStringLiteral("/state.json") : opts.statePath;
    m_store = std::make_unique<JsonStore>(cfgPath);
    m_stateStore = std::make_unique<JsonStore>(statePath, 2);

    m_inj = new InjectorManager(this);
    m_audio = new AudioController(this);
    m_media = new MediaController(this);
    m_brightness = new BrightnessController(this);
    m_workspace = new WorkspaceController(m_inj, this);
    m_notifier = new Notifier(this);
    m_session = new SessionMonitor(this);
    m_bluez = new BluezMonitor(this);
    m_context = new ContextManager(this);
    m_device = new DialDevice(this);
    m_runner = new ActionRunner(this, m_inj, m_audio, m_media, m_brightness, m_workspace, this);
}

Engine::~Engine()
{
    m_inj->releaseAll();
    saveState();
}

void Engine::start()
{
    loadState();
    loadConfiguration();

    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &) {
        // editors and our own atomic writes replace the file: re-arm the watch
        QTimer::singleShot(150, this, [this] {
            if (!m_watcher.files().contains(m_store->path()) && QFile::exists(m_store->path()))
                m_watcher.addPath(m_store->path());
            QJsonObject onDisk;
            QString err;
            if (!m_store->read(onDisk, &err) || hashOf(onDisk) == m_lastWrittenHash)
                return;
            qCInfo(lcEngine) << "config changed on disk, reloading";
            loadConfiguration();
        });
    });
    QDir().mkpath(QFileInfo(m_store->path()).absolutePath());
    if (QFile::exists(m_store->path()))
        m_watcher.addPath(m_store->path());
    m_watcher.addPath(QFileInfo(m_store->path()).absolutePath());
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        if (!m_watcher.files().contains(m_store->path()) && QFile::exists(m_store->path()))
            m_watcher.addPath(m_store->path());
    });

    // ---- device
    connect(m_device, &DialDevice::signalReceived, this, &Engine::onDialSignal);
    connect(m_device, &DialDevice::connected, this, [this](const QJsonObject &) {
        logEvent({{QStringLiteral("kind"), QStringLiteral("connected")}});
        m_gestures.reset();
        m_rotation.reset();
        applyHardwareMode();
        m_notifier->notify(tr("Surface Dial connected"), tr("Active tool: %1").arg(activeTool().value(QStringLiteral("name")).toString()));
        if (auto b = m_device->batteryPercent().has_value() ? m_device->batteryPercent() : m_bluez->battery(); b && *b <= 15 && !m_lowBatteryWarned) {
            m_lowBatteryWarned = true;
            m_notifier->notify(tr("Surface Dial battery low"), tr("%1% remaining. Replace the two AAA batteries soon.").arg(*b), true);
        }
        scheduleStateChanged();
    });
    connect(m_device, &DialDevice::disconnected, this, [this](const QString &reason) {
        logEvent({{QStringLiteral("kind"), QStringLiteral("disconnected")}, {QStringLiteral("reason"), reason}});
        m_gestures.reset();
        m_gestureTimer.stop();
        m_rotation.reset();
        closeMenu(QStringLiteral("disconnect"));
        m_runner->cancelPending();
        m_inj->releaseAll();
        scheduleStateChanged();
    });
    connect(m_device, &DialDevice::hapticsChanged, this, &Engine::scheduleStateChanged);
    connect(m_device, &DialDevice::rawEvent, this, [this](const RawEvent &e, bool control) {
        Q_EMIT rawEvent(QJsonObject{{QStringLiteral("t"), double(e.timeUs)}, {QStringLiteral("type"), e.type},
                                    {QStringLiteral("code"), e.code}, {QStringLiteral("value"), e.value},
                                    {QStringLiteral("node"), control ? QStringLiteral("control") : QStringLiteral("multiaxis")}});
    });

    // ---- context / session
    connect(m_context, &ContextManager::contextChanged, this, [this](const FocusContext &c) {
        m_focus = c;
        resolveContext();
        updateSuppression();
    });
    connect(m_session, &SessionMonitor::lockedChanged, this, [this](bool) { updateSuppression(); });
    connect(m_session, &SessionMonitor::aboutToSleep, this, [this] {
        closeMenu(QStringLiteral("sleep"));
        m_inj->releaseAll();
        m_gestures.reset();
        saveState();
    });
    connect(m_session, &SessionMonitor::resumed, this, [this] {
        // BLE devices often reconnect after resume; the connect guard handles the flood.
        m_rotation.reset();
        applyHardwareMode();
    });
    connect(m_bluez, &BluezMonitor::changed, this, &Engine::scheduleStateChanged);
    connect(m_inj, &InjectorManager::changed, this, &Engine::scheduleStateChanged);
    connect(m_inj, &InjectorManager::portalRestoreTokenChanged, this, [this](const QString &t) {
        m_portalToken = t;
        saveState();
    });
    connect(m_media, &MediaController::playersChanged, this, &Engine::scheduleStateChanged);
    connect(m_audio, &AudioController::volumeChanged, this, [this](bool source, double v, bool muted) {
        showOsd(QJsonObject{{QStringLiteral("icon"), muted ? (source ? QStringLiteral("mic-off") : QStringLiteral("mute")) : (source ? QStringLiteral("mic") : QStringLiteral("volume"))},
                            {QStringLiteral("title"), source ? tr("Microphone") : tr("Volume")},
                            {QStringLiteral("text"), muted ? tr("Muted") : QStringLiteral("%1%").arg(std::lround(v * 100))},
                            {QStringLiteral("value"), qBound(0.0, v, 1.0)},
                            {QStringLiteral("muted"), muted}});
    });

    // Initial injection backend/permission; restore portal grant.
    applySettings();
    if (!m_opts.noContext)
        m_context->start(obj(settings(), "context").value(QStringLiteral("backend")).toString(QStringLiteral("auto")));
    m_focus = m_context->current();
    resolveContext(true);
    updateSuppression();
    if (!m_opts.noDevice)
        m_device->start();
    scheduleStateChanged();
}

// ======================================================================= input

void Engine::onDialSignal(const DialSignal &s)
{
    ++m_signalsReceived;
    const qint64 now = m_clock.elapsed();
    if (s.kind == DialSignal::Button)
        feedButton(s.down, now);
    else if (s.kind == DialSignal::Rotate)
        feedRotation(s.units * unitsToDegrees(), now);
}

double Engine::unitsToDegrees() const
{
    // With hidraw access OpenDial sets the resolution itself, so it is exact.
    if (m_device->hapticsAvailable())
        return m_device->appliedMode().unitsToDegrees();
    // Otherwise the device runs at its power-on resolution; a calibration
    // (Rotation & haptics page) may correct the assumed default.
    const int override_ = obj(settings(), "device").value(QStringLiteral("unitsPerTurn")).toInt(0);
    if (override_ > 0)
        return 360.0 / override_;
    return 360.0 / 3600.0; // assumed kernel default (see docs/hardware-protocol.md)
}

void Engine::feedButton(bool down, qint64 now)
{
    if (m_paused)
        return;
    handleGestures(m_gestures.onButton(down, now));
}

void Engine::feedRotation(double degrees, qint64 now)
{
    if (m_paused || degrees == 0.0)
        return;
    handleGestures(m_gestures.onRotate(degrees, now));
}

void Engine::handleGestures(const std::vector<GestureEvent> &evs)
{
    for (const auto &e : evs)
        handleGesture(e);
    scheduleGestureTimer();
}

void Engine::scheduleGestureTimer()
{
    if (auto d = m_gestures.nextDeadline())
        m_gestureTimer.start(int(qMax<qint64>(0, *d - m_clock.elapsed())));
    else
        m_gestureTimer.stop();
}

void Engine::handleGesture(const GestureEvent &e)
{
    QJsonObject ge{{QStringLiteral("kind"), gestureKindName(e.kind)}, {QStringLiteral("t"), double(e.timeMs)}};
    if (e.kind == GestureKind::Click)
        ge.insert(QStringLiteral("count"), e.count);
    if (e.kind == GestureKind::Rotate) {
        ge.insert(QStringLiteral("degrees"), e.degrees);
        ge.insert(QStringLiteral("pressed"), e.pressed);
    }
    if (e.kind != GestureKind::Rotate)
        logEvent(ge);
    m_lastGesture = ge;
    Q_EMIT gestureEvent(ge);

    const QJsonObject menuS = obj(settings(), "menu");
    const QString openGesture = menuS.value(QStringLiteral("openGesture")).toString(QStringLiteral("long_press"));
    const bool locked = m_suppressed && m_suppressReason != QLatin1String("paused");

    switch (e.kind) {
    case GestureKind::PhysicalDown:
    case GestureKind::PhysicalUp:
        return;
    case GestureKind::LatchChanged:
        showOsd(QJsonObject{{QStringLiteral("icon"), QStringLiteral("press")},
                            {QStringLiteral("title"), e.latched ? tr("Press latched") : tr("Press released")}});
        return;
    case GestureKind::LongPress:
        if (m_menu.isOpen()) {
            if (!m_menu.back())
                closeMenu(QStringLiteral("long press"));
            else
                publishMenu();
            return;
        }
        if (openGesture == QLatin1String("long_press") && !locked) {
            openMenu();
            m_menuHeldOpen = true;
            return;
        }
        runSlot(slot::LongPress, 1, 0);
        return;
    case GestureKind::Release:
        if (m_menuHeldOpen) {
            m_menuHeldOpen = false;
            return;
        }
        runSlot(slot::Release, 1, 0);
        return;
    case GestureKind::Click:
        if (m_menu.isOpen()) {
            menuSelect();
            return;
        }
        if ((openGesture == QLatin1String("double_click") && e.count == 2) || (openGesture == QLatin1String("triple_click") && e.count == 3)
            || (openGesture == QLatin1String("click") && e.count == 1)) {
            if (!locked)
                openMenu();
            return;
        }
        runSlot(e.count == 3 ? slot::TripleClick : e.count == 2 ? slot::DoubleClick : slot::Click, 1, 0);
        return;
    case GestureKind::Rotate: {
        const qint64 now = e.timeMs;
        if (m_menu.isOpen()) {
            menuRotate(e.degrees, now);
            return;
        }
        if (e.pressed) {
            const QString mode = obj(settings(), "gestures").value(QStringLiteral("pressedRotation")).toString(QStringLiteral("rotate"));
            const bool hasPressed = toolHasSlot(slot::PressedCw) || toolHasSlot(slot::PressedCcw);
            if (hasPressed) {
                const auto out = m_rotation.process(e.degrees, now);
                if (out.steps)
                    runSlot(out.steps > 0 ? slot::PressedCw : slot::PressedCcw, std::abs(out.steps), out.steps > 0 ? 1 : -1);
                return;
            }
            if (mode == QLatin1String("ignore"))
                return;
            const bool fine = mode == QLatin1String("fine");
            const auto out = m_rotation.process(e.degrees, now, fine);
            if (out.steps)
                runSlot(out.steps > 0 ? slot::RotateCw : slot::RotateCcw, std::abs(out.steps), out.steps > 0 ? 1 : -1, fine);
            return;
        }
        const auto out = m_rotation.process(e.degrees, now);
        if (out.steps)
            runSlot(out.steps > 0 ? slot::RotateCw : slot::RotateCcw, std::abs(out.steps), out.steps > 0 ? 1 : -1);
        return;
    }
    }
}

bool Engine::toolHasSlot(const QString &slotName) const
{
    const auto b = activeTool().value(QStringLiteral("bindings")).toObject().value(slotName).toObject();
    return !b.isEmpty() && b.value(QStringLiteral("type")).toString() != QLatin1String("none");
}

void Engine::runSlot(const QString &slotName, int steps, int direction, bool fine)
{
    const QJsonObject tool = activeTool();
    const QJsonObject action = tool.value(QStringLiteral("bindings")).toObject().value(slotName).toObject();
    if (action.isEmpty())
        return;
    if (m_suppressed) {
        const bool allowLocked = obj(settings(), "security").value(QStringLiteral("allowMediaWhenLocked")).toBool(true);
        if (m_suppressReason == QLatin1String("paused") || !allowLocked || !lockSafe(action)) {
            logEvent({{QStringLiteral("kind"), QStringLiteral("suppressed")}, {QStringLiteral("slot"), slotName}, {QStringLiteral("reason"), m_suppressReason}});
            return;
        }
    }
    ActionContext ctx;
    ctx.steps = steps;
    ctx.direction = direction;
    ctx.slot = slotName;
    ctx.toolId = m_activeToolId;
    ctx.profileId = m_resolution.profileId;
    ctx.focus = m_focus;
    ctx.fine = fine;
    QElapsedTimer t;
    t.start();
    ActionResult r;
    ctx.dryRun = m_opts.noInjection;
    // Injection backends must never see input while suppressed.
    if (m_suppressed && !ctx.dryRun)
        m_inj->setSuppressed(false);
    r = m_runner->run(action, ctx);
    if (m_suppressed && !ctx.dryRun)
        m_inj->setSuppressed(true);
    m_lastActionLatencyUs = t.nsecsElapsed() / 1000;
    m_lastActionResult = (r.ok ? QStringLiteral("ok") : QStringLiteral("failed")) + (r.message.isEmpty() ? QString() : QStringLiteral(": ") + r.message);
    QJsonObject le{{QStringLiteral("kind"), QStringLiteral("action")}, {QStringLiteral("slot"), slotName},
                   {QStringLiteral("tool"), m_activeToolId}, {QStringLiteral("steps"), steps * (direction ? direction : 1)},
                   {QStringLiteral("action"), describeAction(action)}, {QStringLiteral("ok"), r.ok},
                   {QStringLiteral("latencyUs"), double(m_lastActionLatencyUs)}};
    if (!r.message.isEmpty())
        le.insert(QStringLiteral("message"), r.message);
    logEvent(le);
    if (!r.ok && !r.needsApproval)
        qCInfo(lcEngine) << "action failed" << slotName << describeAction(action) << r.message;
}

void Engine::logEvent(const QJsonObject &e)
{
    QJsonObject x = e;
    if (!x.contains(QStringLiteral("t")))
        x.insert(QStringLiteral("t"), double(m_clock.elapsed()));
    m_log.push_back(x);
    while (m_log.size() > kLogSize)
        m_log.pop_front();
}

// ======================================================================== menu

void Engine::openMenu()
{
    if (m_menu.isOpen() || m_menuTools.isEmpty())
        return;
    const QJsonObject ms = obj(settings(), "menu");
    m_menu.setMaxPerRing(ms.value(QStringLiteral("maxPerRing")).toInt(7));
    m_menu.setWrap(ms.value(QStringLiteral("wrap")).toBool(true));
    m_menu.open(m_menuTools, m_activeToolId);
    RotationConfig rc = RotationConfig::fromJson(obj(settings(), "rotation"));
    rc.mode = RotationConfig::Mode::Stepped;
    rc.detentDeg = qBound(5.0, ms.value(QStringLiteral("detentDegrees")).toDouble(20), 90.0);
    rc.sensitivity = 1.0;
    rc.acceleration = 0;
    m_menuRotation.setConfig(rc);
    m_menuRotation.reset();
    GestureConfig g = m_gestures.config();
    g.maxClickCount = 1; // menu selection must be immediate
    m_gestures.setConfig(g);
    const int timeout = ms.value(QStringLiteral("timeoutMs")).toInt(8000);
    if (timeout > 0)
        m_menuTimeout.start(timeout);
    applyHardwareMode();
    logEvent({{QStringLiteral("kind"), QStringLiteral("menu-open")}});
    publishMenu();
}

void Engine::closeMenu(const QString &reason)
{
    if (!m_menu.isOpen())
        return;
    m_menu.close();
    m_menuHeldOpen = false;
    m_menuTimeout.stop();
    GestureConfig g = m_gestures.config();
    g.maxClickCount = clickCountNeeded();
    m_gestures.setConfig(g);
    m_rotation.reset();
    applyHardwareMode();
    logEvent({{QStringLiteral("kind"), QStringLiteral("menu-close")}, {QStringLiteral("reason"), reason}});
    Q_EMIT menuChanged(QJsonObject{{QStringLiteral("open"), false}});
    scheduleStateChanged();
}

void Engine::menuRotate(double degrees, qint64 now)
{
    const auto out = m_menuRotation.process(degrees, now);
    if (!out.steps)
        return;
    bool boundary = false;
    const bool changed = m_menu.rotate(out.steps, &boundary);
    const QJsonObject hs = obj(settings(), "haptics");
    if (boundary && hs.value(QStringLiteral("boundaryFeedback")).toBool(true))
        haptic(hid::Waveform::BuzzContinuous, 1);
    if (changed) {
        const int timeout = obj(settings(), "menu").value(QStringLiteral("timeoutMs")).toInt(8000);
        if (timeout > 0)
            m_menuTimeout.start(timeout);
        publishMenu();
    }
}

void Engine::menuSelect()
{
    const auto r = m_menu.select();
    const QJsonObject hs = obj(settings(), "haptics");
    if (hs.value(QStringLiteral("selectFeedback")).toBool(true))
        haptic(hid::Waveform::Click, 1);
    switch (r.kind) {
    case RadialMenuModel::SelectKind::ActivateTool:
        closeMenu(QStringLiteral("selected"));
        setActiveTool(r.toolId);
        return;
    case RadialMenuModel::SelectKind::EnteredFolder:
    case RadialMenuModel::SelectKind::WentBack:
    case RadialMenuModel::SelectKind::NextPage:
        m_menuRotation.reset();
        publishMenu();
        return;
    case RadialMenuModel::SelectKind::None:
        closeMenu(QStringLiteral("nothing selected"));
        return;
    }
}

QJsonObject Engine::menuJson() const
{
    QJsonObject m = m_menu.toJson();
    m.insert(QStringLiteral("open"), m_menu.isOpen());
    const QJsonObject ms = obj(settings(), "menu");
    m.insert(QStringLiteral("settings"), ms);
    m.insert(QStringLiteral("handedness"), settings().value(QStringLiteral("handedness")).toString(QStringLiteral("right")));
    m.insert(QStringLiteral("profileName"), profileById(m_config.value(QStringLiteral("profiles")).toArray(), m_resolution.profileId)
                                                .value(QStringLiteral("name")).toString());
    if (ms.value(QStringLiteral("placement")).toString(QStringLiteral("pointer")) == QLatin1String("pointer"))
        if (auto p = m_context->pointerPosition())
            m.insert(QStringLiteral("pointer"), QJsonObject{{QStringLiteral("x"), p->x()}, {QStringLiteral("y"), p->y()}});
    return m;
}

void Engine::publishMenu()
{
    Q_EMIT menuChanged(menuJson());
    scheduleStateChanged();
}

void Engine::menuOp(const QString &op)
{
    if (op == QLatin1String("close") || (op == QLatin1String("toggle") && m_menu.isOpen()))
        closeMenu(QStringLiteral("requested"));
    else if (!(m_suppressed && m_suppressReason != QLatin1String("paused")))
        openMenu();
}

// ===================================================================== control

QJsonObject Engine::activeTool() const { return findTool(m_menuTools, m_activeToolId); }

bool Engine::setActiveTool(const QString &id)
{
    const QJsonObject t = findTool(m_menuTools, id);
    if (t.isEmpty() || t.value(QStringLiteral("kind")).toString() == QLatin1String("folder"))
        return false;
    const bool changed = id != m_activeToolId;
    m_activeToolId = id;
    m_activeTools.insert(m_resolution.profileId, id);
    m_rotation.setConfig(RotationConfig::fromJson(t.value(QStringLiteral("rotation")).toObject(), RotationConfig::fromJson(obj(settings(), "rotation"))));
    m_rotation.reset();
    GestureConfig g = m_gestures.config();
    g.maxClickCount = m_menu.isOpen() ? 1 : clickCountNeeded();
    m_gestures.setConfig(g);
    applyHardwareMode();
    if (changed) {
        logEvent({{QStringLiteral("kind"), QStringLiteral("tool")}, {QStringLiteral("id"), id}});
        showOsd(QJsonObject{{QStringLiteral("icon"), t.value(QStringLiteral("icon")).toString(QStringLiteral("custom"))},
                            {QStringLiteral("title"), t.value(QStringLiteral("name")).toString()},
                            {QStringLiteral("text"), t.value(QStringLiteral("description")).toString()},
                            {QStringLiteral("kind"), QStringLiteral("tool")}});
        saveState();
    }
    scheduleStateChanged();
    return true;
}

void Engine::toolOp(const QString &op, const QString &id, int steps)
{
    if (op == QLatin1String("select")) {
        setActiveTool(id);
        return;
    }
    const QJsonArray flat = flattenTools(m_menuTools);
    QStringList ids;
    for (const auto &v : flat)
        if (v.toObject().value(QStringLiteral("kind")).toString() != QLatin1String("folder"))
            ids << v.toObject().value(QStringLiteral("id")).toString();
    if (ids.isEmpty())
        return;
    int i = qMax(0, ids.indexOf(m_activeToolId));
    const int d = (op == QLatin1String("previous") ? -1 : 1) * qMax(1, steps);
    i = ((i + d) % int(ids.size()) + int(ids.size())) % int(ids.size());
    setActiveTool(ids.at(i));
}

void Engine::profileOp(const QString &op, const QString &id)
{
    if (op == QLatin1String("lock"))
        lockProfile(id.isEmpty() ? m_resolution.profileId : id);
    else if (op == QLatin1String("unlock"))
        unlockProfile();
    else {
        const QJsonArray profiles = m_config.value(QStringLiteral("profiles")).toArray();
        QStringList ids;
        for (const auto &p : profiles)
            if (p.toObject().value(QStringLiteral("enabled")).toBool(true))
                ids << p.toObject().value(QStringLiteral("id")).toString();
        if (ids.isEmpty())
            return;
        const int i = (ids.indexOf(m_resolution.profileId) + 1) % int(ids.size());
        lockProfile(ids.at(i));
    }
}

bool Engine::lockProfile(const QString &id)
{
    if (profileById(m_config.value(QStringLiteral("profiles")).toArray(), id).isEmpty())
        return false;
    m_lockedProfile = id;
    saveState();
    resolveContext(true);
    showOsd(QJsonObject{{QStringLiteral("icon"), QStringLiteral("lock")},
                        {QStringLiteral("title"), tr("Profile locked")},
                        {QStringLiteral("text"), profileById(m_config.value(QStringLiteral("profiles")).toArray(), id).value(QStringLiteral("name")).toString()}});
    return true;
}

void Engine::unlockProfile()
{
    m_lockedProfile.clear();
    saveState();
    resolveContext(true);
}

void Engine::haptic(hid::Waveform w, int repeat)
{
    if (!obj(settings(), "haptics").value(QStringLiteral("enabled")).toBool(true))
        return;
    m_device->buzz(w, qBound(1, repeat, 10));
}

bool Engine::testHaptics(const QString &which)
{
    if (!m_device->hapticsAvailable())
        return false;
    m_device->buzz(which == QLatin1String("buzz") ? hid::Waveform::BuzzContinuous
                   : which == QLatin1String("rumble") ? hid::Waveform::RumbleContinuous
                                                       : hid::Waveform::Click,
                   1);
    return true;
}

void Engine::setPaused(bool p)
{
    m_paused = p;
    saveState();
    updateSuppression();
    m_notifier->notify(p ? tr("OpenDial paused") : tr("OpenDial resumed"), p ? tr("The Surface Dial will not send any input.") : QString());
}

void Engine::completeFirstRun()
{
    if (!m_firstRun)
        return;
    m_firstRun = false;
    saveState();
    scheduleStateChanged();
}

void Engine::reconnect()
{
    m_device->forceReconnect();
}

void Engine::requestPortalPermission() { m_inj->requestPortalPermission(); }

void Engine::setRawStreaming(bool on) { m_device->setRawStreaming(on); }

QJsonObject Engine::bluetooth(const QString &action, const QString &path)
{
    const QString p = path.isEmpty() ? m_bluez->primaryPath() : path;
    if (action == QLatin1String("status"))
        return m_bluez->toJson(false);
    if (action == QLatin1String("discover")) m_bluez->startDiscovery();
    else if (action == QLatin1String("stop-discovery")) m_bluez->stopDiscovery();
    else if (action == QLatin1String("pair")) m_bluez->pair(p);
    else if (action == QLatin1String("connect")) m_bluez->connectDevice(p);
    else if (action == QLatin1String("disconnect")) m_bluez->disconnectDevice(p);
    else if (action == QLatin1String("trust")) m_bluez->trust(p);
    else if (action == QLatin1String("remove")) m_bluez->remove(p);
    else
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("unknown action")}};
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("pending"), true}};
}

// ================================================================== simulation

bool Engine::simulate(const QJsonObject &cmd, QString *error)
{
    auto fail = [&](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    if (cmd.contains(QStringLiteral("sequence"))) {
        const QJsonArray seq = cmd.value(QStringLiteral("sequence")).toArray();
        if (seq.size() > 500)
            return fail(QStringLiteral("sequence too long"));
        int at = 0;
        for (const auto &v : seq) {
            const QJsonObject s = v.toObject();
            if (s.contains(QStringLiteral("wait"))) {
                at += qBound(0, s.value(QStringLiteral("wait")).toInt(), 10000);
                continue;
            }
            QTimer::singleShot(at, this, [this, s] { simulate(s, nullptr); });
        }
        return true;
    }
    if (cmd.contains(QStringLiteral("button"))) {
        const QString b = cmd.value(QStringLiteral("button")).toString();
        if (b != QLatin1String("down") && b != QLatin1String("up"))
            return fail(QStringLiteral("button must be down|up"));
        logEvent({{QStringLiteral("kind"), QStringLiteral("simulated")}, {QStringLiteral("button"), b}});
        feedButton(b == QLatin1String("down"), m_clock.elapsed());
        return true;
    }
    if (cmd.contains(QStringLiteral("rotate"))) {
        const double deg = cmd.value(QStringLiteral("rotate")).toDouble();
        if (!std::isfinite(deg) || std::abs(deg) > 3600)
            return fail(QStringLiteral("rotate out of range"));
        // Deliver as the hardware would: slices of at most one menu detent.
        const int slices = qBound(1, int(std::ceil(std::abs(deg) / 5.0)), 720);
        for (int i = 0; i < slices; ++i)
            feedRotation(deg / slices, m_clock.elapsed());
        return true;
    }
    if (cmd.contains(QStringLiteral("click"))) {
        const int n = qBound(1, cmd.value(QStringLiteral("click")).toInt(1), 3);
        QJsonArray seq;
        for (int i = 0; i < n; ++i) {
            seq.append(QJsonObject{{QStringLiteral("button"), QStringLiteral("down")}});
            seq.append(QJsonObject{{QStringLiteral("wait"), 40}});
            seq.append(QJsonObject{{QStringLiteral("button"), QStringLiteral("up")}});
            seq.append(QJsonObject{{QStringLiteral("wait"), 60}});
        }
        return simulate(QJsonObject{{QStringLiteral("sequence"), seq}}, error);
    }
    if (cmd.contains(QStringLiteral("longPress"))) {
        const int hold = m_gestures.config().longPressMs + 60;
        return simulate(QJsonObject{{QStringLiteral("sequence"), QJsonArray{QJsonObject{{QStringLiteral("button"), QStringLiteral("down")}},
                                                                             QJsonObject{{QStringLiteral("wait"), hold}},
                                                                             QJsonObject{{QStringLiteral("button"), QStringLiteral("up")}}}}},
                        error);
    }
    return fail(QStringLiteral("expected button|rotate|click|longPress|sequence"));
}

void Engine::reportFocus(const QJsonObject &payload)
{
    if (auto *b = m_context->bridge())
        b->report(payload);
}

bool Engine::simulateContext(const QJsonObject &ctx)
{
    if (ctx.isEmpty())
        m_context->simulate(std::nullopt);
    else
        m_context->simulate(FocusContext::fromJson(ctx));
    return true;
}

// ================================================================ configuration

void Engine::loadConfiguration()
{
    m_loaded = loadConfig(*m_store);
    for (const auto &m : std::as_const(m_loaded.messages))
        qCInfo(lcEngine) << m;
    m_config = m_loaded.config;
    m_lastWrittenHash = hashOf(m_config);
    if (m_loaded.fromDefaults && !m_loaded.readOnly && !QFile::exists(m_store->path())) {
        // first run: give users a file to inspect / edit by hand
        QString err;
        if (!m_store->write(m_config, &err))
            qCWarning(lcEngine) << "could not write default configuration:" << err;
    }
    if (m_loaded.fromBackup)
        m_notifier->notify(tr("OpenDial configuration restored"), tr("The configuration file was damaged; the last good backup was loaded."), true);
    if (m_loaded.readOnly)
        m_notifier->notify(tr("OpenDial configuration is from a newer version"),
                           tr("It was not modified. Defaults are in use until you upgrade OpenDial."), true);
    applySettings();
    resolveContext(true);
    Q_EMIT configChanged(m_config);
}

void Engine::applySettings()
{
    const QJsonObject s = settings();
    const QJsonObject g = obj(s, "gestures");
    GestureConfig gc;
    gc.longPressMs = qBound(200, g.value(QStringLiteral("longPressMs")).toInt(500), 3000);
    gc.doubleClickMs = qBound(100, g.value(QStringLiteral("doubleClickMs")).toInt(300), 1000);
    gc.debounceMs = qBound(0, g.value(QStringLiteral("debounceMs")).toInt(8), 100);
    gc.holdRotateThresholdDeg = qBound(0.0, g.value(QStringLiteral("holdRotateThresholdDeg")).toDouble(3), 45.0);
    gc.clickToggleLatch = g.value(QStringLiteral("clickToggleLatch")).toBool(false);
    gc.longPressEnabled = true;
    gc.maxClickCount = m_menu.isOpen() ? 1 : clickCountNeeded();
    m_gestures.setConfig(gc);

    const QJsonObject dev = obj(s, "device");
    m_device->setConnectGuardMs(qBound(0, dev.value(QStringLiteral("connectGuardMs")).toInt(800), 5000));
    m_device->setGrab(dev.value(QStringLiteral("grabDevice")).toBool(true));

    const QJsonObject inj = obj(s, "injection");
    InjectionSettings is;
    is.backend = inj.value(QStringLiteral("backend")).toString(QStringLiteral("auto"));
    is.allowUinput = inj.value(QStringLiteral("allowUinput")).toBool(false);
    is.keyboardLayout = inj.value(QStringLiteral("keyboardLayout")).toString(QStringLiteral("auto"));
    is.portalRestoreToken = m_portalToken;
    if (!m_opts.noInjection)
        m_inj->configure(is);

    QStringList secure;
    for (const auto &v : obj(s, "context").value(QStringLiteral("secureAppIds")).toArray())
        secure << v.toString();
    m_context->setSecureAppIds(secure);
    const QJsonObject n = obj(s, "notifications");
    m_notifier->setEnabled(n.value(QStringLiteral("enabled")).toBool(true), n.value(QStringLiteral("quiet")).toBool(false));
    m_brightness->setDdcEnabled(obj(s, "integrations").value(QStringLiteral("ddcutil")).toBool(false));
    rebuildMenuTools();
}

int Engine::clickCountNeeded() const
{
    const QString og = obj(settings(), "menu").value(QStringLiteral("openGesture")).toString();
    int n = og == QLatin1String("triple_click") ? 3 : og == QLatin1String("double_click") ? 2 : 1;
    if (toolHasSlot(slot::TripleClick))
        n = 3;
    else if (toolHasSlot(slot::DoubleClick))
        n = qMax(n, 2);
    return n;
}

ValidationResult Engine::setConfig(const QJsonObject &cfg, QString *error)
{
    if (m_loaded.readOnly) {
        if (error)
            *error = QStringLiteral("configuration on disk is from a newer OpenDial; refusing to overwrite");
        ValidationResult r;
        r.issues.append({QStringLiteral("config"), *error, false});
        return r;
    }
    auto mig = migrateConfig(cfg);
    if (!mig.ok) {
        ValidationResult r;
        r.issues.append({QStringLiteral("config"), mig.log.join(QStringLiteral("; ")), false});
        return r;
    }
    ValidationResult v = validateConfig(mig.config);
    if (!v.ok())
        return v;
    QString err;
    m_lastWrittenHash = hashOf(mig.config);
    if (!m_store->write(mig.config, &err)) {
        if (error)
            *error = err;
        v.issues.append({QStringLiteral("config"), QStringLiteral("write failed: ") + err, false});
        return v;
    }
    if (!m_watcher.files().contains(m_store->path()))
        m_watcher.addPath(m_store->path());
    m_config = mig.config;
    m_loaded.validation = v;
    applySettings();
    resolveContext(true);
    Q_EMIT configChanged(m_config);
    scheduleStateChanged();
    return v;
}

bool Engine::resetToDefaults(QString *error)
{
    auto r = setConfig(defaultConfig(), error);
    return r.ok();
}

int Engine::rollbackConfig(QString *error)
{
    QJsonObject restored;
    const int idx = m_store->rollback(restored, error);
    if (idx >= 0)
        loadConfiguration();
    return idx;
}

QJsonObject Engine::importProfile(const QJsonObject &doc)
{
    QJsonObject profile;
    QStringList errors;
    QList<QPair<QString, QJsonObject>> risky;
    if (!importProfileEnvelope(doc, profile, &errors, &risky))
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("errors"), QJsonArray::fromStringList(errors)}};
    QJsonObject cfg = m_config;
    QJsonArray profiles = cfg.value(QStringLiteral("profiles")).toArray();
    QString id = profile.value(QStringLiteral("id")).toString();
    // never replace the global profile on import; rename duplicates
    QSet<QString> ids;
    for (const auto &p : profiles)
        ids.insert(p.toObject().value(QStringLiteral("id")).toString());
    if (id == QLatin1String("global") || profile.value(QStringLiteral("match")).toObject().value(QStringLiteral("kind")).toString() == QLatin1String("global")) {
        QJsonObject m = profile.value(QStringLiteral("match")).toObject();
        if (m.value(QStringLiteral("kind")).toString() == QLatin1String("global"))
            m.insert(QStringLiteral("kind"), QStringLiteral("app"));
        profile.insert(QStringLiteral("match"), m);
        id = QStringLiteral("imported-global");
    }
    QString unique = id;
    for (int n = 2; ids.contains(unique); ++n)
        unique = id + QLatin1Char('-') + QString::number(n);
    profile.insert(QStringLiteral("id"), unique);
    profiles.append(profile);
    cfg.insert(QStringLiteral("profiles"), profiles);
    QString err;
    const auto v = setConfig(cfg, &err);
    QJsonArray riskyOut;
    for (const auto &r : risky)
        riskyOut.append(QJsonObject{{QStringLiteral("path"), r.first}, {QStringLiteral("description"), describeAction(r.second)},
                                    {QStringLiteral("fingerprint"), actionFingerprint(r.second)}});
    return QJsonObject{{QStringLiteral("ok"), v.ok()}, {QStringLiteral("id"), unique}, {QStringLiteral("errors"), QJsonArray::fromStringList(v.errors())},
                       {QStringLiteral("warnings"), QJsonArray::fromStringList(v.warnings())}, {QStringLiteral("riskyActions"), riskyOut}};
}

QJsonObject Engine::exportProfile(const QString &id) const
{
    const QJsonObject p = profileById(m_config.value(QStringLiteral("profiles")).toArray(), id);
    return p.isEmpty() ? QJsonObject() : exportProfileEnvelope(p);
}

// ======================================================================= state

void Engine::loadState()
{
    QJsonObject st;
    QString err;
    if (!m_stateStore->read(st, &err) && !err.isEmpty())
        qCWarning(lcEngine) << "state unreadable, starting fresh:" << err;
    m_activeTools = st.value(QStringLiteral("activeTools")).toObject();
    m_lockedProfile = st.value(QStringLiteral("lockedProfile")).toString();
    m_approvals.clear();
    for (const auto &v : st.value(QStringLiteral("approvals")).toArray())
        m_approvals.insert(v.toString());
    m_variables = st.value(QStringLiteral("variables")).toObject();
    m_pluginConsent = st.value(QStringLiteral("pluginConsent")).toObject();
    m_portalToken = st.value(QStringLiteral("portalRestoreToken")).toString();
    m_firstRun = st.value(QStringLiteral("firstRun")).toBool(true);
    m_paused = st.value(QStringLiteral("paused")).toBool(false);
}

void Engine::saveState()
{
    QJsonArray approvals;
    for (const auto &a : std::as_const(m_approvals))
        approvals.append(a);
    QJsonObject st{{QStringLiteral("version"), 1},
                   {QStringLiteral("activeTools"), m_activeTools},
                   {QStringLiteral("lockedProfile"), m_lockedProfile},
                   {QStringLiteral("approvals"), approvals},
                   {QStringLiteral("variables"), m_variables},
                   {QStringLiteral("pluginConsent"), m_pluginConsent},
                   {QStringLiteral("portalRestoreToken"), m_portalToken},
                   {QStringLiteral("firstRun"), m_firstRun},
                   {QStringLiteral("paused"), m_paused}};
    QString err;
    if (!m_stateStore->write(st, &err))
        qCWarning(lcEngine) << "could not save state:" << err;
}

void Engine::setVariable(const QString &name, double v)
{
    m_variables.insert(name, v);
    m_varSaveTimer.start(); // persisted lazily with other state
}

QJsonArray Engine::filterAvailable(const QJsonArray &tools) const
{
    QJsonArray out;
    for (const auto &v : tools) {
        QJsonObject t = v.toObject();
        const QString req = t.value(QStringLiteral("requires")).toString();
        if (req == QLatin1String("brightness") && m_brightness->state() == CapState::NotSupported
            && m_inj->keyboardCapability() != QLatin1String("supported"))
            continue;
        if (t.value(QStringLiteral("kind")).toString() == QLatin1String("folder")) {
            t.insert(QStringLiteral("children"), filterAvailable(t.value(QStringLiteral("children")).toArray()));
            if (t.value(QStringLiteral("children")).toArray().isEmpty())
                continue;
        }
        out.append(t);
    }
    return out;
}

QJsonArray Engine::pluginTools() const
{
    QJsonArray out;
    const auto cands = appIdCandidates(m_focus);
    for (const auto &p : m_plugins) {
        if (!p.approved)
            continue;
        bool match = p.matchAppIds.isEmpty();
        for (const auto &a : p.matchAppIds)
            if (cands.contains(normalizeAppId(a)))
                match = true;
        if (!match)
            continue;
        for (const auto &t : p.tools)
            out.append(t);
    }
    return out;
}

void Engine::resolveContext(bool force)
{
    const QJsonArray profiles = m_config.value(QStringLiteral("profiles")).toArray();
    const bool ctxEnabled = obj(settings(), "context").value(QStringLiteral("enabled")).toBool(true);
    FocusContext ctx = ctxEnabled ? m_focus : FocusContext();
    const Resolution r = resolveProfile(profiles, ctx, m_lockedProfile);
    const bool profileChanged = r.profileId != m_resolution.profileId;
    m_resolution = r;
    if (profileChanged && obj(settings(), "menu").value(QStringLiteral("closeOnFocusChange")).toBool(true))
        closeMenu(QStringLiteral("focus changed"));
    if (profileChanged || force) {
        logEvent({{QStringLiteral("kind"), QStringLiteral("profile")}, {QStringLiteral("id"), r.profileId}, {QStringLiteral("layer"), layerName(r.layer)}});
        rebuildMenuTools();
    } else {
        // plugin tools depend on the focused app even within one profile
        const QJsonArray before = m_menuTools;
        rebuildMenuTools();
        if (before == m_menuTools)
            return;
    }
    scheduleStateChanged();
}

void Engine::rebuildMenuTools()
{
    const QJsonArray profiles = m_config.value(QStringLiteral("profiles")).toArray();
    if (m_resolution.profileId.isEmpty())
        m_resolution = resolveProfile(profiles, m_focus, m_lockedProfile);
    m_menuTools = filterAvailable(enabledTools(composeMenuTools(profiles, m_resolution.profileId, pluginTools())));
    const QJsonObject profile = profileById(profiles, m_resolution.profileId);
    const QString policy = obj(settings(), "context").value(QStringLiteral("toolOnSwitch")).toString(QStringLiteral("remember"));
    QString want = policy == QLatin1String("default") ? QString() : m_activeTools.value(m_resolution.profileId).toString();
    if (want.isEmpty() || findTool(m_menuTools, want).isEmpty())
        want = profile.value(QStringLiteral("defaultTool")).toString();
    if (want.isEmpty() || findTool(m_menuTools, want).isEmpty()) {
        // keep the current tool if this profile offers it (extend mode), else first
        want = findTool(m_menuTools, m_activeToolId).isEmpty() ? QString() : m_activeToolId;
        if (want.isEmpty()) {
            const QJsonArray flat = flattenTools(m_menuTools);
            for (const auto &v : flat)
                if (v.toObject().value(QStringLiteral("kind")).toString() != QLatin1String("folder")) {
                    want = v.toObject().value(QStringLiteral("id")).toString();
                    break;
                }
        }
    }
    if (want.isEmpty()) {
        m_activeToolId.clear();
        return;
    }
    // apply without OSD when the switch is caused by focus, not by the user
    const QJsonObject t = findTool(m_menuTools, want);
    m_activeToolId = want;
    m_rotation.setConfig(RotationConfig::fromJson(t.value(QStringLiteral("rotation")).toObject(), RotationConfig::fromJson(obj(settings(), "rotation"))));
    GestureConfig g = m_gestures.config();
    g.maxClickCount = m_menu.isOpen() ? 1 : clickCountNeeded();
    m_gestures.setConfig(g);
    applyHardwareMode();
    if (m_menu.isOpen())
        publishMenu();
}

void Engine::applyHardwareMode()
{
    const QJsonObject hs = obj(settings(), "haptics");
    HapticsRequest req;
    req.globalEnabled = hs.value(QStringLiteral("enabled")).toBool(true);
    req.minHapticDetentDeg = qBound(1.0, hs.value(QStringLiteral("minDetentDegrees")).toDouble(2), 45.0);
    if (m_menu.isOpen()) {
        req.toolEnabled = hs.value(QStringLiteral("menuTicks")).toBool(true);
        req.detentDeg = obj(settings(), "menu").value(QStringLiteral("detentDegrees")).toDouble(20);
    } else {
        const QJsonObject t = activeTool();
        const RotationConfig rc = m_rotation.config();
        req.toolEnabled = t.value(QStringLiteral("haptics")).toBool(true) && rc.mode == RotationConfig::Mode::Stepped && rc.acceleration == 0;
        // a haptic tick per action step: physical detent = software detent / sensitivity
        req.detentDeg = rc.detentDeg / qMax(0.05, rc.sensitivity);
    }
    m_device->requestMode(hardwareModeFor(req));
}

void Engine::updateSuppression()
{
    QString reason;
    if (m_paused)
        reason = QStringLiteral("paused");
    else if (m_session->locked())
        reason = QStringLiteral("session locked");
    else if (m_focus.secure)
        reason = QStringLiteral("secure input focused");
    const bool s = !reason.isEmpty();
    if (s == m_suppressed && reason == m_suppressReason)
        return;
    m_suppressed = s;
    m_suppressReason = reason;
    m_inj->setSuppressed(s);
    if (s) {
        m_runner->cancelPending();
        closeMenu(reason);
    }
    logEvent({{QStringLiteral("kind"), QStringLiteral("suppression")}, {QStringLiteral("active"), s}, {QStringLiteral("reason"), reason}});
    scheduleStateChanged();
}

void Engine::scheduleStateChanged()
{
    if (!m_stateTimer.isActive())
        m_stateTimer.start();
}

// =================================================================== approvals

void Engine::requestApproval(const QJsonObject &action, const QString &toolId, const QString &profileId)
{
    const QString fp = actionFingerprint(action);
    if (m_pending.contains(fp))
        return;
    m_pending.insert(fp, QJsonObject{{QStringLiteral("fingerprint"), fp},
                                     {QStringLiteral("action"), action},
                                     {QStringLiteral("description"), describeAction(action)},
                                     {QStringLiteral("toolId"), toolId},
                                     {QStringLiteral("profileId"), profileId}});
    m_notifier->notify(tr("Action needs your approval"),
                       tr("“%1” runs a program or contacts another service. Open OpenDial → Approvals to allow it.").arg(describeAction(action)), true);
    Q_EMIT approvalsChanged();
    scheduleStateChanged();
}

QJsonArray Engine::pendingApprovals() const
{
    QJsonArray out;
    // also list risky actions in the config that were never approved
    QList<QPair<QString, QJsonObject>> risky;
    collectRiskyActions(m_config.value(QStringLiteral("profiles")), QStringLiteral("profiles"), risky);
    QSet<QString> seen;
    for (const auto &p : m_pending) {
        out.append(p);
        seen.insert(p.value(QStringLiteral("fingerprint")).toString());
    }
    for (const auto &r : risky) {
        const QString fp = actionFingerprint(r.second);
        if (seen.contains(fp))
            continue;
        seen.insert(fp);
        out.append(QJsonObject{{QStringLiteral("fingerprint"), fp}, {QStringLiteral("action"), r.second},
                               {QStringLiteral("description"), describeAction(r.second)}, {QStringLiteral("path"), r.first},
                               {QStringLiteral("approved"), m_approvals.contains(fp)}});
    }
    return out;
}

bool Engine::approve(const QString &fp)
{
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{64}$"));
    if (!hex.match(fp).hasMatch())
        return false;
    m_approvals.insert(fp);
    m_pending.remove(fp);
    saveState();
    Q_EMIT approvalsChanged();
    scheduleStateChanged();
    return true;
}

bool Engine::revokeApproval(const QString &fp)
{
    const bool r = m_approvals.remove(fp);
    saveState();
    Q_EMIT approvalsChanged();
    scheduleStateChanged();
    return r;
}

// ===================================================================== plugins

QJsonObject Engine::registerApp(const QJsonObject &reg, const QString &busName)
{
    static const QRegularExpression idRe(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"));
    const QString appId = reg.value(QStringLiteral("appId")).toString();
    if (!idRe.match(appId).hasMatch())
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("invalid appId")}};
    if (!obj(settings(), "integrations").value(QStringLiteral("plugins")).toObject().value(QStringLiteral("enabled")).toBool(true))
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("plugins are disabled in settings")}};
    const QJsonArray toolsIn = reg.value(QStringLiteral("tools")).toArray();
    if (toolsIn.size() > 32)
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("too many tools (max 32)")}};
    PluginApp app;
    app.appId = appId;
    app.name = reg.value(QStringLiteral("name")).toString(appId).left(64);
    app.busName = busName;
    for (const auto &v : reg.value(QStringLiteral("matchAppIds")).toArray())
        app.matchAppIds << v.toString().left(128);
    QStringList errors;
    for (const auto &v : toolsIn) {
        QJsonObject t = v.toObject();
        const QString tid = t.value(QStringLiteral("id")).toString();
        if (!idRe.match(tid).hasMatch()) {
            errors << QStringLiteral("invalid tool id");
            continue;
        }
        QJsonObject bindings;
        for (const QString &s : slot::all())
            bindings.insert(s, QJsonObject{{QStringLiteral("type"), QStringLiteral("plugin")}, {QStringLiteral("appId"), appId},
                                           {QStringLiteral("toolId"), tid}, {QStringLiteral("event"), s}});
        QJsonObject tool{{QStringLiteral("id"), QStringLiteral("plugin:%1:%2").arg(appId, tid)},
                         {QStringLiteral("name"), t.value(QStringLiteral("name")).toString(tid).left(64)},
                         {QStringLiteral("icon"), t.value(QStringLiteral("icon")).toString(QStringLiteral("plugin"))},
                         {QStringLiteral("description"), t.value(QStringLiteral("description")).toString().left(200)},
                         {QStringLiteral("enabled"), true},
                         {QStringLiteral("plugin"), appId},
                         {QStringLiteral("haptics"), t.value(QStringLiteral("haptics")).toBool(true)},
                         {QStringLiteral("rotation"), t.value(QStringLiteral("rotation")).toObject()},
                         {QStringLiteral("bindings"), bindings}};
        ValidationIssues issues;
        validateTool(tool, QStringLiteral("plugin"), issues);
        bool bad = false;
        for (const auto &i : issues)
            if (!i.warning) {
                errors << i.message;
                bad = true;
            }
        if (!bad)
            app.tools.append(tool);
    }
    app.approved = m_pluginConsent.value(appId).toBool(false);
    const bool isNew = !m_plugins.contains(appId);
    m_plugins.insert(appId, app);
    if (!app.approved && isNew && !m_pluginConsent.contains(appId))
        m_notifier->notify(tr("%1 wants to add Dial tools").arg(app.name), tr("Open OpenDial → Integrations to allow or deny it."), true);
    resolveContext();
    rebuildMenuTools();
    scheduleStateChanged();
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("approved"), app.approved},
                       {QStringLiteral("tools"), int(app.tools.size())}, {QStringLiteral("errors"), QJsonArray::fromStringList(errors)}};
}

bool Engine::unregisterApp(const QString &appId, const QString &busName)
{
    auto it = m_plugins.find(appId);
    if (it == m_plugins.end() || (!busName.isEmpty() && it->busName != busName))
        return false;
    m_plugins.erase(it);
    rebuildMenuTools();
    scheduleStateChanged();
    return true;
}

void Engine::busNameVanished(const QString &busName)
{
    QStringList gone;
    for (const auto &p : std::as_const(m_plugins))
        if (p.busName == busName)
            gone << p.appId;
    for (const auto &g : gone)
        m_plugins.remove(g);
    if (!gone.isEmpty()) {
        rebuildMenuTools();
        scheduleStateChanged();
    }
}

bool Engine::approvePlugin(const QString &appId, bool approved)
{
    m_pluginConsent.insert(appId, approved);
    if (m_plugins.contains(appId))
        m_plugins[appId].approved = approved;
    saveState();
    rebuildMenuTools();
    scheduleStateChanged();
    return true;
}

bool Engine::emitPluginEvent(const QString &appId, const QJsonObject &ev)
{
    const auto it = m_plugins.constFind(appId);
    if (it == m_plugins.constEnd() || !it->approved)
        return false;
    Q_EMIT pluginEvent(appId, ev);
    return true;
}

bool Engine::pluginFeedback(const QString &appId, const QJsonObject &fb, const QString &busName)
{
    const auto it = m_plugins.constFind(appId);
    if (it == m_plugins.constEnd() || !it->approved || it->busName != busName)
        return false;
    if (fb.contains(QStringLiteral("osd"))) {
        QJsonObject o = fb.value(QStringLiteral("osd")).toObject();
        o.insert(QStringLiteral("title"), o.value(QStringLiteral("title")).toString().left(64));
        o.insert(QStringLiteral("text"), o.value(QStringLiteral("text")).toString().left(120));
        showOsd(o);
    }
    if (fb.contains(QStringLiteral("haptic"))) {
        const QString w = fb.value(QStringLiteral("haptic")).toString();
        haptic(w == QLatin1String("buzz") ? hid::Waveform::BuzzContinuous : hid::Waveform::Click, 1);
    }
    return true;
}

// ================================================================ presentation

void Engine::showOsd(const QJsonObject &osd)
{
    if (!obj(settings(), "osd").value(QStringLiteral("enabled")).toBool(true))
        return;
    if (m_menu.isOpen() && osd.value(QStringLiteral("kind")).toString() != QLatin1String("tool"))
        return;
    QJsonObject o = osd;
    o.insert(QStringLiteral("durationMs"), obj(settings(), "osd").value(QStringLiteral("durationMs")).toInt(1200));
    o.insert(QStringLiteral("position"), obj(settings(), "osd").value(QStringLiteral("position")).toString(QStringLiteral("bottom")));
    Q_EMIT osdRequested(o);
}

QJsonObject Engine::stateJson() const
{
    const QJsonObject tool = activeTool();
    const HardwareMode applied = m_device->appliedMode();
    const HardwareMode requested = m_device->requestedMode();
    QJsonObject dev = m_device->info().toJson(true);
    dev.insert(QStringLiteral("connected"), m_device->isConnected());
    auto battery = m_device->batteryPercent();
    if (!battery)
        battery = m_bluez->battery();
    dev.insert(QStringLiteral("battery"), battery ? QJsonValue(*battery) : QJsonValue(QStringLiteral("not reported")));
    QString hapticsError = m_device->hapticsError();
    if (hapticsError.isEmpty() && !m_device->hapticsAvailable())
        hapticsError = m_device->isConnected() ? QStringLiteral("hidraw node not writable") : QStringLiteral("Dial not connected");
    QJsonObject haptics{{QStringLiteral("available"), m_device->hapticsAvailable()},
                        {QStringLiteral("error"), hapticsError},
                        {QStringLiteral("appliedResolution"), applied.resolutionSteps},
                        {QStringLiteral("appliedAuto"), applied.autoHaptics},
                        {QStringLiteral("requestedResolution"), requested.resolutionSteps},
                        {QStringLiteral("requestedAuto"), requested.autoHaptics},
                        {QStringLiteral("fineClamped"), requested.fineHapticsClamped}};
    QJsonArray plugins;
    for (const auto &p : m_plugins)
        plugins.append(QJsonObject{{QStringLiteral("appId"), p.appId}, {QStringLiteral("name"), p.name},
                                   {QStringLiteral("approved"), p.approved}, {QStringLiteral("tools"), int(p.tools.size())},
                                   {QStringLiteral("matchAppIds"), QJsonArray::fromStringList(p.matchAppIds)}});
    for (auto it = m_pluginConsent.begin(); it != m_pluginConsent.end(); ++it)
        if (!m_plugins.contains(it.key()))
            plugins.append(QJsonObject{{QStringLiteral("appId"), it.key()}, {QStringLiteral("approved"), it.value().toBool()},
                                       {QStringLiteral("registered"), false}});
    return QJsonObject{
        {QStringLiteral("version"), QStringLiteral(OPENDIAL_VERSION)},
        {QStringLiteral("device"), dev},
        {QStringLiteral("haptics"), haptics},
        {QStringLiteral("profile"), m_resolution.toJson()},
        {QStringLiteral("profileName"), profileById(m_config.value(QStringLiteral("profiles")).toArray(), m_resolution.profileId)
                                            .value(QStringLiteral("name")).toString()},
        {QStringLiteral("lockedProfile"), m_lockedProfile},
        {QStringLiteral("activeTool"), m_activeToolId},
        {QStringLiteral("activeToolName"), tool.value(QStringLiteral("name")).toString()},
        {QStringLiteral("activeToolIcon"), tool.value(QStringLiteral("icon")).toString()},
        {QStringLiteral("menuTools"), m_menuTools},
        {QStringLiteral("menuOpen"), m_menu.isOpen()},
        {QStringLiteral("focus"), m_focus.toJson(true)},
        {QStringLiteral("contextSimulated"), m_context->isSimulated()},
        {QStringLiteral("paused"), m_paused},
        {QStringLiteral("dryRun"), m_opts.noInjection},
        {QStringLiteral("suppressed"), m_suppressed},
        {QStringLiteral("suppressReason"), m_suppressReason},
        {QStringLiteral("session"), m_session->toJson()},
        {QStringLiteral("capabilities"), QJsonObject{
             {QStringLiteral("injection"), m_inj->toJson()},
             {QStringLiteral("context"), m_context->toJson(true)},
             {QStringLiteral("audio"), m_audio->toJson()},
             {QStringLiteral("media"), m_media->toJson()},
             {QStringLiteral("brightness"), m_brightness->toJson()},
             {QStringLiteral("workspace"), QJsonObject{{QStringLiteral("method"), m_workspace->method()},
                                                       {QStringLiteral("state"), capStateName(m_workspace->state())}}},
             {QStringLiteral("bluetooth"), QJsonObject{{QStringLiteral("available"), m_bluez->available()}}}}},
        {QStringLiteral("bluetooth"), m_bluez->toJson(true)},
        {QStringLiteral("pendingApprovals"), int(m_pending.size())},
        {QStringLiteral("plugins"), plugins},
        {QStringLiteral("configPath"), m_store->path()},
        {QStringLiteral("configReadOnly"), m_loaded.readOnly},
        {QStringLiteral("configWarnings"), QJsonArray::fromStringList(m_loaded.validation.warnings())},
        {QStringLiteral("firstRun"), m_firstRun},
        {QStringLiteral("lastGesture"), m_lastGesture},
        {QStringLiteral("lastAction"), m_lastActionResult},
        {QStringLiteral("lastActionLatencyUs"), double(m_lastActionLatencyUs)},
        {QStringLiteral("variables"), m_variables},
    };
}

QJsonObject Engine::diagnostics(bool redact) const
{
    QJsonObject d = stateJson();
    QJsonObject dev = m_device->info().toJson(redact);
    dev.insert(QStringLiteral("connected"), m_device->isConnected());
    dev.insert(QStringLiteral("guardDropped"), m_device->guardDropped());
    dev.insert(QStringLiteral("malformed"), m_device->malformed());
    dev.insert(QStringLiteral("signalsReceived"), m_signalsReceived);
    dev.insert(QStringLiteral("unitsToDegrees"), unitsToDegrees());
    d.insert(QStringLiteral("device"), dev);
    d.insert(QStringLiteral("nodes"), m_device->scan());
    d.insert(QStringLiteral("bluetooth"), m_bluez->toJson(redact));
    d.insert(QStringLiteral("focus"), m_focus.toJson(redact));
    QJsonArray log;
    for (const auto &e : m_log)
        log.append(e);
    d.insert(QStringLiteral("eventLog"), log);
    d.insert(QStringLiteral("configValidation"), m_loaded.validation.toJson());
    d.insert(QStringLiteral("configMessages"), QJsonArray::fromStringList(m_loaded.messages));
    d.insert(QStringLiteral("environment"), QJsonObject{
        {QStringLiteral("XDG_CURRENT_DESKTOP"), qEnvironmentVariable("XDG_CURRENT_DESKTOP")},
        {QStringLiteral("XDG_SESSION_TYPE"), qEnvironmentVariable("XDG_SESSION_TYPE")},
        {QStringLiteral("WAYLAND_DISPLAY"), qEnvironmentVariableIsSet("WAYLAND_DISPLAY")},
        {QStringLiteral("DISPLAY"), qEnvironmentVariableIsSet("DISPLAY")},
        {QStringLiteral("qtPlatform"), QGuiApplication::platformName()},
        {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
        {QStringLiteral("kernel"), QSysInfo::kernelVersion()},
        {QStringLiteral("os"), QSysInfo::prettyProductName()},
    });
    d.insert(QStringLiteral("approvals"), pendingApprovals());
    d.insert(QStringLiteral("redacted"), redact);
    d.remove(QStringLiteral("menuTools"));
    if (redact)
        d.remove(QStringLiteral("variables"));
    return d;
}

} // namespace opendial
