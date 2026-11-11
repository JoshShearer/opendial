// SPDX-License-Identifier: MIT
#include "Config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static void opendialInitCoreResources()
{
    // Resources in a static library are not registered automatically.
    Q_INIT_RESOURCE(core);
}

namespace opendial {

namespace {
const QRegularExpression &idRe()
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,79}$"));
    return re;
}
} // namespace

QJsonObject defaultConfig()
{
    static const bool init = (opendialInitCoreResources(), true);
    Q_UNUSED(init);
    QFile f(QStringLiteral(":/opendial/default-config.json"));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("default configuration resource missing");
    return QJsonDocument::fromJson(f.readAll()).object();
}

QJsonObject defaultSettings() { return defaultConfig().value(QLatin1String("settings")).toObject(); }

bool ValidationResult::ok() const
{
    for (const auto &i : issues)
        if (!i.warning)
            return false;
    return true;
}

QStringList ValidationResult::errors() const
{
    QStringList l;
    for (const auto &i : issues)
        if (!i.warning)
            l << i.path + QStringLiteral(": ") + i.message;
    return l;
}

QStringList ValidationResult::warnings() const
{
    QStringList l;
    for (const auto &i : issues)
        if (i.warning)
            l << i.path + QStringLiteral(": ") + i.message;
    return l;
}

QJsonArray ValidationResult::toJson() const
{
    QJsonArray a;
    for (const auto &i : issues)
        a.append(QJsonObject{{QStringLiteral("path"), i.path},
                             {QStringLiteral("message"), i.message},
                             {QStringLiteral("severity"), i.warning ? QStringLiteral("warning") : QStringLiteral("error")}});
    return a;
}

void validateTool(const QJsonObject &t, const QString &path, ValidationIssues &is, int depth)
{
    if (depth > 3) {
        is.append({path, QStringLiteral("folders nested deeper than 3 levels"), false});
        return;
    }
    const QString id = t.value(QLatin1String("id")).toString();
    if (!idRe().match(id).hasMatch())
        is.append({path + QStringLiteral(".id"), QStringLiteral("tool id must be 1-80 chars [A-Za-z0-9_.:-]"), false});
    if (t.value(QLatin1String("name")).toString().trimmed().isEmpty())
        is.append({path + QStringLiteral(".name"), QStringLiteral("tool name required"), false});
    if (t.value(QLatin1String("name")).toString().size() > 40)
        is.append({path + QStringLiteral(".name"), QStringLiteral("name longer than 40 characters may be truncated"), true});
    const bool folder = t.value(QLatin1String("kind")).toString() == QLatin1String("folder");
    if (folder) {
        const auto kids = t.value(QLatin1String("children")).toArray();
        if (kids.isEmpty())
            is.append({path + QStringLiteral(".children"), QStringLiteral("folder is empty"), true});
        QSet<QString> seen;
        for (int i = 0; i < kids.size(); ++i) {
            const auto k = kids.at(i).toObject();
            if (seen.contains(k.value(QLatin1String("id")).toString()))
                is.append({path + QStringLiteral(".children[%1].id").arg(i), QStringLiteral("duplicate tool id"), false});
            seen.insert(k.value(QLatin1String("id")).toString());
            validateTool(k, path + QStringLiteral(".children[%1]").arg(i), is, depth + 1);
        }
        return;
    }
    const auto b = t.value(QLatin1String("bindings")).toObject();
    const QStringList slots = slot::all();
    for (auto it = b.begin(); it != b.end(); ++it) {
        if (!slots.contains(it.key())) {
            is.append({path + QStringLiteral(".bindings.") + it.key(), QStringLiteral("unknown gesture slot"), false});
            continue;
        }
        if (!it.value().isObject()) {
            is.append({path + QStringLiteral(".bindings.") + it.key(), QStringLiteral("binding must be an action object"), false});
            continue;
        }
        validateAction(it.value().toObject(), path + QStringLiteral(".bindings.") + it.key(), is);
    }
    if (b.contains(slot::LongPress))
        is.append({path + QStringLiteral(".bindings.long_press"),
                   QStringLiteral("long press opens the radial menu; this binding only fires when the menu gesture is moved (settings.menu.openGesture)"), true});
}

ValidationResult validateProfile(const QJsonObject &p, const QString &path)
{
    ValidationResult r;
    auto &is = r.issues;
    if (!idRe().match(p.value(QLatin1String("id")).toString()).hasMatch())
        is.append({path + QStringLiteral(".id"), QStringLiteral("profile id must be 1-80 chars [A-Za-z0-9_.:-]"), false});
    if (p.value(QLatin1String("name")).toString().trimmed().isEmpty())
        is.append({path + QStringLiteral(".name"), QStringLiteral("profile name required"), false});
    const auto m = p.value(QLatin1String("match")).toObject();
    const QString kind = m.value(QLatin1String("kind")).toString();
    static const QStringList kinds = {QStringLiteral("global"), QStringLiteral("app"), QStringLiteral("window"),
                                      QStringLiteral("workspace"), QStringLiteral("manual")};
    if (!kinds.contains(kind))
        is.append({path + QStringLiteral(".match.kind"), QStringLiteral("kind must be global/app/window/workspace/manual"), false});
    if ((kind == QLatin1String("app") || kind == QLatin1String("window")) && m.value(QLatin1String("appIds")).toArray().isEmpty())
        is.append({path + QStringLiteral(".match.appIds"), QStringLiteral("app/window profiles need at least one app id"), false});
    if (kind == QLatin1String("window")) {
        QRegularExpression re(m.value(QLatin1String("titleRegex")).toString());
        if (m.value(QLatin1String("titleRegex")).toString().isEmpty() && m.value(QLatin1String("windowClass")).toString().isEmpty())
            is.append({path + QStringLiteral(".match"), QStringLiteral("window profiles need titleRegex or windowClass"), false});
        else if (!re.isValid())
            is.append({path + QStringLiteral(".match.titleRegex"), QStringLiteral("invalid regular expression: ") + re.errorString(), false});
    }
    if (kind == QLatin1String("workspace") && m.value(QLatin1String("workspaces")).toArray().isEmpty())
        is.append({path + QStringLiteral(".match.workspaces"), QStringLiteral("workspace profiles need at least one workspace"), false});
    const QString mm = p.value(QLatin1String("menuMode")).toString(QStringLiteral("replace"));
    if (mm != QLatin1String("replace") && mm != QLatin1String("extend"))
        is.append({path + QStringLiteral(".menuMode"), QStringLiteral("menuMode must be replace or extend"), false});
    const auto tools = p.value(QLatin1String("tools")).toArray();
    if (tools.size() > 64)
        is.append({path + QStringLiteral(".tools"), QStringLiteral("at most 64 top-level tools"), false});
    QSet<QString> ids;
    for (const auto &t : flattenTools(tools)) {
        const QString id = t.toObject().value(QLatin1String("id")).toString();
        if (ids.contains(id))
            is.append({path + QStringLiteral(".tools"), QStringLiteral("duplicate tool id '%1'").arg(id), false});
        ids.insert(id);
    }
    for (int i = 0; i < tools.size(); ++i)
        validateTool(tools.at(i).toObject(), path + QStringLiteral(".tools[%1]").arg(i), is);
    const QString def = p.value(QLatin1String("defaultTool")).toString();
    if (!def.isEmpty() && !ids.contains(def) && kind == QLatin1String("global"))
        is.append({path + QStringLiteral(".defaultTool"), QStringLiteral("default tool '%1' not found").arg(def), true});
    return r;
}

ValidationResult validateConfig(const QJsonObject &c)
{
    ValidationResult r;
    auto &is = r.issues;
    if (c.value(QLatin1String("schema")).toString() != kConfigSchema)
        is.append({QStringLiteral("schema"), QStringLiteral("expected \"%1\"").arg(kConfigSchema), false});
    const int v = c.value(QLatin1String("version")).toInt(-1);
    if (v != kConfigVersion)
        is.append({QStringLiteral("version"), QStringLiteral("unsupported version %1 (expected %2)").arg(v).arg(kConfigVersion), false});
    const auto s = c.value(QLatin1String("settings")).toObject();
    const auto g = s.value(QLatin1String("gestures")).toObject();
    auto range = [&](const QJsonObject &o, const char *k, const QString &p, double lo, double hi) {
        const auto val = o.value(QLatin1String(k));
        if (!val.isUndefined() && (!val.isDouble() || val.toDouble() < lo || val.toDouble() > hi))
            is.append({p + QLatin1Char('.') + QLatin1String(k), QStringLiteral("must be a number between %1 and %2").arg(lo).arg(hi), false});
    };
    range(g, "longPressMs", QStringLiteral("settings.gestures"), 150, 5000);
    range(g, "doubleClickMs", QStringLiteral("settings.gestures"), 80, 1500);
    range(g, "debounceMs", QStringLiteral("settings.gestures"), 0, 100);
    range(g, "holdRotateThresholdDeg", QStringLiteral("settings.gestures"), 0, 45);
    const auto menu = s.value(QLatin1String("menu")).toObject();
    range(menu, "maxPerRing", QStringLiteral("settings.menu"), 3, 12);
    range(menu, "timeoutMs", QStringLiteral("settings.menu"), 0, 120000);
    range(menu, "detentDegrees", QStringLiteral("settings.menu"), 5, 90);
    range(menu, "size", QStringLiteral("settings.menu"), 200, 900);
    range(menu, "opacity", QStringLiteral("settings.menu"), 0.3, 1.0);
    static const QStringList placements = {QStringLiteral("pointer"), QStringLiteral("center"), QStringLiteral("saved"), QStringLiteral("display")};
    if (menu.contains(QLatin1String("placement")) && !placements.contains(menu.value(QLatin1String("placement")).toString()))
        is.append({QStringLiteral("settings.menu.placement"), QStringLiteral("placement must be pointer/center/saved/display"), false});
    const auto dev = s.value(QLatin1String("device")).toObject();
    range(dev, "connectGuardMs", QStringLiteral("settings.device"), 0, 10000);
    const auto http = s.value(QLatin1String("integrations")).toObject().value(QLatin1String("httpApi")).toObject();
    range(http, "port", QStringLiteral("settings.integrations.httpApi"), 1024, 65535);

    const auto profiles = c.value(QLatin1String("profiles")).toArray();
    int globals = 0;
    QSet<QString> pids;
    for (int i = 0; i < profiles.size(); ++i) {
        const auto p = profiles.at(i).toObject();
        if (p.value(QLatin1String("match")).toObject().value(QLatin1String("kind")).toString() == QLatin1String("global"))
            ++globals;
        const QString id = p.value(QLatin1String("id")).toString();
        if (pids.contains(id))
            is.append({QStringLiteral("profiles[%1].id").arg(i), QStringLiteral("duplicate profile id '%1'").arg(id), false});
        pids.insert(id);
        auto pr = validateProfile(p, QStringLiteral("profiles[%1]").arg(i));
        is.append(pr.issues);
    }
    if (globals != 1)
        is.append({QStringLiteral("profiles"), QStringLiteral("exactly one global profile is required (found %1)").arg(globals), false});
    return r;
}

QJsonObject mergeDefaults(const QJsonObject &defaults, const QJsonObject &user)
{
    QJsonObject out = user;
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (!out.contains(it.key()))
            out.insert(it.key(), it.value());
        else if (it.value().isObject() && out.value(it.key()).isObject())
            out.insert(it.key(), mergeDefaults(it.value().toObject(), out.value(it.key()).toObject()));
    }
    return out;
}

MigrationResult migrateConfig(const QJsonObject &input)
{
    MigrationResult r;
    QJsonObject c = input;
    int v = c.value(QLatin1String("version")).toInt(0);
    if (v > kConfigVersion) {
        r.tooNew = true;
        r.log << QStringLiteral("configuration version %1 is newer than supported %2; refusing to modify it").arg(v).arg(kConfigVersion);
        return r;
    }
    if (v == 0) {
        // v0: pre-release layout {"tools":[...], "settings":{...}} without profiles.
        QJsonObject g{{QStringLiteral("id"), QStringLiteral("global")},
                      {QStringLiteral("name"), QStringLiteral("All apps")},
                      {QStringLiteral("enabled"), true},
                      {QStringLiteral("match"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("global")}}},
                      {QStringLiteral("menuMode"), QStringLiteral("replace")},
                      {QStringLiteral("tools"), c.value(QLatin1String("tools")).toArray()}};
        if (c.contains(QLatin1String("defaultTool")))
            g.insert(QStringLiteral("defaultTool"), c.value(QLatin1String("defaultTool")));
        if (!c.contains(QLatin1String("profiles")))
            c.insert(QStringLiteral("profiles"), QJsonArray{g});
        c.remove(QStringLiteral("tools"));
        c.remove(QStringLiteral("defaultTool"));
        c.insert(QStringLiteral("schema"), kConfigSchema);
        c.insert(QStringLiteral("version"), 1);
        r.log << QStringLiteral("migrated v0 -> v1: wrapped tools into the global profile");
        r.changed = true;
        v = 1;
    }
    // Future: if (v == 1) { ...; v = 2; }
    const auto merged = mergeDefaults(QJsonObject{{QStringLiteral("settings"), defaultSettings()}}, c);
    if (merged != c)
        r.changed = true;
    r.config = merged;
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------- JsonStore

JsonStore::JsonStore(QString path, int backups) : m_path(std::move(path)), m_backups(backups) {}

QStringList JsonStore::backupPaths() const
{
    QStringList l;
    for (int i = 1; i <= m_backups; ++i)
        l << m_path + QStringLiteral(".bak.%1").arg(i);
    return l;
}

bool JsonStore::read(QJsonObject &out, QString *error) const
{
    QFile f(m_path);
    if (!f.exists()) {
        out = {};
        return true;
    }
    if (f.size() > 8 * 1024 * 1024) {
        if (error) *error = QStringLiteral("%1 is larger than 8 MiB").arg(m_path);
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QJsonParseError pe{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("%1: parse error at offset %2: %3").arg(m_path).arg(pe.offset).arg(pe.errorString());
        return false;
    }
    out = doc.object();
    return true;
}

bool JsonStore::write(const QJsonObject &obj, QString *error)
{
    QFileInfo fi(m_path);
    QDir().mkpath(fi.absolutePath());
    ::chmod(QFile::encodeName(fi.absolutePath()).constData(), 0700);
    struct stat st{};
    if (::lstat(QFile::encodeName(fi.absolutePath()).constData(), &st) == 0 && (st.st_mode & S_IWOTH)) {
        if (error) *error = QStringLiteral("refusing to write into world-writable directory %1").arg(fi.absolutePath());
        return false;
    }
    if (fi.isSymLink()) {
        if (error) *error = QStringLiteral("refusing to write through symlink %1").arg(m_path);
        return false;
    }
    // rotate backups: .bak.(n-1) -> .bak.n ... current -> .bak.1
    if (fi.exists() && m_backups > 0) {
        const auto b = backupPaths();
        QFile::remove(b.last());
        for (int i = m_backups - 1; i >= 1; --i)
            QFile::rename(b.at(i - 1), b.at(i));
        QFile::copy(m_path, b.first());
        QFile::setPermissions(b.first(), QFile::ReadOwner | QFile::WriteOwner);
    }
    QSaveFile sf(m_path);
    if (!sf.open(QIODevice::WriteOnly)) {
        if (error) *error = sf.errorString();
        return false;
    }
    sf.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    sf.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    if (!sf.commit()) {
        if (error) *error = sf.errorString();
        return false;
    }
    return true;
}

int JsonStore::rollback(QJsonObject &out, QString *error)
{
    const auto b = backupPaths();
    for (int i = 0; i < b.size(); ++i) {
        JsonStore s(b.at(i), 0);
        QJsonObject o;
        QString e;
        if (QFile::exists(b.at(i)) && s.read(o, &e) && !o.isEmpty()) {
            out = o;
            return i + 1;
        }
    }
    if (error) *error = QStringLiteral("no readable backup");
    return -1;
}

LoadedConfig loadConfig(JsonStore &store)
{
    LoadedConfig lc;
    QJsonObject raw;
    QString err;
    bool exists = QFile::exists(store.path());
    if (!store.read(raw, &err)) {
        lc.messages << QStringLiteral("configuration unreadable: ") + err;
        QJsonObject b;
        const int idx = store.rollback(b, &err);
        if (idx > 0) {
            lc.messages << QStringLiteral("restored backup #%1").arg(idx);
            raw = b;
            lc.fromBackup = true;
            // preserve the corrupt file for inspection, never delete it
            QFile::copy(store.path(), store.path() + QStringLiteral(".corrupt"));
        } else {
            raw = {};
            exists = false;
            QFile::copy(store.path(), store.path() + QStringLiteral(".corrupt"));
        }
    }
    if (!exists && raw.isEmpty()) {
        lc.config = defaultConfig();
        lc.fromDefaults = true;
        lc.validation = validateConfig(lc.config);
        return lc;
    }
    auto mr = migrateConfig(raw);
    lc.messages << mr.log;
    if (mr.tooNew) {
        lc.readOnly = true;
        lc.config = defaultConfig();
        lc.fromDefaults = true;
        lc.validation = validateConfig(lc.config);
        return lc;
    }
    lc.validation = validateConfig(mr.config);
    if (!lc.validation.ok()) {
        lc.messages << QStringLiteral("configuration invalid: ") + lc.validation.errors().join(QStringLiteral("; "));
        QJsonObject b;
        const int idx = store.rollback(b, &err);
        if (idx > 0) {
            auto mb = migrateConfig(b);
            auto vb = validateConfig(mb.config);
            if (mb.ok && vb.ok()) {
                lc.messages << QStringLiteral("using backup #%1").arg(idx);
                lc.config = mb.config;
                lc.validation = vb;
                lc.fromBackup = true;
                return lc;
            }
        }
        lc.messages << QStringLiteral("falling back to defaults (your file is kept untouched)");
        lc.config = defaultConfig();
        lc.fromDefaults = true;
        lc.readOnly = true; // do not overwrite the user's broken file automatically
        lc.validation = validateConfig(lc.config);
        return lc;
    }
    lc.config = mr.config;
    if (mr.changed) {
        QString werr;
        if (!store.write(lc.config, &werr))
            lc.messages << QStringLiteral("could not save migrated configuration: ") + werr;
    }
    return lc;
}

namespace {
QString xdgDir(const char *env, const QString &fallbackRel)
{
    const QByteArray v = qgetenv(env);
    QString base = (!v.isEmpty() && QDir::isAbsolutePath(QString::fromLocal8Bit(v)))
                       ? QString::fromLocal8Bit(v)
                       : QDir::homePath() + fallbackRel;
    return base + QStringLiteral("/opendial");
}
} // namespace

QString configDir() { return xdgDir("XDG_CONFIG_HOME", QStringLiteral("/.config")); }
QString stateDir() { return xdgDir("XDG_STATE_HOME", QStringLiteral("/.local/state")); }
QString runtimeDir()
{
    const QByteArray v = qgetenv("XDG_RUNTIME_DIR");
    if (!v.isEmpty())
        return QString::fromLocal8Bit(v) + QStringLiteral("/opendial");
    return QDir::tempPath() + QStringLiteral("/opendial-%1").arg(::getuid());
}

QJsonObject findTool(const QJsonArray &tools, const QString &id)
{
    for (const auto &v : tools) {
        const auto t = v.toObject();
        if (t.value(QLatin1String("id")).toString() == id)
            return t;
        if (t.value(QLatin1String("kind")).toString() == QLatin1String("folder")) {
            auto c = findTool(t.value(QLatin1String("children")).toArray(), id);
            if (!c.isEmpty())
                return c;
        }
    }
    return {};
}

QJsonArray flattenTools(const QJsonArray &tools)
{
    QJsonArray out;
    for (const auto &v : tools) {
        const auto t = v.toObject();
        out.append(t);
        if (t.value(QLatin1String("kind")).toString() == QLatin1String("folder"))
            for (const auto &c : flattenTools(t.value(QLatin1String("children")).toArray()))
                out.append(c);
    }
    return out;
}

QJsonObject exportProfileEnvelope(const QJsonObject &profile)
{
    return QJsonObject{{QStringLiteral("schema"), kProfileSchema},
                       {QStringLiteral("version"), kConfigVersion},
                       {QStringLiteral("profile"), profile}};
}

bool importProfileEnvelope(const QJsonObject &doc, QJsonObject &profile, QStringList *errors,
                           QList<QPair<QString, QJsonObject>> *risky)
{
    QJsonObject p;
    if (doc.value(QLatin1String("schema")).toString() == kProfileSchema) {
        const int v = doc.value(QLatin1String("version")).toInt(-1);
        if (v > kConfigVersion || v < 1) {
            if (errors) *errors << QStringLiteral("unsupported profile version %1").arg(v);
            return false;
        }
        p = doc.value(QLatin1String("profile")).toObject();
    } else if (doc.contains(QLatin1String("match")) && doc.contains(QLatin1String("tools"))) {
        p = doc; // bare profile object
    } else {
        if (errors) *errors << QStringLiteral("not an OpenDial profile document");
        return false;
    }
    auto vr = validateProfile(p);
    if (!vr.ok()) {
        if (errors) *errors << vr.errors();
        return false;
    }
    if (risky)
        collectRiskyActions(p, QStringLiteral("profile"), *risky);
    profile = p;
    return true;
}

} // namespace opendial
