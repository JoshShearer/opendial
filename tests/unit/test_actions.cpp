// SPDX-License-Identifier: MIT
#include "Action.h"

#include <QtTest>

using namespace opendial;

namespace {
bool valid(const QJsonObject &a)
{
    ValidationIssues is;
    validateAction(a, QStringLiteral("a"), is);
    for (const auto &i : is)
        if (!i.warning)
            return false;
    return true;
}
} // namespace

class TestActions : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void validActions()
    {
        QVERIFY(valid({{"type", "key"}, {"keys", "Ctrl+Z"}}));
        QVERIFY(valid({{"type", "volume"}, {"delta", 2}}));
        QVERIFY(valid({{"type", "scroll"}, {"axis", "horizontal"}, {"amount", -1}}));
        QVERIFY(valid({{"type", "media"}, {"command", "seek"}, {"offsetMs", 5000}}));
        QVERIFY(valid({{"type", "command"}, {"argv", QJsonArray{"notify-send", "hi"}}}));
        QVERIFY(valid({{"type", "launch"}, {"url", "https://example.org"}}));
        QVERIFY(valid({{"type", "osc"}, {"host", "127.0.0.1"}, {"port", 9000}, {"address", "/dial"}}));
        QVERIFY(valid({{"type", "macro"}, {"steps", QJsonArray{QJsonObject{{"action", QJsonObject{{"type", "key"}, {"keys", "a"}}}}, QJsonObject{{"delayMs", 50}}}}}));
        QVERIFY(valid({{"type", "value"}, {"variable", "brush"}, {"step", 1}, {"min", 0}, {"max", 100}}));
        QVERIFY(valid({{"type", "if"}, {"condition", QJsonObject{{"appId", QJsonArray{"krita"}}}}, {"then", QJsonObject{{"type", "none"}}}}));
    }

    void invalidActions()
    {
        QVERIFY(!valid({{"type", "bogus"}}));
        QVERIFY(!valid({{"keys", "a"}}));
        QVERIFY(!valid({{"type", "key"}, {"keys", "Ctrl+Nope"}}));
        QVERIFY(!valid({{"type", "volume"}}));
        QVERIFY(!valid({{"type", "volume"}, {"delta", 500}}));
        QVERIFY(!valid({{"type", "command"}, {"argv", "rm -rf ~"}})); // shell strings rejected
        for (const char *o : {"coalesce", "reject", "queue"})
            QVERIFY(valid({{"type", "command"}, {"argv", QJsonArray{"x"}}, {"overlap", o}}));
        QVERIFY(!valid({{"type", "command"}, {"argv", QJsonArray{"x"}}, {"overlap", "parallel"}}));
        QVERIFY(!valid({{"type", "command"}, {"argv", QJsonArray{"x"}}, {"overlap", 3}}));
        QVERIFY(!valid({{"type", "launch"}, {"url", "file:///etc/passwd"}}));
        QVERIFY(!valid({{"type", "launch"}, {"url", "https://x"}, {"path", "/bin/sh"}}));
        QVERIFY(!valid({{"type", "osc"}, {"host", "h"}, {"port", 1}, {"address", "nope"}}));
        QVERIFY(!valid({{"type", "value"}, {"variable", "1bad"}, {"step", 1}}));
        QVERIFY(!valid({{"type", "macro"}, {"steps", QJsonArray{QJsonObject{{"delayMs", 9000}}, QJsonObject{{"delayMs", 9000}}}}, {"repeat", 2}}));
    }

    void depthBounded()
    {
        QJsonObject a{{"type", "none"}};
        for (int i = 0; i < 8; ++i)
            a = QJsonObject{{"type", "macro"}, {"steps", QJsonArray{QJsonObject{{"action", a}}}}};
        QVERIFY(!valid(a));
    }

    void risk()
    {
        QCOMPARE(actionRisk({{"type", "key"}, {"keys", "a"}}), Risk::Safe);
        QCOMPARE(actionRisk({{"type", "command"}, {"argv", QJsonArray{"x"}}}), Risk::NeedsApproval);
        QCOMPARE(actionRisk({{"type", "dbus"}}), Risk::NeedsApproval);
        QCOMPARE(actionRisk({{"type", "launch"}, {"url", "https://a"}}), Risk::NeedsApproval);
        QCOMPARE(actionRisk({{"type", "osc"}, {"host", "127.0.0.1"}}), Risk::Safe);
        QCOMPARE(actionRisk({{"type", "osc"}, {"host", "10.0.0.5"}}), Risk::NeedsApproval);
        const QJsonObject nested{{"type", "macro"}, {"steps", QJsonArray{QJsonObject{{"action", QJsonObject{{"type", "command"}, {"argv", QJsonArray{"x"}}}}}}}};
        QCOMPARE(actionRisk(nested), Risk::NeedsApproval);
        QCOMPARE(actionRisk({{"type", "if"}, {"then", QJsonObject{{"type", "launch"}, {"desktopId", "x"}}}}), Risk::NeedsApproval);
    }

    void fingerprintStableAndSensitive()
    {
        QJsonObject a{{"type", "command"}, {"argv", QJsonArray{"echo", "1"}}};
        QJsonObject b{{"argv", QJsonArray{"echo", "1"}}, {"type", "command"}};
        QCOMPARE(actionFingerprint(a), actionFingerprint(b));
        QJsonObject c{{"type", "command"}, {"argv", QJsonArray{"echo", "2"}}};
        QVERIFY(actionFingerprint(a) != actionFingerprint(c));
        QCOMPARE(actionFingerprint(a).size(), 64);
    }

    void collectRisky()
    {
        QJsonObject profile{{"tools", QJsonArray{QJsonObject{{"id", "t"}, {"bindings", QJsonObject{
            {"click", QJsonObject{{"type", "command"}, {"argv", QJsonArray{"x"}}}},
            {"rotate_cw", QJsonObject{{"type", "key"}, {"keys", "a"}}}}}}}}};
        QList<QPair<QString, QJsonObject>> out;
        collectRiskyActions(profile, QStringLiteral("p"), out);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.first().first, QStringLiteral("p.tools[0].bindings.click"));
    }

    // What the user agrees to: every risky action inside, in full.
    void describeApproval()
    {
        const QString longArg(200, QLatin1Char('x'));
        const QJsonObject cmd{{"type", "command"}, {"argv", QJsonArray{"run", "{value}", longArg}}};
        const QString run = QStringLiteral("Run run {value} ") + longArg;
        QCOMPARE(describeForApproval(cmd), run);
        QCOMPARE(describeForApproval({{"type", "value"}, {"variable", "v"}, {"step", 1}, {"min", 5}, {"max", 20}, {"output", cmd}}),
                 QStringLiteral("Value v (5 to 20): ") + run);
        QCOMPARE(describeForApproval({{"type", "value"}, {"variable", "v"}, {"step", 1}, {"output", cmd}}),
                 QStringLiteral("Value v (0 to 100): ") + run); // the defaults runValue() uses
        const QJsonObject open{{"type", "launch"}, {"url", "https://a"}};
        const QJsonObject cond{{"type", "if"}, {"condition", QJsonObject()}, {"then", cmd}, {"else", open}};
        QCOMPARE(describeForApproval(cond), QStringLiteral("Conditional: ") + run + QStringLiteral("; otherwise Open URL https://a"));
        const QJsonObject key{{"type", "key"}, {"keys", "a"}};
        QCOMPARE(describeForApproval({{"type", "macro"}, {"repeat", 3},
                                      {"steps", QJsonArray{QJsonObject{{"action", key}}, QJsonObject{{"delayMs", 5}}, QJsonObject{{"action", cond}}, QJsonObject{{"action", open}}}}}),
                 QStringLiteral("Macro (4 steps, 3 times): Conditional: ") + run + QStringLiteral("; otherwise Open URL https://a; Open URL https://a"));
    }

    // Nothing in an approval request may be hidden, ambiguous or rendered.
    void describeApprovalIsComplete()
    {
        const auto run = [](const QJsonArray &argv) { return describeForApproval({{"type", "command"}, {"argv", argv}}); };
        QCOMPARE(run({"a", "b c"}), QStringLiteral("Run a \"b c\""));
        QVERIFY(run({"a", "b c"}) != run({"a", "b", "c"}));
        QCOMPARE(run({"a", ""}), QStringLiteral("Run a \"\""));
        QCOMPARE(run({"a", "q\"\\"}), QStringLiteral("Run a \"q\\\"\\\\\""));
        QCOMPARE(run({"a", "x\n\x1b[2J"}), QStringLiteral("Run a \"x\\u000a\\u001b[2J\""));
        QCOMPARE(run({"a", QString(QChar(0x202e)) + QStringLiteral("b")}), QStringLiteral("Run a \"\\u202eb\""));
        QCOMPARE(run({"a", QString(QChar(0x9b))}), QStringLiteral("Run a \"\\u009b\""));
        QCOMPARE(run({"a", QString::fromUtf8("\xF3\xA0\x81\x81")}), QStringLiteral("Run a \"\\udb40\\udc41\"")); // tag character
        QCOMPARE(run({"a", QString::fromUtf8("\xC3\xA9")}), QString::fromUtf8("Run a \"\xC3\xA9\"")); // plain letters stay readable
        QCOMPARE(visibleText(QStringLiteral("a\x1b") + QChar(0x2066)), QStringLiteral("a\\u001b\\u2066"));

        QCOMPARE(describeForApproval({{"type", "dbus"}, {"bus", "system"}, {"service", "org.s"}, {"path", "/p"}, {"interface", "org.i"}, {"method", "M"},
                                      {"args", QJsonArray{"x", 1}}}),
                 QStringLiteral("D-Bus system org.s /p org.i.M [\"x\",1]"));
        QCOMPARE(describeForApproval({{"type", "dbus"}, {"service", "org.s"}, {"path", "/p"}, {"interface", "org.i"}, {"method", "M"}}),
                 QStringLiteral("D-Bus session org.s /p org.i.M []"));
        QCOMPARE(describeForApproval({{"type", "osc"}, {"host", "10.0.0.5"}, {"port", 9001}, {"address", "/a"}, {"args", QJsonArray{0.5}}}),
                 QStringLiteral("OSC 10.0.0.5:9001 /a [0.5]"));
        QCOMPARE(describeForApproval({{"type", "launch"}, {"desktopId", "x.desktop"}}), QStringLiteral("Open app x.desktop"));
        QCOMPARE(describeForApproval({{"type", "launch"}, {"path", "/tmp/a b"}}), QStringLiteral("Open file \"/tmp/a b\""));
        QCOMPARE(describeForApproval({{"type", "launch"}, {"url", "https://a/\x1b"}}), QStringLiteral("Open URL \"https://a/\\u001b\""));
    }

    void describe()
    {
        QCOMPARE(describeAction({{"type", "key"}, {"keys", "ctrl+z"}}), QStringLiteral("Key Ctrl+Z"));
        QCOMPARE(describeAction({{"type", "volume"}, {"delta", 2}}), QStringLiteral("Volume +2%"));
        QCOMPARE(describeAction({}), QStringLiteral("Nothing"));
    }
};

QTEST_GUILESS_MAIN(TestActions)
#include "test_actions.moc"
