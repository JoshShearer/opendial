// SPDX-License-Identifier: MIT
// Integration: the real Engine (gestures → menu → tools → actions → profiles,
// approvals, plugins, config lifecycle) driven through its simulation API, the
// same path `opendialctl simulate` and replay fixtures use. No hardware,
// no injection (dry-run: actions are logged, never executed).
#include "Action.h"
#include "Engine.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace opendial;

namespace {
QJsonArray actionsSince(Engine &e, int from)
{
    QJsonArray out;
    const QJsonArray log = e.diagnostics(false).value(QStringLiteral("eventLog")).toArray();
    int i = 0;
    for (const auto &v : log)
        if (v.toObject().value(QStringLiteral("kind")).toString() == QLatin1String("action") && i++ >= from)
            out.append(v);
    return out;
}
int actionCount(Engine &e) { return actionsSince(e, 0).size(); }
} // namespace

class TestEngine : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    std::unique_ptr<Engine> m_engine;

    Engine &e() { return *m_engine; }
    void sim(const QJsonObject &cmd)
    {
        QString err;
        QVERIFY2(e().simulate(cmd, &err), qPrintable(err));
    }
    bool menuOpen() { return e().stateJson().value(QStringLiteral("menuOpen")).toBool(); }
    void longPress()
    {
        sim({{QStringLiteral("longPress"), true}});
        QTRY_VERIFY_WITH_TIMEOUT(menuOpen(), 2000);
        QTest::qWait(150); // release lands
    }
    void click()
    {
        sim({{QStringLiteral("click"), 1}});
        QTest::qWait(450); // > double-click window
    }

private Q_SLOTS:
    void init()
    {
        QVERIFY(m_dir.isValid());
        QDir(m_dir.path()).removeRecursively();
        QDir().mkpath(m_dir.path());
        EngineOptions o;
        o.noDevice = true;
        o.noContext = true;
        o.noInjection = true;
        o.configPath = m_dir.filePath(QStringLiteral("config.json"));
        o.statePath = m_dir.filePath(QStringLiteral("state.json"));
        m_engine = std::make_unique<Engine>(o);
        m_engine->start();
    }
    void cleanup() { m_engine.reset(); }

    void firstRunWritesDefaults()
    {
        QVERIFY(QFile::exists(m_dir.filePath(QStringLiteral("config.json"))));
        QCOMPARE(e().activeProfileId(), QStringLiteral("global"));
        QCOMPARE(e().activeToolId(), QStringLiteral("volume"));
        QVERIFY(e().stateJson().value(QStringLiteral("firstRun")).toBool());
        e().completeFirstRun();
        QVERIFY(!e().stateJson().value(QStringLiteral("firstRun")).toBool());
    }

    // Windows parity: press and hold opens the menu, turning highlights, click selects.
    void longPressRotateClickSelects()
    {
        QVERIFY(!menuOpen());
        longPress();
        const QJsonObject m = e().menuJson();
        QVERIFY(m.value(QStringLiteral("open")).toBool());
        QVERIFY(m.value(QStringLiteral("items")).toArray().size() >= 6);
        QCOMPARE(m.value(QStringLiteral("highlight")).toInt(), 0); // starts on the active tool
        sim({{QStringLiteral("rotate"), 45.0}});                    // 20° per item → two items on
        QCOMPARE(e().menuJson().value(QStringLiteral("highlight")).toInt(), 2);
        click();
        QTRY_VERIFY(!menuOpen());
        QCOMPARE(e().activeToolId(), QStringLiteral("zoom"));
    }

    void menuRotationBackwardsWraps()
    {
        longPress();
        sim({{QStringLiteral("rotate"), -25.0}});
        const int n = e().menuJson().value(QStringLiteral("items")).toArray().size();
        QCOMPARE(e().menuJson().value(QStringLiteral("highlight")).toInt(), n - 1);
    }

    void menuNoActionsWhileOpen()
    {
        longPress();
        const int before = actionCount(e());
        sim({{QStringLiteral("rotate"), 60.0}});
        QCOMPARE(actionCount(e()), before);
    }

    // Built-in tool: rotation produces clockwise/counter-clockwise actions by detent.
    void volumeToolRotates()
    {
        QCOMPARE(e().activeToolId(), QStringLiteral("volume"));
        const int before = actionCount(e());
        sim({{QStringLiteral("rotate"), 30.0}});  // 10° detents → 3 steps
        QTRY_VERIFY(actionsSince(e(), before).size() >= 1);
        int cw = 0;
        for (const auto &v : actionsSince(e(), before)) {
            const auto a = v.toObject();
            QCOMPARE(a.value(QStringLiteral("slot")).toString(), QStringLiteral("rotate_cw"));
            QVERIFY(a.value(QStringLiteral("action")).toString().startsWith(QStringLiteral("Volume")));
            cw += a.value(QStringLiteral("steps")).toInt();
        }
        QCOMPARE(cw, 3);
        const int mid = actionCount(e());
        sim({{QStringLiteral("rotate"), -10.0}});
        const auto back = actionsSince(e(), mid);
        QCOMPARE(back.size(), 1);
        QCOMPARE(back.first().toObject().value(QStringLiteral("slot")).toString(), QStringLiteral("rotate_ccw"));
    }

    void builtinToolsPresent()
    {
        QStringList ids;
        for (const auto &v : e().menuTools())
            ids << v.toObject().value(QStringLiteral("id")).toString();
        for (const char *t : {"volume", "scroll", "zoom", "undo", "media"})
            QVERIFY2(ids.contains(QLatin1String(t)), t);
        // brightness only when a backlight is controllable: listed in the config either way
        bool hasBrightness = false;
        for (const auto &v : e().config().value(QStringLiteral("profiles")).toArray().first().toObject().value(QStringLiteral("tools")).toArray())
            hasBrightness |= v.toObject().value(QStringLiteral("id")).toString() == QLatin1String("brightness");
        QVERIFY(hasBrightness);
    }

    // Custom tool with separate clockwise, counter-clockwise and click shortcuts.
    void customToolSeparateBindings()
    {
        QJsonObject cfg = e().config();
        QJsonArray profiles = cfg.value(QStringLiteral("profiles")).toArray();
        QJsonObject global = profiles.first().toObject();
        QJsonArray tools = global.value(QStringLiteral("tools")).toArray();
        tools.append(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("my-tool")},
            {QStringLiteral("name"), QStringLiteral("Brackets")},
            {QStringLiteral("icon"), QStringLiteral("custom")},
            {QStringLiteral("bindings"), QJsonObject{
                 {QStringLiteral("rotate_cw"), QJsonObject{{QStringLiteral("type"), QStringLiteral("key")}, {QStringLiteral("keys"), QStringLiteral("bracketright")}}},
                 {QStringLiteral("rotate_ccw"), QJsonObject{{QStringLiteral("type"), QStringLiteral("key")}, {QStringLiteral("keys"), QStringLiteral("bracketleft")}}},
                 {QStringLiteral("click"), QJsonObject{{QStringLiteral("type"), QStringLiteral("key")}, {QStringLiteral("keys"), QStringLiteral("Ctrl+Shift+N")}}}}}});
        global.insert(QStringLiteral("tools"), tools);
        profiles.replace(0, global);
        cfg.insert(QStringLiteral("profiles"), profiles);
        QString err;
        const auto v = e().setConfig(cfg, &err);
        QVERIFY2(v.ok(), qPrintable(v.errors().join(QLatin1Char(';')) + err));
        QVERIFY(e().setActiveTool(QStringLiteral("my-tool")));

        int at = actionCount(e());
        sim({{QStringLiteral("rotate"), 10.0}});
        QCOMPARE(actionsSince(e(), at).first().toObject().value(QStringLiteral("action")).toString(), QStringLiteral("Key ]"));
        at = actionCount(e());
        sim({{QStringLiteral("rotate"), -10.0}});
        QCOMPARE(actionsSince(e(), at).first().toObject().value(QStringLiteral("action")).toString(), QStringLiteral("Key ["));
        at = actionCount(e());
        click();
        QTRY_COMPARE(actionsSince(e(), at).size(), 1);
        const auto c = actionsSince(e(), at).first().toObject();
        QCOMPARE(c.value(QStringLiteral("slot")).toString(), QStringLiteral("click"));
        QCOMPARE(c.value(QStringLiteral("action")).toString(), QStringLiteral("Key Ctrl+Shift+N"));
    }

    // Per-app tools: focusing Krita switches to its profile and default tool.
    void appProfileSwitching()
    {
        QVERIFY(e().simulateContext({{QStringLiteral("appId"), QStringLiteral("org.kde.krita")}}));
        QTRY_COMPARE(e().activeProfileId(), QStringLiteral("krita"));
        QCOMPARE(e().activeToolId(), QStringLiteral("krita-brush"));
        QStringList ids;
        for (const auto &v : e().menuTools())
            ids << v.toObject().value(QStringLiteral("id")).toString();
        QVERIFY(ids.contains(QStringLiteral("krita-brush")));
        QVERIFY(ids.contains(QStringLiteral("volume"))); // global tools stay available
        QVERIFY(e().simulateContext({{QStringLiteral("appId"), QStringLiteral("firefox")}}));
        QTRY_COMPARE(e().activeProfileId(), QStringLiteral("browser"));
        QVERIFY(e().simulateContext({{QStringLiteral("appId"), QStringLiteral("some.unknown.App")}}));
        QTRY_COMPARE(e().activeProfileId(), QStringLiteral("global"));
    }

    void rememberToolPerApp()
    {
        QVERIFY(e().simulateContext({{QStringLiteral("appId"), QStringLiteral("krita")}}));
        QTRY_COMPARE(e().activeProfileId(), QStringLiteral("krita"));
        QVERIFY(e().setActiveTool(QStringLiteral("krita-opacity")));
        QVERIFY(e().simulateContext({{QStringLiteral("appId"), QStringLiteral("foo")}}));
        QTRY_COMPARE(e().activeProfileId(), QStringLiteral("global"));
        QVERIFY(e().simulateContext({{QStringLiteral("appId"), QStringLiteral("krita")}}));
        QTRY_COMPARE(e().activeToolId(), QStringLiteral("krita-opacity"));
    }

    void lockedProfileIgnoresFocus()
    {
        QVERIFY(e().lockProfile(QStringLiteral("krita")));
        QVERIFY(e().simulateContext({{QStringLiteral("appId"), QStringLiteral("firefox")}}));
        QTest::qWait(50);
        QCOMPARE(e().activeProfileId(), QStringLiteral("krita"));
        e().unlockProfile();
        QTRY_COMPARE(e().activeProfileId(), QStringLiteral("browser"));
    }

    void pauseSuppressesActions()
    {
        e().setPaused(true);
        const int before = actionCount(e());
        sim({{QStringLiteral("rotate"), 30.0}});
        QCOMPARE(actionCount(e()), before);
        e().setPaused(false);
        sim({{QStringLiteral("rotate"), 10.0}});
        QCOMPARE(actionCount(e()), before + 1);
    }

    void invalidConfigRejected()
    {
        const QJsonObject good = e().config();
        QJsonObject bad = good;
        bad.insert(QStringLiteral("profiles"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("x")},
                                                                        {QStringLiteral("tools"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("t")},
                                                                            {QStringLiteral("bindings"), QJsonObject{{QStringLiteral("rotate_cw"), QJsonObject{{QStringLiteral("type"), QStringLiteral("rm-rf")}}}}}}}}}});
        QString err;
        const auto v = e().setConfig(bad, &err);
        QVERIFY(!v.ok());
        QCOMPARE(e().config(), good);
    }

    void rollbackRestoresPrevious()
    {
        QJsonObject cfg = e().config();
        QJsonObject s = cfg.value(QStringLiteral("settings")).toObject();
        s.insert(QStringLiteral("handedness"), QStringLiteral("left"));
        cfg.insert(QStringLiteral("settings"), s);
        QString err;
        QVERIFY(e().setConfig(cfg, &err).ok());
        QCOMPARE(e().settings().value(QStringLiteral("handedness")).toString(), QStringLiteral("left"));
        QVERIFY(e().rollbackConfig(&err) >= 0);
        QCOMPARE(e().settings().value(QStringLiteral("handedness")).toString(), QStringLiteral("right"));
    }

    // Commands never run until the user approves the exact action.
    void riskyActionNeedsApproval()
    {
        const QJsonObject cmd{{QStringLiteral("type"), QStringLiteral("command")},
                              {QStringLiteral("argv"), QJsonArray{QStringLiteral("notify-send"), QStringLiteral("hi")}}};
        QJsonObject cfg = e().config();
        QJsonArray profiles = cfg.value(QStringLiteral("profiles")).toArray();
        QJsonObject global = profiles.first().toObject();
        QJsonArray tools = global.value(QStringLiteral("tools")).toArray();
        tools.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("runner")}, {QStringLiteral("name"), QStringLiteral("Runner")},
                                 {QStringLiteral("bindings"), QJsonObject{{QStringLiteral("click"), cmd}}}});
        global.insert(QStringLiteral("tools"), tools);
        profiles.replace(0, global);
        cfg.insert(QStringLiteral("profiles"), profiles);
        QString err;
        QVERIFY(e().setConfig(cfg, &err).ok());
        QVERIFY(e().setActiveTool(QStringLiteral("runner")));

        const QString fp = actionFingerprint(cmd);
        int at = actionCount(e());
        click();
        QTRY_COMPARE(actionsSince(e(), at).size(), 1);
        QCOMPARE(actionsSince(e(), at).first().toObject().value(QStringLiteral("ok")).toBool(), false);
        bool listed = false;
        for (const auto &v : e().pendingApprovals())
            listed |= v.toObject().value(QStringLiteral("fingerprint")).toString() == fp;
        QVERIFY(listed);

        QVERIFY(e().approve(fp));
        at = actionCount(e());
        click();
        QTRY_COMPARE(actionsSince(e(), at).size(), 1);
        QVERIFY(actionsSince(e(), at).first().toObject().value(QStringLiteral("ok")).toBool());

        QVERIFY(e().revokeApproval(fp));
        QVERIFY(!e().isApproved(fp));
    }

    void pluginToolsNeedConsent()
    {
        const QJsonObject reg{{QStringLiteral("appId"), QStringLiteral("org.example.paint")},
                              {QStringLiteral("name"), QStringLiteral("Paint")},
                              {QStringLiteral("tools"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("size")}, {QStringLiteral("name"), QStringLiteral("Brush size")}}}}};
        QVERIFY(e().registerApp(reg, QStringLiteral(":1.99")).value(QStringLiteral("ok")).toBool());
        auto has = [this] {
            for (const auto &v : e().menuTools())
                if (v.toObject().value(QStringLiteral("id")).toString() == QLatin1String("plugin:org.example.paint:size"))
                    return true;
            return false;
        };
        QVERIFY(!has());
        QVERIFY(e().approvePlugin(QStringLiteral("org.example.paint"), true));
        QTRY_VERIFY(has());

        QSignalSpy spy(&e(), &Engine::pluginEvent);
        QVERIFY(e().setActiveTool(QStringLiteral("plugin:org.example.paint:size")));
        sim({{QStringLiteral("rotate"), 10.0}});
        QTRY_VERIFY(spy.count() >= 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("org.example.paint"));

        e().busNameVanished(QStringLiteral(":1.99"));
        QTRY_VERIFY(!has());
        QVERIFY(e().activeToolId() != QLatin1String("plugin:org.example.paint:size"));
    }

    void stateSurvivesRestart()
    {
        QVERIFY(e().setActiveTool(QStringLiteral("scroll")));
        QVERIFY(e().lockProfile(QStringLiteral("krita")));
        e().unlockProfile();
        QVERIFY(e().lockProfile(QStringLiteral("global")));
        m_engine.reset();
        EngineOptions o;
        o.noDevice = o.noContext = o.noInjection = true;
        o.configPath = m_dir.filePath(QStringLiteral("config.json"));
        o.statePath = m_dir.filePath(QStringLiteral("state.json"));
        m_engine = std::make_unique<Engine>(o);
        m_engine->start();
        QCOMPARE(e().activeToolId(), QStringLiteral("scroll"));
        QCOMPARE(e().stateJson().value(QStringLiteral("lockedProfile")).toString(), QStringLiteral("global"));
    }

    void doubleClickBindingOnlyWhenBound()
    {
        // Volume has no double-click binding: two quick clicks are two clicks (mute toggles twice).
        const int at = actionCount(e());
        sim({{QStringLiteral("click"), 2}});
        QTest::qWait(600);
        for (const auto &v : actionsSince(e(), at))
            QCOMPARE(v.toObject().value(QStringLiteral("slot")).toString(), QStringLiteral("click"));
    }

    void simulationRejectsGarbage()
    {
        QString err;
        QVERIFY(!e().simulate({{QStringLiteral("rotate"), 1e9}}, &err));
        QVERIFY(!e().simulate({{QStringLiteral("button"), QStringLiteral("sideways")}}, &err));
        QVERIFY(!e().simulate({{QStringLiteral("nope"), 1}}, &err));
    }
};

QTEST_MAIN(TestEngine)
#include "test_engine.moc"
