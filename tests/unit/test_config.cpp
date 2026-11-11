// SPDX-License-Identifier: MIT
#include "Config.h"

#include <QtTest>
#include <sys/stat.h>

using namespace opendial;

class TestConfig : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QString path(const char *n) const { return m_dir.filePath(QString::fromLatin1(n)); }
    static void writeRaw(const QString &p, const QByteArray &b)
    {
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(b);
    }

private Q_SLOTS:
    void defaultsAreValid()
    {
        auto c = defaultConfig();
        auto v = validateConfig(c);
        QVERIFY2(v.ok(), qPrintable(v.errors().join(QLatin1Char('\n'))));
        QVERIFY(v.warnings().isEmpty());
        // Windows parity default tools exist in order
        const auto g = c.value("profiles").toArray().first().toObject();
        QStringList ids;
        for (const auto &t : g.value("tools").toArray())
            ids << t.toObject().value("id").toString();
        QCOMPARE(ids.mid(0, 6), (QStringList{"volume", "scroll", "zoom", "undo", "brightness", "media"}));
    }

    void rejectsInvalid()
    {
        auto c = defaultConfig();
        auto profiles = c.value("profiles").toArray();
        auto g = profiles.first().toObject();
        auto tools = g.value("tools").toArray();
        auto t = tools.first().toObject();
        t.insert("bindings", QJsonObject{{"rotate_sideways", QJsonObject{{"type", "none"}}}});
        tools.replace(0, t);
        g.insert("tools", tools);
        profiles.replace(0, g);
        c.insert("profiles", profiles);
        QVERIFY(!validateConfig(c).ok());

        auto c2 = defaultConfig();
        c2.insert("profiles", QJsonArray{});
        QVERIFY(!validateConfig(c2).ok()); // no global profile

        auto c3 = defaultConfig();
        auto s = c3.value("settings").toObject();
        auto gs = s.value("gestures").toObject();
        gs.insert("longPressMs", 10);
        s.insert("gestures", gs);
        c3.insert("settings", s);
        QVERIFY(!validateConfig(c3).ok());
    }

    void duplicateToolIds()
    {
        QJsonObject p{{"id", "x"}, {"name", "X"}, {"match", QJsonObject{{"kind", "global"}}},
                      {"tools", QJsonArray{QJsonObject{{"id", "a"}, {"name", "A"}}, QJsonObject{{"id", "a"}, {"name", "B"}}}}};
        QVERIFY(!validateProfile(p).ok());
    }

    void migrateV0()
    {
        QJsonObject v0{{"tools", QJsonArray{QJsonObject{{"id", "vol"}, {"name", "Vol"}, {"bindings", QJsonObject{{"click", QJsonObject{{"type", "mute"}}}}}}}},
                       {"defaultTool", "vol"},
                       {"customKey", "preserved"}};
        auto r = migrateConfig(v0);
        QVERIFY(r.ok);
        QVERIFY(r.changed);
        QCOMPARE(r.config.value("version").toInt(), kConfigVersion);
        QCOMPARE(r.config.value("customKey").toString(), QStringLiteral("preserved"));
        QVERIFY(validateConfig(r.config).ok());
        QCOMPARE(globalTools(r.config).first().toObject().value("id").toString(), QStringLiteral("vol"));
    }

    void migrateV1AccentFollowsSystem()
    {
        auto v1 = defaultConfig();
        v1.insert("version", 1);
        auto settings = v1.value("settings").toObject();
        auto menu = settings.value("menu").toObject();
        menu.insert("accent", "#4C8DFF"); // the old default, as v1 wrote it
        settings.insert("menu", menu);
        v1.insert("settings", settings);
        auto r = migrateConfig(v1);
        QVERIFY(r.ok);
        QVERIFY(r.changed);
        QCOMPARE(r.config.value("version").toInt(), kConfigVersion);
        QCOMPARE(r.config.value("settings").toObject().value("menu").toObject().value("accent").toString(), QStringLiteral("system"));

        // a colour the user picked stays
        menu.insert("accent", "#e0527a");
        settings.insert("menu", menu);
        v1.insert("settings", settings);
        r = migrateConfig(v1);
        QCOMPARE(r.config.value("settings").toObject().value("menu").toObject().value("accent").toString(), QStringLiteral("#e0527a"));
    }

    void migrateV2KritaReplacedBy()
    {
        auto c = defaultConfig();
        c.insert("version", 2);
        QJsonArray profiles = c.value("profiles").toArray();
        for (int i = 0; i < profiles.size(); ++i) {
            auto p = profiles.at(i).toObject();
            if (p.value("id") != "krita")
                continue;
            QJsonArray tools;
            for (const auto &t : p.value("tools").toArray()) {
                auto o = t.toObject();
                o.remove("replacedBy");
                tools.append(o);
            }
            p.insert("tools", tools);
            profiles.replace(i, p);
        }
        c.insert("profiles", profiles);
        auto r = migrateConfig(c);
        QVERIFY(r.ok);
        int replaced = 0;
        for (const auto &p : r.config.value("profiles").toArray())
            if (p.toObject().value("id") == "krita")
                for (const auto &t : p.toObject().value("tools").toArray())
                    replaced += t.toObject().value("replacedBy").toString() == QLatin1String("org.kde.krita");
        QCOMPARE(replaced, 3);
        QVERIFY(validateConfig(r.config).ok());
    }

    void menuThemeValidation()
    {
        auto set = [](const char *key, const QString &val) {
            auto c = defaultConfig();
            auto settings = c.value("settings").toObject();
            auto menu = settings.value("menu").toObject();
            menu.insert(key, val);
            settings.insert("menu", menu);
            c.insert("settings", settings);
            return validateConfig(c).ok();
        };
        QVERIFY(set("accent", "system"));
        QVERIFY(set("accent", "#b59790"));
        QVERIFY(!set("accent", "red"));
        QVERIFY(!set("accent", "#12345"));
        QVERIFY(set("theme", "light"));
        QVERIFY(!set("theme", "purple"));
    }

    void refusesNewer()
    {
        auto r = migrateConfig(QJsonObject{{"schema", kConfigSchema}, {"version", 99}});
        QVERIFY(r.tooNew);
        QVERIFY(!r.ok);
    }

    void atomicWriteAndBackups()
    {
        JsonStore s(path("cfg/config.json"), 3);
        QString err;
        for (int i = 0; i < 5; ++i)
            QVERIFY2(s.write(QJsonObject{{"n", i}}, &err), qPrintable(err));
        QJsonObject o;
        QVERIFY(s.read(o, &err));
        QCOMPARE(o.value("n").toInt(), 4);
        QVERIFY(QFile::exists(s.backupPaths().at(0)));
        QVERIFY(QFile::exists(s.backupPaths().at(2)));
        JsonStore b1(s.backupPaths().at(0), 0);
        QVERIFY(b1.read(o, &err));
        QCOMPARE(o.value("n").toInt(), 3);
        QFileInfo fi(s.path());
        QCOMPARE(fi.permissions() & (QFile::ReadOther | QFile::WriteOther | QFile::ReadGroup), QFile::Permissions());
    }

    void rollbackWalksBack()
    {
        JsonStore s(path("rb/config.json"), 5);
        QString err;
        for (int v = 1; v <= 4; ++v)
            QVERIFY2(s.write(QJsonObject{{QStringLiteral("v"), v}}, &err), qPrintable(err));
        QJsonObject o;
        QCOMPARE(s.rollback(o, &err), 1);
        QCOMPARE(o.value(QStringLiteral("v")).toInt(), 3);
        QVERIFY(s.read(o, &err));
        QCOMPARE(o.value(QStringLiteral("v")).toInt(), 3); // the file itself was restored
        QCOMPARE(s.rollback(o, &err), 1);
        QCOMPARE(o.value(QStringLiteral("v")).toInt(), 2);
        QCOMPARE(s.rollback(o, &err), 1);
        QCOMPARE(o.value(QStringLiteral("v")).toInt(), 1);
        QCOMPARE(s.rollback(o, &err), -1); // nothing older
        QVERIFY(s.read(o, &err));
        QCOMPARE(o.value(QStringLiteral("v")).toInt(), 1);
    }

    void refusesSymlinkTarget()
    {
        const QString target = path("elsewhere.json");
        writeRaw(target, "{}");
        const QString link = path("linkdir/config.json");
        QDir().mkpath(path("linkdir"));
        QVERIFY(QFile::link(target, link));
        JsonStore s(link, 1);
        QString err;
        QVERIFY(!s.write(QJsonObject{{"evil", true}}, &err));
        QVERIFY(err.contains("symlink"));
        QFile f(target);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("{}"));
    }

    void corruptFallsBackToBackup()
    {
        JsonStore s(path("c2/config.json"), 3);
        QString err;
        QVERIFY(s.write(defaultConfig(), &err));
        QVERIFY(s.write(defaultConfig(), &err)); // creates backup 1
        writeRaw(s.path(), "{ this is not json");
        auto lc = loadConfig(s);
        QVERIFY(lc.fromBackup);
        QVERIFY(lc.validation.ok());
        QVERIFY(QFile::exists(s.path() + ".corrupt"));
    }

    void invalidKeepsUserFileReadOnly()
    {
        JsonStore s(path("c3/config.json"), 0);
        QDir().mkpath(path("c3"));
        const QByteArray bad = R"({"schema":"opendial.config","version":1,"profiles":[]})";
        writeRaw(s.path(), bad);
        auto lc = loadConfig(s);
        QVERIFY(lc.readOnly);
        QVERIFY(lc.fromDefaults);
        QFile f(s.path());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), bad); // never overwritten
    }

    void newerVersionNotTouched()
    {
        JsonStore s(path("c4/config.json"), 0);
        QDir().mkpath(path("c4"));
        const QByteArray newer = R"({"schema":"opendial.config","version":7,"profiles":[]})";
        writeRaw(s.path(), newer);
        auto lc = loadConfig(s);
        QVERIFY(lc.readOnly);
        QFile f(s.path());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), newer);
    }

    void missingFileGivesDefaults()
    {
        JsonStore s(path("none/config.json"), 0);
        auto lc = loadConfig(s);
        QVERIFY(lc.fromDefaults);
        QVERIFY(!lc.readOnly);
    }

    void mergeDefaultsKeepsUserValues()
    {
        auto m = mergeDefaults(QJsonObject{{"a", 1}, {"o", QJsonObject{{"x", 1}, {"y", 2}}}},
                               QJsonObject{{"a", 5}, {"o", QJsonObject{{"x", 9}}}});
        QCOMPARE(m.value("a").toInt(), 5);
        QCOMPARE(m.value("o").toObject().value("x").toInt(), 9);
        QCOMPARE(m.value("o").toObject().value("y").toInt(), 2);
    }

    void profileImportExport()
    {
        auto g = defaultConfig().value("profiles").toArray().at(2).toObject(); // krita
        auto env = exportProfileEnvelope(g);
        QJsonObject back;
        QStringList errs;
        QList<QPair<QString, QJsonObject>> risky;
        QVERIFY(importProfileEnvelope(env, back, &errs, &risky));
        QCOMPARE(back, g);
        QVERIFY(risky.isEmpty());

        auto evil = g;
        auto tools = evil.value("tools").toArray();
        auto t = tools.first().toObject();
        auto b = t.value("bindings").toObject();
        b.insert("click", QJsonObject{{"type", "command"}, {"argv", QJsonArray{"curl", "evil"}}});
        t.insert("bindings", b);
        tools.replace(0, t);
        evil.insert("tools", tools);
        risky.clear();
        QVERIFY(importProfileEnvelope(exportProfileEnvelope(evil), back, &errs, &risky));
        QCOMPARE(risky.size(), 1);

        QVERIFY(!importProfileEnvelope(QJsonObject{{"schema", kProfileSchema}, {"version", 42}, {"profile", g}}, back, &errs, nullptr));
        QVERIFY(!importProfileEnvelope(QJsonObject{{"hello", 1}}, back, &errs, nullptr));
    }

private:
    static QJsonArray globalTools(const QJsonObject &c)
    {
        return c.value("profiles").toArray().first().toObject().value("tools").toArray();
    }
};

QTEST_GUILESS_MAIN(TestConfig)
#include "test_config.moc"
