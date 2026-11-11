// SPDX-License-Identifier: MIT
#include "SystemTheme.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStyleHints>
#include <QtMath>

namespace opendial {

namespace {

const QColor kDefaultAccent(QStringLiteral("#4c8dff"));
const QColor kDarkBackground(QStringLiteral("#171a21"));
const QColor kDarkForeground(QStringLiteral("#f2f4f8"));
const QColor kLightBackground(QStringLiteral("#f7f7fa"));
const QColor kLightForeground(QStringLiteral("#1b1f27"));

const auto kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const auto kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const auto kPortalSettings = QStringLiteral("org.freedesktop.portal.Settings");
const auto kAppearance = QStringLiteral("org.freedesktop.appearance");

double channel(double c)
{
    return c <= 0.03928 ? c / 12.92 : qPow((c + 0.055) / 1.055, 2.4);
}

double luminance(const QColor &c)
{
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}

double contrast(const QColor &a, const QColor &b)
{
    double la = luminance(a), lb = luminance(b);
    if (la < lb)
        std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(float(a.redF() * t + b.redF() * (1 - t)), float(a.greenF() * t + b.greenF() * (1 - t)),
                            float(a.blueF() * t + b.blueF() * (1 - t)));
}

// The portal's Read wraps the value in one more variant than ReadOne does.
QVariant unwrap(QVariant v)
{
    while (v.canConvert<QDBusVariant>() && v.userType() == qMetaTypeId<QDBusVariant>())
        v = v.value<QDBusVariant>().variant();
    return v;
}

} // namespace

QVariantMap MenuPalette::toVariant() const
{
    return {{QStringLiteral("dark"), dark},
            {QStringLiteral("background"), background},
            {QStringLiteral("foreground"), foreground},
            {QStringLiteral("foregroundDim"), foregroundDim},
            {QStringLiteral("accent"), accent},
            {QStringLiteral("accentForeground"), accentForeground},
            {QStringLiteral("radius"), radius},
            {QStringLiteral("source"), source}};
}

SystemThemeState parseOmarchyColors(const QString &toml)
{
    static const QRegularExpression line(QStringLiteral(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*["']([^"']*)["'])"));
    SystemThemeState s;
    QString mode;
    for (const auto &l : toml.split(QLatin1Char('\n'))) {
        const auto m = line.match(l);
        if (!m.hasMatch())
            continue;
        const QString key = m.captured(1), val = m.captured(2).trimmed();
        auto color = [&] { const QColor c(val); return c.isValid() && val.startsWith(QLatin1Char('#')) ? c : QColor(); };
        if (key == QLatin1String("accent"))
            s.accent = color();
        else if (key == QLatin1String("background"))
            s.background = color();
        else if (key == QLatin1String("foreground"))
            s.foreground = color();
        else if (key == QLatin1String("mode"))
            mode = val.toLower();
    }
    if (mode == QLatin1String("dark"))
        s.dark = true;
    else if (mode == QLatin1String("light"))
        s.dark = false;
    else if (s.background.isValid())
        s.dark = s.background.lightness() < 128;
    if (s.accent.isValid() || s.background.isValid() || s.foreground.isValid())
        s.source = QStringLiteral("omarchy");
    return s;
}

QColor readableOn(const QColor &bg, const QColor &light, const QColor &dark)
{
    // Prefer the theme's own colours when they are legible (WCAG AA for large
    // text and icons is 3:1); otherwise take the best of all candidates.
    QColor best;
    double bestC = 0;
    for (const QColor &c : {light, dark}) {
        if (!c.isValid())
            continue;
        const double k = contrast(bg, c);
        if (k > bestC)
            bestC = k, best = c;
    }
    if (bestC >= 3.0)
        return best;
    for (const QColor &c : {QColor(Qt::white), QColor(Qt::black)}) {
        const double k = contrast(bg, c);
        if (k > bestC)
            bestC = k, best = c;
    }
    return best;
}

MenuPalette resolveMenuPalette(const SystemThemeState &system, const QString &themeSetting, const QString &accentSetting)
{
    MenuPalette p;
    const bool sysDark = system.dark.value_or(true);
    p.dark = themeSetting == QLatin1String("dark") ? true : themeSetting == QLatin1String("light") ? false : sysDark;
    // The desktop's own surface colours only fit when the mode matches theirs.
    const bool useSystem = system.background.isValid() && system.foreground.isValid() && p.dark == sysDark;
    p.background = useSystem ? system.background : p.dark ? kDarkBackground : kLightBackground;
    p.foreground = useSystem ? system.foreground : p.dark ? kDarkForeground : kLightForeground;
    p.foregroundDim = mix(p.foreground, p.background, 0.62);

    const QColor custom(accentSetting);
    if (accentSetting.startsWith(QLatin1Char('#')) && custom.isValid())
        p.accent = custom;
    else if (system.accent.isValid())
        p.accent = system.accent;
    else
        p.accent = kDefaultAccent;
    p.accentForeground = readableOn(p.accent, p.dark ? p.foreground : p.background, p.dark ? p.background : p.foreground);
    p.radius = system.radius >= 0 ? system.radius : 12;
    const bool forced = themeSetting == QLatin1String("dark") || themeSetting == QLatin1String("light");
    const bool fromSystem = useSystem || !forced || (system.accent.isValid() && p.accent == system.accent);
    p.source = fromSystem && !system.source.isEmpty() ? system.source : QStringLiteral("builtin");
    return p;
}

// ---------------------------------------------------------------- SystemTheme

QString SystemTheme::defaultOmarchyThemeDir()
{
    return QDir::homePath() + QStringLiteral("/.local/state/omarchy/current/theme");
}

SystemTheme::SystemTheme(QObject *parent) : SystemTheme(Options{}, parent) {}

SystemTheme::SystemTheme(const Options &options, QObject *parent) : QObject(parent), m_opt(options)
{
    // A theme switch touches several files and links in a burst; read once.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        reloadOmarchy();
        rearm();
        readHyprRounding();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    reloadOmarchy();
    rearm();
    readHyprRounding();
    if (m_opt.usePortal) {
        QDBusConnection::sessionBus().connect(kPortalService, kPortalPath, kPortalSettings, QStringLiteral("SettingChanged"), this,
                                              SLOT(onPortalSettingChanged(QString, QString, QDBusVariant)));
        readPortal();
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (qGuiApp)
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
            if (!m_omarchy && !m_portalDark)
                bump();
        });
#endif
}

SystemThemeState SystemTheme::state() const
{
    SystemThemeState s;
    if (m_omarchy) {
        s = *m_omarchy;
    } else {
        if (m_portalDark) {
            s.dark = m_portalDark;
            s.source = QStringLiteral("portal");
        }
        if (m_portalAccent.isValid()) {
            s.accent = m_portalAccent;
            s.source = QStringLiteral("portal");
        }
        if (!s.dark && qGuiApp) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
            const auto scheme = QGuiApplication::styleHints()->colorScheme();
            if (scheme != Qt::ColorScheme::Unknown)
                s.dark = scheme == Qt::ColorScheme::Dark;
#endif
            if (!s.dark)
                s.dark = QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
            if (s.source.isEmpty())
                s.source = QStringLiteral("qt");
        }
    }
    s.radius = m_hyprRadius;
    return s;
}

void SystemTheme::bump()
{
    ++m_revision;
    Q_EMIT changed();
}

void SystemTheme::reloadOmarchy()
{
    std::optional<SystemThemeState> next;
    QFile f(m_opt.omarchyThemeDir + QStringLiteral("/colors.toml"));
    if (!m_opt.omarchyThemeDir.isEmpty() && f.open(QIODevice::ReadOnly)) {
        auto s = parseOmarchyColors(QString::fromUtf8(f.read(64 * 1024)));
        if (!s.source.isEmpty())
            next = s;
    }
    const bool same = next.has_value() == m_omarchy.has_value()
                      && (!next || (next->dark == m_omarchy->dark && next->accent == m_omarchy->accent
                                    && next->background == m_omarchy->background && next->foreground == m_omarchy->foreground));
    m_omarchy = next;
    if (!same)
        bump();
}

void SystemTheme::rearm()
{
    if (m_opt.omarchyThemeDir.isEmpty())
        return;
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    // theme/colors.toml, theme/, current/ and omarchy/: switching themes
    // replaces `current/theme`, so watching only the file would go deaf.
    const QString theme = m_opt.omarchyThemeDir;
    const QString current = QFileInfo(theme).path();
    const QString base = QFileInfo(current).path();
    if (!QFileInfo(base).isDir())
        return; // not an Omarchy system; nothing to follow
    QStringList paths;
    for (const QString &p : {theme + QStringLiteral("/colors.toml"), theme, current, base})
        if (QFileInfo::exists(p))
            paths << p;
    if (!paths.isEmpty())
        m_watcher.addPaths(paths);
}

void SystemTheme::readPortal()
{
    for (const QString &key : {QStringLiteral("color-scheme"), QStringLiteral("accent-color")}) {
        auto msg = QDBusMessage::createMethodCall(kPortalService, kPortalPath, kPortalSettings, QStringLiteral("ReadOne"));
        msg << kAppearance << key;
        auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 2000), this);
        connect(w, &QDBusPendingCallWatcher::finished, this, [this, key](QDBusPendingCallWatcher *w) {
            w->deleteLater();
            const QDBusMessage r = w->reply();
            if (r.type() == QDBusMessage::ReplyMessage && !r.arguments().isEmpty()) {
                applyPortalValue(key, r.arguments().constFirst());
                return;
            }
            if (r.errorName() != QLatin1String("org.freedesktop.DBus.Error.UnknownMethod"))
                return; // no portal, or the key is not provided
            // Portal versions before 2 only have the deprecated Read.
            auto old = QDBusMessage::createMethodCall(kPortalService, kPortalPath, kPortalSettings, QStringLiteral("Read"));
            old << kAppearance << key;
            auto *w2 = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(old, 2000), this);
            connect(w2, &QDBusPendingCallWatcher::finished, this, [this, key](QDBusPendingCallWatcher *w2) {
                w2->deleteLater();
                const QDBusMessage r2 = w2->reply();
                if (r2.type() == QDBusMessage::ReplyMessage && !r2.arguments().isEmpty())
                    applyPortalValue(key, r2.arguments().constFirst());
            });
        });
    }
}

void SystemTheme::onPortalSettingChanged(const QString &ns, const QString &key, const QDBusVariant &value)
{
    if (ns == kAppearance)
        applyPortalValue(key, value.variant());
}

void SystemTheme::applyPortalValue(const QString &key, const QVariant &raw)
{
    const QVariant v = unwrap(raw);
    if (key == QLatin1String("color-scheme")) {
        // 0 = no preference, 1 = prefer dark, 2 = prefer light
        const uint scheme = v.toUInt();
        const std::optional<bool> next = scheme == 1 ? std::optional<bool>(true) : scheme == 2 ? std::optional<bool>(false) : std::nullopt;
        if (next != m_portalDark) {
            m_portalDark = next;
            bump();
        }
    } else if (key == QLatin1String("accent-color")) {
        // (ddd) sRGB in 0..1; anything outside means "not set"
        QColor next;
        if (v.userType() == qMetaTypeId<QDBusArgument>()) {
            const auto arg = v.value<QDBusArgument>();
            double r = -1, g = -1, b = -1;
            arg.beginStructure();
            arg >> r >> g >> b;
            arg.endStructure();
            if (r >= 0 && r <= 1 && g >= 0 && g <= 1 && b >= 0 && b <= 1)
                next = QColor::fromRgbF(float(r), float(g), float(b));
        }
        if (next != m_portalAccent) {
            m_portalAccent = next;
            bump();
        }
    }
}

void SystemTheme::readHyprRounding()
{
    if (!m_opt.useHyprland || qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
        return;
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("hyprctl"));
    if (exe.isEmpty())
        return;
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus st) {
        p->deleteLater();
        if (st != QProcess::NormalExit || code != 0)
            return;
        const auto o = QJsonDocument::fromJson(p->readAllStandardOutput()).object();
        if (!o.contains(QLatin1String("int")))
            return;
        const int r = qBound(0, o.value(QLatin1String("int")).toInt(), 64);
        if (r != m_hyprRadius) {
            m_hyprRadius = r;
            bump();
        }
    });
    connect(p, &QProcess::errorOccurred, p, &QObject::deleteLater);
    QTimer::singleShot(500, p, [p] { p->kill(); });
    p->start(exe, {QStringLiteral("-j"), QStringLiteral("getoption"), QStringLiteral("decoration:rounding")});
}

} // namespace opendial
