// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace opendial {

// What the desktop adapter knows about the focused window. Titles are only
// used for window-level matching and are never logged or exported.
struct FocusContext {
    QString appId;      // Wayland app_id / X11 WM_CLASS instance or class
    QString windowClass;
    QString title;
    QString exe;        // basename of /proc/<pid>/exe when known
    QString flatpakId;  // from /proc/<pid>/cgroup when sandboxed
    QString workspace;
    qint64 pid = 0;
    QString source;     // adapter name: hyprland, kwin, gnome, x11, simulated, ...
    bool secure = false; // password prompt / polkit agent / lock screen

    bool operator==(const FocusContext &o) const
    {
        return appId == o.appId && windowClass == o.windowClass && title == o.title && workspace == o.workspace
               && pid == o.pid && secure == o.secure;
    }
    bool operator!=(const FocusContext &o) const { return !(*this == o); }
    QJsonObject toJson(bool redact) const;
    static FocusContext fromJson(const QJsonObject &o);
};

// Normalise an app identifier for matching: lower-case, strip ".desktop",
// strip a trailing "-bin".
QString normalizeAppId(const QString &id);
// Candidate identifiers for a context (appId, class, flatpak id, exe, last
// reverse-DNS component).
QStringList appIdCandidates(const FocusContext &ctx);

enum class ProfileLayer { Global = 0, Workspace = 1, App = 2, Window = 3, Lock = 4 };
QString layerName(ProfileLayer l);

struct Resolution {
    QString profileId;
    ProfileLayer layer = ProfileLayer::Global;
    QString reason;          // human-readable explanation for the UI
    QStringList candidates;  // every profile that matched, highest precedence first
    QJsonObject toJson() const;
};

// Deterministic precedence: manual lock > window > app > workspace > global.
// Ties within a layer: higher "priority" wins, then earlier position.
Resolution resolveProfile(const QJsonArray &profiles, const FocusContext &ctx, const QString &lockedProfileId);

QJsonObject profileById(const QJsonArray &profiles, const QString &id);
QJsonObject globalProfile(const QJsonArray &profiles);

// Tools offered in the menu for a profile: its own tools, then extra tools
// (from approved plugins, each carrying "plugin": appId), then the global
// tools when menuMode == "extend" (a profile tool with the same id overrides
// the global one). A tool with "replacedBy": appId is left out while that
// plugin supplies tools, so a shortcut-based tool and the plugin's direct
// version never both appear.
QJsonArray composeMenuTools(const QJsonArray &profiles, const QString &profileId, const QJsonArray &extraTools = {});
// Remove disabled tools / empty folders (recursively).
QJsonArray enabledTools(const QJsonArray &tools);

} // namespace opendial
