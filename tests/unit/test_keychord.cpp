// SPDX-License-Identifier: MIT
#include "KeyChord.h"

#include <QtTest>
#include <xkbcommon/xkbcommon-keysyms.h>

using namespace opendial;

class TestKeyChord : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void basicChord()
    {
        auto r = parseKeyChord(QStringLiteral("ctrl+shift+z"));
        QVERIFY(r.ok());
        const auto c = r.sequence.chords.first();
        QCOMPARE(c.modifiers, unsigned(ModCtrl | ModShift));
        QCOMPARE(c.keysym, quint32(XKB_KEY_z));
        QCOMPARE(c.toString(), QStringLiteral("Ctrl+Shift+Z"));
    }

    void plusKeyAndAliases()
    {
        auto r = parseKeyChord(QStringLiteral("Ctrl++"));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.sequence.chords.first().keysym, quint32(XKB_KEY_plus));
        QCOMPARE(parseKeyChord(QStringLiteral("Ctrl+plus")).sequence.chords.first().keysym, quint32(XKB_KEY_plus));
        QCOMPARE(parseKeyChord(QStringLiteral("]")).sequence.chords.first().keysym, quint32(XKB_KEY_bracketright));
        QCOMPARE(parseKeyChord(QStringLiteral("Ctrl+[")).sequence.chords.first().keysym, quint32(XKB_KEY_bracketleft));
        QCOMPARE(parseKeyChord(QStringLiteral("esc")).sequence.chords.first().keysym, quint32(XKB_KEY_Escape));
        QCOMPARE(parseKeyChord(QStringLiteral("pgdn")).sequence.chords.first().keysym, quint32(XKB_KEY_Next));
        QCOMPARE(parseKeyChord(QStringLiteral("Super+Page_Down")).sequence.chords.first().keysym, quint32(XKB_KEY_Next));
        QCOMPARE(parseKeyChord(QStringLiteral("VolumeUp")).sequence.chords.first().keysym, quint32(XKB_KEY_XF86AudioRaiseVolume));
        QCOMPARE(parseKeyChord(QStringLiteral("F12")).sequence.chords.first().keysym, quint32(XKB_KEY_F12));
    }

    void internationalKeysyms()
    {
        auto r = parseKeyChord(QStringLiteral("Ctrl+eacute"));
        QVERIFY(r.ok());
        QCOMPARE(r.sequence.chords.first().keysym, quint32(XKB_KEY_eacute));
        auto u = parseKeyChord(QString::fromUtf8("Ctrl+ß"));
        QVERIFY2(u.ok(), qPrintable(u.error));
        QCOMPARE(u.sequence.chords.first().keysym, quint32(XKB_KEY_ssharp));
    }

    void sequences()
    {
        auto r = parseKeySequence(QStringLiteral("Ctrl+K Ctrl+C"));
        QVERIFY(r.ok());
        QCOMPARE(r.sequence.chords.size(), 2);
        QCOMPARE(r.sequence.toString(), QStringLiteral("Ctrl+K Ctrl+C"));
        QVERIFY(parseKeySequence(QStringLiteral("V, F")).ok());
    }

    void modifierOnly()
    {
        auto r = parseKeyChord(QStringLiteral("Alt"));
        QVERIFY(r.ok());
        QCOMPARE(r.sequence.chords.first().keysym, quint32(0));
        QCOMPARE(r.sequence.chords.first().modifiers, unsigned(ModAlt));
    }

    void errors()
    {
        QVERIFY(!parseKeyChord(QString()).ok());
        QVERIFY(!parseKeyChord(QStringLiteral("Ctrl+NotAKeyName")).ok());
        QVERIFY(!parseKeyChord(QStringLiteral("z+Ctrl")).ok());
        QVERIFY(!parseKeyChord(QStringLiteral("Ctrl++z")).ok());
        QString many;
        for (int i = 0; i < 20; ++i)
            many += QStringLiteral("a ");
        QVERIFY(!parseKeySequence(many).ok());
    }

    void textKeysyms()
    {
        auto l = keysymsForText(QString::fromUtf8("Hé!\n"));
        QCOMPARE(l.size(), 4);
        QCOMPARE(l.at(0), quint32(XKB_KEY_H));
        QCOMPARE(l.at(1), quint32(XKB_KEY_eacute));
        QCOMPARE(l.at(3), quint32(XKB_KEY_Return));
    }
};

QTEST_GUILESS_MAIN(TestKeyChord)
#include "test_keychord.moc"
