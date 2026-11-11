// SPDX-License-Identifier: MIT
#include "Backend.h"
#include "Action.h"
#include "Config.h"
#include "IconProvider.h"
#include "KeyChord.h"
#include "Keymap.h"
#include "Profiles.h"

#include <QClipboard>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <xkbcommon/xkbcommon.h>

namespace opendial {

namespace {
const QString kSvc = QStringLiteral("io.github.opendial");
const QString kPath = QStringLiteral("/io/github/opendial/Daemon");
const QString kIface = QStringLiteral("io.github.opendial.Daemon1");

QJsonValue parseJson(const QString &s)
{
    const auto d = QJsonDocument::fromJson(s.toUtf8());
    return d.isObject() ? QJsonValue(d.object()) : d.isArray() ? QJsonValue(d.array()) : QJsonValue();
}
QString compact(const QJsonObject &o) { return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)); }

QStringList splitPath(const QString &p) { return p.isEmpty() ? QStringList() : p.split(QLatin1Char('.')); }

QJsonValue getAt(const QJsonValue &root, const QStringList &parts, int i = 0)
{
    if (i >= parts.size())
        return root;
    if (root.isArray()) {
        bool ok = false;
        const int idx = parts[i].toInt(&ok);
        const QJsonArray a = root.toArray();
        return ok && idx >= 0 && idx < a.size() ? getAt(a.at(idx), parts, i + 1) : QJsonValue();
    }
    return getAt(root.toObject().value(parts[i]), parts, i + 1);
}

// op: 0 set, 1 remove, 2 insert(index from value pair), 3 move
QJsonValue modify(const QJsonValue &node, const QStringList &parts, int i, const std::function<QJsonValue(const QJsonValue &)> &leaf)
{
    if (i >= parts.size())
        return leaf(node);
    if (node.isArray()) {
        QJsonArray a = node.toArray();
        bool ok = false;
        const int idx = parts[i].toInt(&ok);
        if (!ok || idx < 0 || idx >= a.size())
            return node;
        a[idx] = modify(a.at(idx), parts, i + 1, leaf);
        return a;
    }
    QJsonObject o = node.toObject();
    o.insert(parts[i], modify(o.value(parts[i]), parts, i + 1, leaf));
    return o;
}
} // namespace

Backend::Backend(QObject *parent) : QObject(parent)
{
    initIconResources();
    m_apply.setSingleShot(true);
    m_apply.setInterval(450);
    connect(&m_apply, &QTimer::timeout, this, &Backend::applyNow);
    m_watcher.setConnection(QDBusConnection::sessionBus());
    m_watcher.addWatchedService(kSvc);
    connect(&m_watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] { connectDaemon(); });
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        m_running = false;
        Q_EMIT daemonRunningChanged();
    });
    auto bus = QDBusConnection::sessionBus();
    bus.connect(kSvc, kPath, kIface, QStringLiteral("StateChanged"), this, SLOT(onState(QString)));
    bus.connect(kSvc, kPath, kIface, QStringLiteral("ConfigChanged"), this, SLOT(onConfig(QString)));
    bus.connect(kSvc, kPath, kIface, QStringLiteral("GestureEvent"), this, SLOT(onGesture(QString)));
    bus.connect(kSvc, kPath, kIface, QStringLiteral("RawEvent"), this, SLOT(onRaw(QString)));
    connectDaemon();
    if (!m_running) {
        // offline editing of the file is still possible
        JsonStore store(configDir() + QStringLiteral("/config.json"));
        m_config = loadConfig(store).config;
        m_applied = m_config;
    }
}

QString Backend::version() const { return QStringLiteral(OPENDIAL_VERSION); }
QStringList Backend::icons() const { return IconProvider::builtinNames(); }

void Backend::connectDaemon()
{
    auto *bus = QDBusConnection::sessionBus().interface();
    m_running = bus && bus->isServiceRegistered(kSvc).value();
    Q_EMIT daemonRunningChanged();
    if (m_running)
        refresh();
}

void Backend::refresh()
{
    onState(callString("GetState"));
    const QString c = callString("GetConfig");
    if (!c.isEmpty()) {
        m_config = parseJson(c).toObject();
        m_applied = m_config;
        m_dirty = false;
        m_errors.clear();
        m_warnings = QJsonValue::fromVariant(m_state.value(QStringLiteral("configWarnings")).toVariant()).toVariant().toStringList();
        Q_EMIT configChanged();
        Q_EMIT validationChanged();
    }
}

QString Backend::callString(const char *method, const QVariantList &args) const
{
    QDBusMessage m = QDBusMessage::createMethodCall(kSvc, kPath, kIface, QString::fromLatin1(method));
    m.setArguments(args);
    const QDBusMessage r = QDBusConnection::sessionBus().call(m, QDBus::Block, 5000);
    return r.type() == QDBusMessage::ReplyMessage && !r.arguments().isEmpty() ? r.arguments().first().toString() : QString();
}

bool Backend::callBool(const char *method, const QVariantList &args) const
{
    QDBusMessage m = QDBusMessage::createMethodCall(kSvc, kPath, kIface, QString::fromLatin1(method));
    m.setArguments(args);
    const QDBusMessage r = QDBusConnection::sessionBus().call(m, QDBus::Block, 5000);
    if (r.type() != QDBusMessage::ReplyMessage)
        return false;
    return r.arguments().isEmpty() ? true : r.arguments().first().toBool();
}

void Backend::onState(const QString &json)
{
    const auto v = parseJson(json);
    if (!v.isObject())
        return;
    m_state = v.toObject();
    Q_EMIT stateChanged();
}

void Backend::onConfig(const QString &json)
{
    if (m_dirty)
        return; // keep the user's edits; they win when applied
    const auto v = parseJson(json);
    if (!v.isObject())
        return;
    m_config = v.toObject();
    m_applied = m_config;
    Q_EMIT configChanged();
}

void Backend::appendEvent(const QVariantMap &e)
{
    m_events.prepend(e);
    while (m_events.size() > 300)
        m_events.removeLast();
    Q_EMIT eventsChanged();
}

void Backend::onGesture(const QString &json)
{
    QVariantMap e = parseJson(json).toObject().toVariantMap();
    e.insert(QStringLiteral("source"), QStringLiteral("gesture"));
    appendEvent(e);
    Q_EMIT gesture(e);
}

void Backend::onRaw(const QString &json)
{
    QVariantMap e = parseJson(json).toObject().toVariantMap();
    e.insert(QStringLiteral("source"), QStringLiteral("raw"));
    appendEvent(e);
}

void Backend::clearEvents()
{
    m_events.clear();
    Q_EMIT eventsChanged();
}

// ------------------------------------------------------------------ editing

QVariant Backend::get(const QString &path) const { return getAt(m_config, splitPath(path)).toVariant(); }

void Backend::set(const QString &path, const QVariant &value)
{
    const QJsonValue nv = QJsonValue::fromVariant(value);
    m_config = modify(m_config, splitPath(path), 0, [&](const QJsonValue &) { return nv; }).toObject();
    Q_EMIT configChanged();
    scheduleApply();
}

void Backend::remove(const QString &path)
{
    QStringList parts = splitPath(path);
    if (parts.isEmpty())
        return;
    const QString last = parts.takeLast();
    m_config = modify(m_config, parts, 0, [&](const QJsonValue &n) -> QJsonValue {
                   if (n.isArray()) {
                       QJsonArray a = n.toArray();
                       const int i = last.toInt();
                       if (i >= 0 && i < a.size())
                           a.removeAt(i);
                       return a;
                   }
                   QJsonObject o = n.toObject();
                   o.remove(last);
                   return o;
               }).toObject();
    Q_EMIT configChanged();
    scheduleApply();
}

void Backend::insert(const QString &arrayPath, int index, const QVariant &value)
{
    const QJsonValue nv = QJsonValue::fromVariant(value);
    m_config = modify(m_config, splitPath(arrayPath), 0, [&](const QJsonValue &n) -> QJsonValue {
                   QJsonArray a = n.toArray();
                   a.insert(qBound(0, index < 0 ? int(a.size()) : index, int(a.size())), nv);
                   return a;
               }).toObject();
    Q_EMIT configChanged();
    scheduleApply();
}

void Backend::move(const QString &arrayPath, int from, int to)
{
    m_config = modify(m_config, splitPath(arrayPath), 0, [&](const QJsonValue &n) -> QJsonValue {
                   QJsonArray a = n.toArray();
                   if (from < 0 || from >= a.size() || to < 0 || to >= a.size())
                       return a;
                   const QJsonValue v = a.takeAt(from);
                   a.insert(to, v);
                   return a;
               }).toObject();
    Q_EMIT configChanged();
    scheduleApply();
}

void Backend::scheduleApply()
{
    m_dirty = true;
    // validate immediately for inline feedback, apply after the debounce
    const auto v = validateConfig(m_config);
    m_errors = v.errors();
    m_warnings = v.warnings();
    Q_EMIT validationChanged();
    if (v.ok())
        m_apply.start();
    else
        m_apply.stop();
}

void Backend::applyNow()
{
    if (!m_running) {
        // daemon not running: write the file directly (same validation)
        const auto v = validateConfig(m_config);
        if (!v.ok())
            return;
        JsonStore store(configDir() + QStringLiteral("/config.json"));
        QString err;
        if (store.write(m_config, &err)) {
            m_applied = m_config;
            m_dirty = false;
            setMessage(tr("Saved. Start the OpenDial service to use it."));
        } else {
            setMessage(tr("Could not save: %1").arg(err));
        }
        Q_EMIT validationChanged();
        return;
    }
    const QJsonObject r = parseJson(callString("SetConfig", {compact(m_config)})).toObject();
    m_errors.clear();
    m_warnings.clear();
    for (const auto &e : r.value(QStringLiteral("errors")).toArray())
        m_errors << e.toString();
    for (const auto &w : r.value(QStringLiteral("warnings")).toArray())
        m_warnings << w.toString();
    if (r.value(QStringLiteral("ok")).toBool()) {
        m_applied = m_config;
        m_dirty = false;
    }
    Q_EMIT validationChanged();
}

void Backend::revert()
{
    m_apply.stop();
    m_config = m_applied;
    m_dirty = false;
    m_errors.clear();
    Q_EMIT configChanged();
    Q_EMIT validationChanged();
}

void Backend::resetDefaults()
{
    m_apply.stop();
    if (m_running) {
        callString("ResetToDefaults");
        m_dirty = false;
        refresh();
    } else {
        m_config = defaultConfig();
        scheduleApply();
    }
    setMessage(tr("Defaults restored."));
}

void Backend::rollback()
{
    const QJsonObject r = parseJson(callString("RollbackConfig")).toObject();
    m_dirty = false;
    refresh();
    setMessage(r.value(QStringLiteral("ok")).toBool() ? tr("Restored the previous configuration.") : tr("No backup to restore."));
}

void Backend::setMessage(const QString &m)
{
    m_message = m;
    Q_EMIT messageChanged();
}

// ------------------------------------------------------------------ helpers

QString Backend::describeAction(const QVariantMap &a) const { return opendial::describeAction(QJsonObject::fromVariantMap(a)); }

QString Backend::validateKeys(const QString &keys) const
{
    const auto r = parseKeySequence(keys);
    return r.ok() ? QString() : (r.error.isEmpty() ? tr("empty shortcut") : r.error);
}

QString Backend::chordFromKeyEvent(int key, int modifiers, quint32 scanCode) const
{
    // modifier-only presses are not chords
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_AltGr
        || key == Qt::Key_Super_L || key == Qt::Key_Super_R)
        return {};
    QString name;
    if (scanCode > 8) {
        // unshifted keysym of the physical key in the session layout
        static Keymap km;
        static bool loaded = km.load(detectLayout(m_config.value(QStringLiteral("settings")).toObject()
                                                      .value(QStringLiteral("injection")).toObject()
                                                      .value(QStringLiteral("keyboardLayout")).toString(QStringLiteral("auto"))));
        if (loaded) {
            xkb_state *st = xkb_state_new(km.raw());
            const xkb_keysym_t sym = xkb_state_key_get_one_sym(st, scanCode);
            xkb_state_unref(st);
            if (sym != XKB_KEY_NoSymbol)
                name = keysymName(sym);
        }
    }
    if (name.isEmpty()) {
        name = QKeySequence(key).toString(QKeySequence::PortableText);
        if (name.isEmpty())
            return {};
    }
    QStringList parts;
    if (modifiers & Qt::ControlModifier) parts << QStringLiteral("Ctrl");
    if (modifiers & Qt::AltModifier) parts << QStringLiteral("Alt");
    if (modifiers & Qt::ShiftModifier) parts << QStringLiteral("Shift");
    if (modifiers & Qt::MetaModifier) parts << QStringLiteral("Super");
    parts << (name.size() == 1 ? name.toUpper() : name);
    const auto r = parseKeySequence(parts.join(QLatin1Char('+')));
    return r.ok() ? r.sequence.chords.first().toString() : parts.join(QLatin1Char('+'));
}

QVariantList Backend::builtinTools() const
{
    QVariantList out;
    const auto g = defaultConfig().value(QStringLiteral("profiles")).toArray().first().toObject();
    for (const auto &t : flattenTools(g.value(QStringLiteral("tools")).toArray()))
        if (t.toObject().value(QStringLiteral("kind")).toString() != QLatin1String("folder"))
            out << t.toObject().toVariantMap();
    // application-tuned tools from the shipped app profiles
    for (const auto &p : defaultConfig().value(QStringLiteral("profiles")).toArray())
        if (p.toObject().value(QStringLiteral("id")).toString() != QLatin1String("global"))
            for (const auto &t : p.toObject().value(QStringLiteral("tools")).toArray()) {
                QVariantMap m = t.toObject().toVariantMap();
                m.insert(QStringLiteral("catalogGroup"), p.toObject().value(QStringLiteral("name")).toString());
                out << m;
            }
    return out;
}

QString Backend::uniqueId(const QString &base) const
{
    QString b = base.toLower();
    b.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    b.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (b.isEmpty())
        b = QStringLiteral("item");
    QSet<QString> ids;
    for (const auto &p : m_config.value(QStringLiteral("profiles")).toArray()) {
        ids.insert(p.toObject().value(QStringLiteral("id")).toString());
        for (const auto &t : flattenTools(p.toObject().value(QStringLiteral("tools")).toArray()))
            ids.insert(t.toObject().value(QStringLiteral("id")).toString());
    }
    QString id = b;
    for (int n = 2; ids.contains(id); ++n)
        id = b + QLatin1Char('-') + QString::number(n);
    return id;
}

QVariantMap Backend::newTool(const QString &name) const
{
    return QJsonObject{{QStringLiteral("id"), uniqueId(name)},
                       {QStringLiteral("name"), name},
                       {QStringLiteral("icon"), QStringLiteral("custom")},
                       {QStringLiteral("enabled"), true},
                       {QStringLiteral("haptics"), true},
                       {QStringLiteral("rotation"), QJsonObject{{QStringLiteral("detentDegrees"), 10}}},
                       {QStringLiteral("bindings"), QJsonObject{{QStringLiteral("rotate_cw"), QJsonObject{{QStringLiteral("type"), QStringLiteral("key")}, {QStringLiteral("keys"), QStringLiteral("Right")}}},
                                                                {QStringLiteral("rotate_ccw"), QJsonObject{{QStringLiteral("type"), QStringLiteral("key")}, {QStringLiteral("keys"), QStringLiteral("Left")}}}}}}
        .toVariantMap();
}

QVariantMap Backend::newProfile(const QString &name, const QString &appId) const
{
    QJsonArray ids;
    if (!appId.isEmpty())
        ids.append(appId);
    return QJsonObject{{QStringLiteral("id"), uniqueId(name)},
                       {QStringLiteral("name"), name},
                       {QStringLiteral("enabled"), true},
                       {QStringLiteral("match"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("app")}, {QStringLiteral("appIds"), ids}}},
                       {QStringLiteral("menuMode"), QStringLiteral("extend")},
                       {QStringLiteral("tools"), QJsonArray()}}
        .toVariantMap();
}

int Backend::profileIndex(const QString &id) const
{
    const auto arr = m_config.value(QStringLiteral("profiles")).toArray();
    for (int i = 0; i < arr.size(); ++i)
        if (arr.at(i).toObject().value(QStringLiteral("id")).toString() == id)
            return i;
    return -1;
}

QVariantList Backend::installedApps() const
{
    QVariantList out;
    QSet<QString> seen;
    for (const auto &dir : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        QDirIterator it(dir, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString id = it.fileInfo().completeBaseName();
            if (seen.contains(id))
                continue;
            seen.insert(id);
            QSettings d(path, QSettings::IniFormat);
            d.beginGroup(QStringLiteral("Desktop Entry"));
            if (d.value(QStringLiteral("NoDisplay")).toString() == QLatin1String("true") || d.value(QStringLiteral("Type")).toString() != QLatin1String("Application"))
                continue;
            const QString name = d.value(QStringLiteral("Name")).toString();
            if (name.isEmpty())
                continue;
            out << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
                               {QStringLiteral("wmClass"), d.value(QStringLiteral("StartupWMClass")).toString()}};
        }
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().compare(b.toMap().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
    });
    return out;
}

QStringList Backend::actionTypes() const { return opendial::actionTypes(); }

QString Backend::riskOf(const QVariantMap &a) const
{
    return actionRisk(QJsonObject::fromVariantMap(a)) == Risk::NeedsApproval ? QStringLiteral("approval") : QStringLiteral("safe");
}

// ------------------------------------------------------------------ control

void Backend::startDaemon()
{
    if (!QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("start"), QStringLiteral("opendial.service")}))
        QProcess::startDetached(QStringLiteral("opendiald"), {});
}

bool Backend::autostartEnabled() const
{
    QProcess p;
    p.start(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("is-enabled"), QStringLiteral("opendial.service")});
    p.waitForFinished(2000);
    return p.exitCode() == 0;
}

void Backend::setAutostart(bool on)
{
    QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("--user"), on ? QStringLiteral("enable") : QStringLiteral("disable"),
                                                    QStringLiteral("--now"), QStringLiteral("opendial.service")});
}

void Backend::setActiveTool(const QString &id) { callBool("SetActiveTool", {id}); }
void Backend::lockProfile(const QString &id) { callBool("LockProfile", {id}); }
void Backend::unlockProfile() { callBool("UnlockProfile"); }
void Backend::openMenu() { callBool("OpenMenu"); }
void Backend::closeMenu() { callBool("CloseMenu"); }
bool Backend::testHaptics(const QString &w) { return callBool("TestHaptics", {w}); }
void Backend::simulate(const QVariantMap &cmd) { callString("Simulate", {compact(QJsonObject::fromVariantMap(cmd))}); }
void Backend::simulateContext(const QString &appId)
{
    callBool("SimulateContext", {appId.isEmpty() ? QString() : compact(QJsonObject{{QStringLiteral("appId"), appId}})});
}
void Backend::setPaused(bool p) { callBool("SetPaused", {p}); }
void Backend::completeFirstRun() { callBool("CompleteFirstRun"); }
void Backend::reconnect() { callBool("Reconnect"); }
void Backend::requestPortalPermission() { callBool("RequestPortalPermission"); }
QVariantList Backend::approvals() { return parseJson(callString("ListApprovals")).toArray().toVariantList(); }
bool Backend::approve(const QString &fp) { return callBool("ApproveAction", {fp}); }
bool Backend::revokeApproval(const QString &fp) { return callBool("RevokeApproval", {fp}); }
bool Backend::approvePlugin(const QString &appId, bool allow) { return callBool("ApprovePlugin", {appId, allow}); }
QVariantMap Backend::bluetooth(const QString &action, const QString &path) { return parseJson(callString("Bluetooth", {action, path})).toObject().toVariantMap(); }
QVariantMap Backend::diagnostics(bool redact) { return parseJson(callString("GetDiagnostics", {redact})).toObject().toVariantMap(); }
void Backend::setRawStreaming(bool on) { callBool("SetRawStreaming", {on}); }

QString Backend::exportDiagnostics(const QUrl &file, bool redact)
{
    const QJsonObject r = parseJson(callString("ExportDiagnostics", {file.toLocalFile(), redact})).toObject();
    return r.value(QStringLiteral("ok")).toBool() ? QString() : r.value(QStringLiteral("error")).toString(tr("daemon not running"));
}

QString Backend::exportProfile(const QString &id, const QUrl &file)
{
    QJsonObject env;
    const QString s = callString("ExportProfile", {id});
    if (!s.isEmpty())
        env = parseJson(s).toObject();
    else
        env = exportProfileEnvelope(profileById(m_config.value(QStringLiteral("profiles")).toArray(), id));
    QFile f(file.toLocalFile());
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(env).toJson()) < 0)
        return f.errorString();
    return {};
}

QString Backend::importProfile(const QUrl &file)
{
    QFile f(file.toLocalFile());
    if (!f.open(QIODevice::ReadOnly))
        return f.errorString();
    const QJsonObject r = parseJson(callString("ImportProfile", {QString::fromUtf8(f.readAll())})).toObject();
    if (!r.value(QStringLiteral("ok")).toBool()) {
        QStringList e;
        for (const auto &v : r.value(QStringLiteral("errors")).toArray())
            e << v.toString();
        return e.isEmpty() ? tr("import failed") : e.join(QLatin1Char('\n'));
    }
    refresh();
    const int risky = r.value(QStringLiteral("riskyActions")).toArray().size();
    setMessage(risky ? tr("Imported. %1 action(s) need approval before they run (Integrations → Approvals).").arg(risky) : tr("Profile imported."));
    return {};
}

void Backend::copyToClipboard(const QString &text) { QGuiApplication::clipboard()->setText(text); }
void Backend::openUrl(const QString &url) { QDesktopServices::openUrl(QUrl(url)); }

} // namespace opendial
