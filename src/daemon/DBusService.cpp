// SPDX-License-Identifier: MIT
#include "DBusService.h"
#include "Engine.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace opendial {

namespace {
QString toJson(const QJsonObject &o) { return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)); }
QString toJson(const QJsonArray &a) { return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact)); }

bool parseObject(const QString &json, QJsonObject &out, QString *err)
{
    if (json.size() > 4 * 1024 * 1024) {
        *err = QStringLiteral("payload too large");
        return false;
    }
    QJsonParseError pe;
    const auto doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        *err = pe.error != QJsonParseError::NoError ? pe.errorString() : QStringLiteral("expected a JSON object");
        return false;
    }
    out = doc.object();
    return true;
}

QString validationJson(const ValidationResult &v, const QString &extraError = {})
{
    QStringList errors = v.errors();
    if (!extraError.isEmpty())
        errors << extraError;
    return toJson(QJsonObject{{QStringLiteral("ok"), v.ok() && extraError.isEmpty()},
                              {QStringLiteral("errors"), QJsonArray::fromStringList(errors)},
                              {QStringLiteral("warnings"), QJsonArray::fromStringList(v.warnings())},
                              {QStringLiteral("issues"), v.toJson()}});
}
} // namespace

DBusService::DBusService(Engine *engine, QObject *parent) : QObject(parent), m_engine(engine)
{
    connect(engine, &Engine::stateChanged, this, [this](const QJsonObject &s) { Q_EMIT StateChanged(toJson(s)); });
    connect(engine, &Engine::configChanged, this, [this](const QJsonObject &c) { Q_EMIT ConfigChanged(toJson(c)); });
    connect(engine, &Engine::rawEvent, this, [this](const QJsonObject &e) { Q_EMIT RawEvent(toJson(e)); });
    connect(engine, &Engine::gestureEvent, this, [this](const QJsonObject &e) { Q_EMIT GestureEvent(toJson(e)); });
    connect(engine, &Engine::pluginEvent, this, [this](const QString &a, const QJsonObject &e) { Q_EMIT PluginEvent(a, toJson(e)); });
    connect(engine, &Engine::menuChanged, this, [this](const QJsonObject &m) { Q_EMIT MenuChanged(toJson(m)); });
    m_watcher.setConnection(QDBusConnection::sessionBus());
    m_watcher.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &name) {
        m_engine->busNameVanished(name);
        m_watcher.removeWatchedService(name);
    });
}

bool DBusService::registerOnBus(QString *error)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        *error = QStringLiteral("no session bus");
        return false;
    }
    if (!bus.registerObject(kDBusPath, this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals | QDBusConnection::ExportAllProperties)) {
        *error = bus.lastError().message();
        return false;
    }
    // Single instance: do not queue, do not allow replacement.
    const auto r = bus.interface()->registerService(kDBusService, QDBusConnectionInterface::DontQueueService,
                                                    QDBusConnectionInterface::DontAllowReplacement);
    if (!r.isValid() || r.value() != QDBusConnectionInterface::ServiceRegistered) {
        *error = QStringLiteral("another OpenDial daemon owns %1").arg(kDBusService);
        return false;
    }
    return true;
}

QString DBusService::version() const { return QStringLiteral(OPENDIAL_VERSION); }

QString DBusService::GetState() { return toJson(m_engine->stateJson()); }
QString DBusService::GetConfig() { return toJson(m_engine->config()); }

QString DBusService::SetConfig(const QString &json)
{
    QJsonObject o;
    QString err;
    if (!parseObject(json, o, &err))
        return validationJson({}, err);
    QString werr;
    const auto v = m_engine->setConfig(o, &werr);
    return validationJson(v, v.ok() ? werr : QString());
}

QString DBusService::ValidateConfig(const QString &json)
{
    QJsonObject o;
    QString err;
    if (!parseObject(json, o, &err))
        return validationJson({}, err);
    auto mig = migrateConfig(o);
    if (!mig.ok)
        return validationJson({}, mig.log.join(QStringLiteral("; ")));
    return validationJson(validateConfig(mig.config));
}

QString DBusService::ResetToDefaults()
{
    QString err;
    const bool ok = m_engine->resetToDefaults(&err);
    return toJson(QJsonObject{{QStringLiteral("ok"), ok}, {QStringLiteral("error"), err}});
}

QString DBusService::RollbackConfig()
{
    QString err;
    const int idx = m_engine->rollbackConfig(&err);
    return toJson(QJsonObject{{QStringLiteral("ok"), idx >= 0}, {QStringLiteral("backup"), idx}, {QStringLiteral("error"), err}});
}

QString DBusService::ImportProfile(const QString &json)
{
    QJsonObject o;
    QString err;
    if (!parseObject(json, o, &err))
        return toJson(QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("errors"), QJsonArray{err}}});
    return toJson(m_engine->importProfile(o));
}

QString DBusService::ExportProfile(const QString &id) { return toJson(m_engine->exportProfile(id)); }

bool DBusService::SetActiveTool(const QString &id) { return m_engine->setActiveTool(id); }
bool DBusService::LockProfile(const QString &id) { return m_engine->lockProfile(id); }
void DBusService::UnlockProfile() { m_engine->unlockProfile(); }
void DBusService::OpenMenu() { m_engine->menuOp(QStringLiteral("open")); }
void DBusService::CloseMenu() { m_engine->menuOp(QStringLiteral("close")); }
void DBusService::SetPaused(bool p) { m_engine->setPaused(p); }
bool DBusService::TestHaptics(const QString &w) { return m_engine->testHaptics(w); }
void DBusService::Reconnect() { m_engine->reconnect(); }
void DBusService::RequestPortalPermission() { m_engine->requestPortalPermission(); }
QString DBusService::Bluetooth(const QString &action, const QString &path) { return toJson(m_engine->bluetooth(action, path)); }

QString DBusService::Simulate(const QString &json)
{
    QJsonObject o;
    QString err;
    if (!parseObject(json, o, &err) || !m_engine->simulate(o, &err))
        return toJson(QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), err}});
    return toJson(QJsonObject{{QStringLiteral("ok"), true}});
}

bool DBusService::SimulateContext(const QString &json)
{
    QJsonObject o;
    QString err;
    if (!json.trimmed().isEmpty() && !parseObject(json, o, &err))
        return false;
    return m_engine->simulateContext(o);
}

void DBusService::ReportFocus(const QString &json)
{
    QJsonObject o;
    QString err;
    if (parseObject(json, o, &err))
        m_engine->reportFocus(o);
}

void DBusService::SetRawStreaming(bool on) { m_engine->setRawStreaming(on); }

QString DBusService::GetDiagnostics(bool redact) { return toJson(m_engine->diagnostics(redact)); }

QString DBusService::ExportDiagnostics(const QString &path, bool redact)
{
    // Only write into the caller's home or runtime dir, never follow symlinks.
    const QFileInfo fi(path);
    const QString abs = fi.absoluteFilePath();
    const bool allowed = abs.startsWith(QDir::homePath() + QLatin1Char('/')) || abs.startsWith(QStringLiteral("/tmp/"))
                         || abs.startsWith(qEnvironmentVariable("XDG_RUNTIME_DIR") + QLatin1Char('/'));
    if (!allowed || fi.isSymLink() || fi.isDir())
        return toJson(QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("path not allowed")}});
    QSaveFile f(abs);
    if (!f.open(QIODevice::WriteOnly))
        return toJson(QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), f.errorString()}});
    f.write(QJsonDocument(m_engine->diagnostics(redact)).toJson(QJsonDocument::Indented));
    if (!f.commit())
        return toJson(QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), f.errorString()}});
    QFile::setPermissions(abs, QFile::ReadOwner | QFile::WriteOwner);
    return toJson(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("path"), abs}});
}

QString DBusService::ListApprovals() { return toJson(m_engine->pendingApprovals()); }
bool DBusService::ApproveAction(const QString &fp) { return m_engine->approve(fp); }
bool DBusService::RevokeApproval(const QString &fp) { return m_engine->revokeApproval(fp); }

QString DBusService::RegisterApp(const QString &json)
{
    QJsonObject o;
    QString err;
    if (!parseObject(json, o, &err))
        return toJson(QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), err}});
    const QString sender = calledFromDBus() ? message().service() : QString();
    if (!sender.isEmpty())
        m_watcher.addWatchedService(sender);
    return toJson(m_engine->registerApp(o, sender));
}

bool DBusService::UnregisterApp(const QString &appId)
{
    return m_engine->unregisterApp(appId, calledFromDBus() ? message().service() : QString());
}

bool DBusService::ApprovePlugin(const QString &appId, bool approved) { return m_engine->approvePlugin(appId, approved); }

bool DBusService::PluginFeedback(const QString &appId, const QString &json)
{
    QJsonObject o;
    QString err;
    if (!parseObject(json, o, &err))
        return false;
    return m_engine->pluginFeedback(appId, o, calledFromDBus() ? message().service() : QString());
}

QString DBusService::ListTools() { return toJson(m_engine->menuTools()); }

QString DBusService::ListProfiles()
{
    QJsonArray out;
    for (const auto &v : m_engine->config().value(QStringLiteral("profiles")).toArray()) {
        const auto p = v.toObject();
        out.append(QJsonObject{{QStringLiteral("id"), p.value(QStringLiteral("id"))},
                               {QStringLiteral("name"), p.value(QStringLiteral("name"))},
                               {QStringLiteral("enabled"), p.value(QStringLiteral("enabled")).toBool(true)},
                               {QStringLiteral("match"), p.value(QStringLiteral("match"))},
                               {QStringLiteral("active"), p.value(QStringLiteral("id")).toString() == m_engine->activeProfileId()}});
    }
    return toJson(out);
}

} // namespace opendial
