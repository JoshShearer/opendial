// SPDX-License-Identifier: MIT
#include "Config.h"
#include "Profiles.h"

#include <QtTest>

using namespace opendial;

namespace {
QJsonObject prof(const QString &id, const QJsonObject &match, int priority = 0, const QJsonArray &tools = {})
{
    return QJsonObject{{"id", id}, {"name", id}, {"match", match}, {"priority", priority}, {"tools", tools}, {"menuMode", "extend"}};
}
QJsonObject tool(const QString &id, bool enabled = true)
{
    return QJsonObject{{"id", id}, {"name", id}, {"enabled", enabled}};
}
} // namespace

class TestProfiles : public QObject {
    Q_OBJECT
private:
    QJsonArray m_profiles;

private Q_SLOTS:
    void init()
    {
        m_profiles = QJsonArray{
            prof("global", {{"kind", "global"}}, 0, QJsonArray{tool("volume"), tool("scroll"), tool("off", false)}),
            prof("ws-code", {{"kind", "workspace"}, {"workspaces", QJsonArray{"3"}}}),
            prof("krita", {{"kind", "app"}, {"appIds", QJsonArray{"org.kde.krita"}}}, 0, QJsonArray{tool("brush"), tool("volume")}),
            prof("krita2", {{"kind", "app"}, {"appIds", QJsonArray{"krita"}}}, 5),
            prof("krita-dlg", {{"kind", "window"}, {"appIds", QJsonArray{"krita"}}, {"titleRegex", "^Export"}}),
            prof("browsers", {{"kind", "app"}, {"appIds", QJsonArray{"*fox*"}}}),
            prof("manual", {{"kind", "manual"}}),
        };
    }

    void globalFallback()
    {
        FocusContext c;
        c.appId = "unknown-app";
        auto r = resolveProfile(m_profiles, c, {});
        QCOMPARE(r.profileId, QStringLiteral("global"));
        QCOMPARE(r.layer, ProfileLayer::Global);
    }

    void appBeatsWorkspace()
    {
        FocusContext c;
        c.appId = "org.kde.krita";
        c.workspace = "3";
        auto r = resolveProfile(m_profiles, c, {});
        // krita2 has higher priority within the app layer and matches the
        // reverse-DNS short name
        QCOMPARE(r.profileId, QStringLiteral("krita2"));
        QCOMPARE(r.layer, ProfileLayer::App);
        QCOMPARE(r.candidates, (QStringList{"krita2", "krita", "ws-code", "global"}));
    }

    void workspaceBeatsGlobal()
    {
        FocusContext c;
        c.appId = "kitty";
        c.workspace = "3";
        QCOMPARE(resolveProfile(m_profiles, c, {}).profileId, QStringLiteral("ws-code"));
    }

    void windowBeatsApp()
    {
        FocusContext c;
        c.appId = "krita";
        c.title = "Export Layers";
        auto r = resolveProfile(m_profiles, c, {});
        QCOMPARE(r.profileId, QStringLiteral("krita-dlg"));
        QCOMPARE(r.layer, ProfileLayer::Window);
    }

    void lockBeatsEverything()
    {
        FocusContext c;
        c.appId = "krita";
        c.title = "Export";
        auto r = resolveProfile(m_profiles, c, "manual");
        QCOMPARE(r.profileId, QStringLiteral("manual"));
        QCOMPARE(r.layer, ProfileLayer::Lock);
    }

    void manualNeverAutoMatches()
    {
        FocusContext c;
        c.appId = "manual";
        QCOMPARE(resolveProfile(m_profiles, c, {}).profileId, QStringLiteral("global"));
    }

    void flatpakAndDesktopSuffix()
    {
        FocusContext c;
        c.appId = "";
        c.flatpakId = "org.kde.krita";
        QVERIFY(resolveProfile(m_profiles, c, {}).profileId.startsWith("krita"));
        FocusContext d;
        d.appId = "org.kde.krita.desktop";
        QVERIFY(resolveProfile(m_profiles, d, {}).profileId.startsWith("krita"));
    }

    void globMatch()
    {
        FocusContext c;
        c.appId = "Firefox";
        QCOMPARE(resolveProfile(m_profiles, c, {}).profileId, QStringLiteral("browsers"));
    }

    void titleSubstringAloneNeverMatchesApp()
    {
        FocusContext c;
        c.appId = "kitty";
        c.title = "krita - Export";
        QCOMPARE(resolveProfile(m_profiles, c, {}).profileId, QStringLiteral("global"));
    }

    void disabledProfileSkipped()
    {
        auto p = m_profiles;
        auto k = p.at(3).toObject();
        k.insert("enabled", false);
        p.replace(3, k);
        FocusContext c;
        c.appId = "krita";
        QCOMPARE(resolveProfile(p, c, {}).profileId, QStringLiteral("krita"));
    }

    void composeExtendsAndOverrides()
    {
        auto tools = composeMenuTools(m_profiles, "krita");
        QStringList ids;
        for (const auto &t : tools)
            ids << t.toObject().value("id").toString();
        QCOMPARE(ids, (QStringList{"brush", "volume", "scroll"})); // 'off' disabled, volume deduped
        auto withPlugin = composeMenuTools(m_profiles, "global", QJsonArray{tool("plug")});
        QCOMPARE(withPlugin.size(), 3);
    }

    void defaultProfilesResolve()
    {
        const auto profiles = defaultConfig().value("profiles").toArray();
        FocusContext c;
        c.appId = "org.mozilla.firefox";
        QCOMPARE(resolveProfile(profiles, c, {}).profileId, QStringLiteral("browser"));
        c.appId = "mpv";
        QCOMPARE(resolveProfile(profiles, c, {}).profileId, QStringLiteral("media-players"));
    }

    void redactedContext()
    {
        FocusContext c;
        c.title = "secret document.pdf";
        QCOMPARE(c.toJson(true).value("title").toString(), QStringLiteral("<redacted>"));
        QCOMPARE(FocusContext::fromJson(c.toJson(false)).title, c.title);
    }
};

QTEST_GUILESS_MAIN(TestProfiles)
#include "test_profiles.moc"
