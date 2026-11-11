// SPDX-License-Identifier: MIT
#pragma once

#include "Action.h"

#include <QJsonObject>
#include <QString>

namespace opendial {

constexpr int kConfigVersion = 1;
inline const QString kConfigSchema = QStringLiteral("opendial.config");
inline const QString kProfileSchema = QStringLiteral("opendial.profile");

QJsonObject defaultConfig();
QJsonObject defaultSettings();

struct ValidationResult {
    ValidationIssues issues;
    bool ok() const;
    QStringList errors() const;
    QStringList warnings() const;
    QJsonArray toJson() const;
};

ValidationResult validateConfig(const QJsonObject &config);
ValidationResult validateProfile(const QJsonObject &profile, const QString &path = QStringLiteral("profile"));
void validateTool(const QJsonObject &tool, const QString &path, ValidationIssues &issues, int depth = 0);

struct MigrationResult {
    QJsonObject config;
    bool ok = false;
    bool changed = false;
    bool tooNew = false; // written by a newer OpenDial: never touch it
    QStringList log;
};
// Migrates any older supported version to kConfigVersion. Never destructive:
// unknown keys are preserved.
MigrationResult migrateConfig(const QJsonObject &input);

// Fill missing settings keys from defaults (deep merge, user values win).
QJsonObject mergeDefaults(const QJsonObject &defaults, const QJsonObject &user);

// Persistent JSON file with atomic writes, rotating backups and rollback.
class JsonStore {
public:
    explicit JsonStore(QString path, int backups = 5);
    QString path() const { return m_path; }

    // Returns false and sets error if the file exists but cannot be parsed.
    bool read(QJsonObject &out, QString *error) const;
    // Atomic replace (QSaveFile + fsync + rename). Keeps N rotated backups.
    // Refuses to follow a symlinked target or write into a world-writable dir.
    bool write(const QJsonObject &obj, QString *error);
    // Restore the newest backup that parses; returns its index or -1.
    int rollback(QJsonObject &out, QString *error);
    QStringList backupPaths() const;

private:
    QString m_path;
    int m_backups;
};

struct LoadedConfig {
    QJsonObject config;
    ValidationResult validation;
    QStringList messages;
    bool readOnly = false;   // file is from a newer version: run with defaults, never overwrite
    bool fromBackup = false; // primary was corrupt, restored backup
    bool fromDefaults = false;
};

// Load + migrate + validate with fallback to backups or defaults.
LoadedConfig loadConfig(JsonStore &store);

QString configDir();   // $XDG_CONFIG_HOME/opendial
QString stateDir();    // $XDG_STATE_HOME/opendial
QString runtimeDir();  // $XDG_RUNTIME_DIR/opendial

// Find a tool (recursively through folders) in a profile's tool list.
QJsonObject findTool(const QJsonArray &tools, const QString &id);
QJsonArray flattenTools(const QJsonArray &tools);

// Profile import/export envelope: {"schema":"opendial.profile","version":1,"profile":{..}}
QJsonObject exportProfileEnvelope(const QJsonObject &profile);
// Validates an envelope or bare profile; returns the profile and the list of
// risky actions (which the daemon leaves unapproved).
bool importProfileEnvelope(const QJsonObject &doc, QJsonObject &profile, QStringList *errors,
                           QList<QPair<QString, QJsonObject>> *risky);

} // namespace opendial
