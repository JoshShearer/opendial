// SPDX-License-Identifier: MIT
#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <optional>

class QDBusVariant;

namespace opendial {

// What the desktop says about its look. Every field is optional: an empty
// QColor or an unset `dark` means "the desktop did not say".
struct SystemThemeState {
    QString source;              // "omarchy", "portal", "qt" or "" (nothing known)
    std::optional<bool> dark;
    QColor accent;
    QColor background;
    QColor foreground;
    int radius = -1;             // window corner rounding in px, -1 = unknown
};

// The colours the menu, the OSD and the settings app draw with.
struct MenuPalette {
    bool dark = true;
    QColor background;
    QColor foreground;
    QColor foregroundDim;
    QColor accent;
    QColor accentForeground;     // text and icons drawn on the accent
    int radius = 12;             // corner radius for rectangular panels
    QString source;              // where the colours came from, for diagnostics
    QVariantMap toVariant() const;
};

// Omarchy's colors.toml: only the flat `key = "#rrggbb"` lines matter.
SystemThemeState parseOmarchyColors(const QString &toml);

// Applies the user's settings (`settings.menu.theme`: system|dark|light,
// `settings.menu.accent`: system|#rrggbb) on top of what the desktop reports.
MenuPalette resolveMenuPalette(const SystemThemeState &system, const QString &themeSetting, const QString &accentSetting);

// The text colour that reads best on `bg`, chosen by WCAG contrast from the
// candidates (falls back to black or white).
QColor readableOn(const QColor &bg, const QColor &light = QColor(), const QColor &dark = QColor());

// Follows the desktop theme live:
// - Omarchy: ~/.local/state/omarchy/current/theme/colors.toml, watched; a theme
//   switch replaces the symlinks, so the watcher is re-armed on every change.
//   Corner rounding comes from Hyprland (`decoration:rounding`).
// - Elsewhere (GNOME, KDE, COSMIC): the XDG portal setting
//   org.freedesktop.appearance color-scheme and accent-color, live.
// - Fallback: Qt's colour scheme / palette.
// It never writes anything.
class SystemTheme : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)
public:
    struct Options {
        QString omarchyThemeDir = defaultOmarchyThemeDir();
        bool usePortal = true;
        bool useHyprland = true;
    };
    explicit SystemTheme(QObject *parent = nullptr);
    SystemTheme(const Options &options, QObject *parent = nullptr);

    static QString defaultOmarchyThemeDir();

    SystemThemeState state() const;
    int revision() const { return m_revision; }
    QString source() const { return state().source; }
    MenuPalette palette(const QString &themeSetting, const QString &accentSetting) const
    {
        return resolveMenuPalette(state(), themeSetting, accentSetting);
    }
    // For QML: bind to `revision` so the call re-evaluates on a change.
    Q_INVOKABLE QVariantMap paletteFor(const QString &themeSetting, const QString &accentSetting) const
    {
        return palette(themeSetting, accentSetting).toVariant();
    }

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void onPortalSettingChanged(const QString &ns, const QString &key, const QDBusVariant &value);

private:
    void reloadOmarchy();
    void rearm();
    void readPortal();
    void applyPortalValue(const QString &key, const QVariant &value);
    void readHyprRounding();
    void bump();

    Options m_opt;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    std::optional<SystemThemeState> m_omarchy;
    std::optional<bool> m_portalDark;
    QColor m_portalAccent;
    int m_hyprRadius = -1;
    int m_revision = 0;
};

} // namespace opendial
