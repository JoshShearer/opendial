// SPDX-License-Identifier: MIT
#include "SystemTheme.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace opendial;

class TestTheme : public QObject {
    Q_OBJECT

    static void write(const QString &path, const QByteArray &data)
    {
        QDir().mkpath(QFileInfo(path).path());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(data);
    }

    static SystemTheme::Options opts(const QString &dir)
    {
        SystemTheme::Options o;
        o.omarchyThemeDir = dir;
        o.usePortal = false;
        o.useHyprland = false;
        return o;
    }

private Q_SLOTS:
    void parsesOmarchyColors()
    {
        const auto s = parseOmarchyColors(QStringLiteral(
            "# Omarchy theme\n"
            "mode = \"dark\"\n"
            "accent = \"#b59790\"\n"
            "background = '#0c0b0c'\n"
            "  foreground   =   \"#FAFCFB\"   # trailing comment\n"
            "color0 = \"#000000\"\n"
            "selection = \"not a colour\"\n"));
        QCOMPARE(s.source, QStringLiteral("omarchy"));
        QVERIFY(s.dark.has_value() && *s.dark);
        QCOMPARE(s.accent, QColor("#b59790"));
        QCOMPARE(s.background, QColor("#0c0b0c"));
        QCOMPARE(s.foreground, QColor("#fafcfb"));
    }

    void infersModeFromBackground()
    {
        auto s = parseOmarchyColors(QStringLiteral("background = \"#f5f5f0\"\nforeground = \"#222222\"\n"));
        QVERIFY(s.dark.has_value() && !*s.dark);
        s = parseOmarchyColors(QStringLiteral("garbage\n[section]\n"));
        QVERIFY(s.source.isEmpty());
        QVERIFY(!s.dark.has_value());
        // named colours are not hex: ignored like Omarchy's own apps do
        s = parseOmarchyColors(QStringLiteral("accent = \"red\"\n"));
        QVERIFY(!s.accent.isValid());
    }

    void paletteFollowsSystem()
    {
        SystemThemeState sys;
        sys.source = QStringLiteral("omarchy");
        sys.dark = true;
        sys.accent = QColor("#b59790");
        sys.background = QColor("#0c0b0c");
        sys.foreground = QColor("#fafcfb");
        sys.radius = 0;

        auto p = resolveMenuPalette(sys, QStringLiteral("system"), QStringLiteral("system"));
        QVERIFY(p.dark);
        QCOMPARE(p.background, sys.background);
        QCOMPARE(p.foreground, sys.foreground);
        QCOMPARE(p.accent, sys.accent);
        QCOMPARE(p.radius, 0);
        QCOMPARE(p.source, QStringLiteral("omarchy"));
        // light accent on a dark theme: the theme background reads best on it
        QCOMPARE(p.accentForeground, sys.background);

        // explicit accent wins; forced light ignores the dark surfaces
        p = resolveMenuPalette(sys, QStringLiteral("light"), QStringLiteral("#2fb37b"));
        QVERIFY(!p.dark);
        QVERIFY(p.background.lightness() > 200);
        QCOMPARE(p.accent, QColor("#2fb37b"));
        QCOMPARE(p.source, QStringLiteral("builtin"));

        // forced dark on a dark theme keeps the theme's own surfaces
        p = resolveMenuPalette(sys, QStringLiteral("dark"), QStringLiteral("system"));
        QCOMPARE(p.background, sys.background);
    }

    void paletteForPortalLightDark()
    {
        // GNOME/Ubuntu: only light/dark (and maybe an accent) is known
        SystemThemeState sys;
        sys.source = QStringLiteral("portal");
        sys.dark = false;
        auto p = resolveMenuPalette(sys, QStringLiteral("system"), QStringLiteral("system"));
        QVERIFY(!p.dark);
        QVERIFY(p.background.lightness() > 200);
        QVERIFY(p.foreground.lightness() < 60);
        QCOMPARE(p.accent, QColor("#4c8dff"));
        QCOMPARE(p.radius, 12);
        sys.dark = true;
        sys.accent = QColor("#e66100");
        p = resolveMenuPalette(sys, QStringLiteral("system"), QStringLiteral("system"));
        QVERIFY(p.dark);
        QVERIFY(p.background.lightness() < 60);
        QCOMPARE(p.accent, QColor("#e66100"));
    }

    void readableAccentForeground()
    {
        QCOMPARE(readableOn(QColor("#ffd60a")), QColor(Qt::black));
        QCOMPARE(readableOn(QColor("#1b3a8a")), QColor(Qt::white));
        // theme candidates that are too weak fall back to black/white
        QCOMPARE(readableOn(QColor("#777777"), QColor("#808080"), QColor("#707070")), QColor(Qt::black));
    }

    void followsThemeSwitchLive()
    {
        // Same layout as Omarchy: omarchy/current/theme -> themes/<name>,
        // switched by replacing the `theme` symlink.
        QTemporaryDir tmp;
        const QString base = tmp.path() + QStringLiteral("/omarchy");
        write(base + QStringLiteral("/themes/one/colors.toml"), "mode = \"dark\"\naccent = \"#b59790\"\nbackground = \"#0c0b0c\"\nforeground = \"#fafcfb\"\n");
        write(base + QStringLiteral("/themes/two/colors.toml"), "mode = \"light\"\naccent = \"#1e66f5\"\nbackground = \"#eff1f5\"\nforeground = \"#4c4f69\"\n");
        QDir().mkpath(base + QStringLiteral("/current"));
        QVERIFY(QFile::link(base + QStringLiteral("/themes/one"), base + QStringLiteral("/current/theme")));

        SystemTheme theme(opts(base + QStringLiteral("/current/theme")));
        QCOMPARE(theme.source(), QStringLiteral("omarchy"));
        QCOMPARE(theme.palette("system", "system").accent, QColor("#b59790"));

        QSignalSpy spy(&theme, &SystemTheme::changed);
        QFile::remove(base + QStringLiteral("/current/theme"));
        QVERIFY(QFile::link(base + QStringLiteral("/themes/two"), base + QStringLiteral("/current/theme")));
        QTRY_VERIFY_WITH_TIMEOUT(theme.palette("system", "system").accent == QColor("#1e66f5"), 3000);
        QVERIFY(spy.count() >= 1);
        QVERIFY(!theme.palette("system", "system").dark);

        // and again: the watcher must have been re-armed on the new target
        write(base + QStringLiteral("/themes/two/colors.toml"), "mode = \"light\"\naccent = \"#40a02b\"\nbackground = \"#eff1f5\"\nforeground = \"#4c4f69\"\n");
        QTRY_VERIFY_WITH_TIMEOUT(theme.palette("system", "system").accent == QColor("#40a02b"), 3000);
    }

    void noOmarchyMeansFallback()
    {
        QTemporaryDir tmp;
        SystemTheme theme(opts(tmp.path() + QStringLiteral("/nothing/current/theme")));
        QVERIFY(theme.source() != QStringLiteral("omarchy"));
        const auto p = theme.palette("dark", "system");
        QVERIFY(p.dark);
        QCOMPARE(p.accent, QColor("#4c8dff"));
    }
};

QTEST_MAIN(TestTheme)
#include "test_theme.moc"
