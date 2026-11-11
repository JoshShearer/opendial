// SPDX-License-Identifier: MIT
#include "GestureEngine.h"

#include <QtTest>

using namespace opendial;

namespace {
QList<GestureKind> kinds(const std::vector<GestureEvent> &v)
{
    QList<GestureKind> l;
    for (const auto &e : v)
        l << e.kind;
    return l;
}
std::vector<GestureEvent> drive(GestureEngine &g, qint64 until)
{
    std::vector<GestureEvent> out;
    while (auto d = g.nextDeadline()) {
        if (*d > until)
            break;
        auto e = g.onTimer(*d);
        out.insert(out.end(), e.begin(), e.end());
    }
    return out;
}
} // namespace

class TestGesture : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void shortClickFiresOnceImmediately()
    {
        GestureEngine g;
        QCOMPARE(kinds(g.onButton(true, 0)), QList<GestureKind>{GestureKind::PhysicalDown});
        auto up = g.onButton(false, 120);
        QCOMPARE(kinds(up), (QList<GestureKind>{GestureKind::PhysicalUp, GestureKind::Click}));
        QCOMPARE(up.back().count, 1);
        QVERIFY(!g.nextDeadline());
        QVERIFY(drive(g, 5000).empty()); // no late long press
    }

    void longPressFiresAtThresholdAndNoClick()
    {
        GestureEngine g({.longPressMs = 500});
        g.onButton(true, 1000);
        QCOMPARE(*g.nextDeadline(), 1500);
        QVERIFY(g.onTimer(1499).empty());
        auto lp = g.onTimer(1500);
        QCOMPARE(kinds(lp), QList<GestureKind>{GestureKind::LongPress});
        auto up = g.onButton(false, 2000);
        QCOMPARE(kinds(up), (QList<GestureKind>{GestureKind::PhysicalUp, GestureKind::Release}));
    }

    void configurableLongPress()
    {
        GestureEngine g({.longPressMs = 900});
        g.onButton(true, 0);
        QVERIFY(g.onTimer(800).empty());
        QCOMPARE(kinds(g.onTimer(900)), QList<GestureKind>{GestureKind::LongPress});
    }

    void rotateWhilePressedCancelsLongPressAndClick()
    {
        GestureEngine g;
        g.onButton(true, 0);
        auto r1 = g.onRotate(1.0, 50); // below hysteresis threshold
        QVERIFY(r1.empty());
        auto r2 = g.onRotate(5.0, 80);
        QCOMPARE(r2.size(), size_t(1));
        QCOMPARE(r2[0].kind, GestureKind::Rotate);
        QVERIFY(r2[0].pressed);
        QCOMPARE(r2[0].degrees, 6.0); // buffered wobble preserved
        QVERIFY(!g.nextDeadline());    // long press cancelled
        auto up = g.onButton(false, 2000);
        QCOMPARE(kinds(up), (QList<GestureKind>{GestureKind::PhysicalUp, GestureKind::Release}));
    }

    void wobbleDuringClickStillClicks()
    {
        GestureEngine g;
        g.onButton(true, 0);
        QVERIFY(g.onRotate(1.5, 30).empty());
        auto up = g.onButton(false, 100);
        QCOMPARE(up.back().kind, GestureKind::Click);
    }

    void plainRotationNotPressed()
    {
        GestureEngine g;
        auto r = g.onRotate(-10.0, 0);
        QCOMPARE(r.size(), size_t(1));
        QVERIFY(!r[0].pressed);
        QCOMPARE(r[0].degrees, -10.0);
    }

    void doubleClickWaitsWhenEnabled()
    {
        GestureEngine g({.doubleClickMs = 300, .maxClickCount = 2});
        g.onButton(true, 0);
        auto up1 = g.onButton(false, 80);
        QCOMPARE(kinds(up1), QList<GestureKind>{GestureKind::PhysicalUp});
        g.onButton(true, 200);
        auto up2 = g.onButton(false, 260);
        QCOMPARE(up2.back().kind, GestureKind::Click);
        QCOMPARE(up2.back().count, 2);
    }

    void singleClickAfterDoubleWindow()
    {
        GestureEngine g({.doubleClickMs = 300, .maxClickCount = 2});
        g.onButton(true, 0);
        g.onButton(false, 80);
        auto t = drive(g, 1000);
        QCOMPARE(t.size(), size_t(1));
        QCOMPARE(t[0].kind, GestureKind::Click);
        QCOMPARE(t[0].count, 1);
        QCOMPARE(t[0].timeMs, 380);
    }

    void tripleClick()
    {
        GestureEngine g({.doubleClickMs = 300, .maxClickCount = 3});
        g.onButton(true, 0);
        g.onButton(false, 50);
        g.onButton(true, 150);
        g.onButton(false, 200);
        g.onButton(true, 300);
        auto up = g.onButton(false, 350);
        QCOMPARE(up.back().count, 3);
    }

    void rotationFlushesPendingClick()
    {
        GestureEngine g({.doubleClickMs = 300, .maxClickCount = 2});
        g.onButton(true, 0);
        g.onButton(false, 60);
        auto r = g.onRotate(10, 100);
        QCOMPARE(kinds(r), (QList<GestureKind>{GestureKind::Click, GestureKind::Rotate}));
    }

    void debounceSuppressesBouncedPress()
    {
        GestureEngine g({.debounceMs = 10});
        g.onButton(true, 0);
        g.onButton(false, 40);
        auto bounce = g.onButton(true, 45); // 5 ms after release: bounce
        QVERIFY(bounce.empty());
        QVERIFY(!g.isDown());
        auto dup = g.onButton(false, 47); // duplicate release ignored
        QVERIFY(dup.empty());
    }

    void duplicateStatesIgnored()
    {
        GestureEngine g;
        g.onButton(true, 0);
        QVERIFY(g.onButton(true, 20).empty());
    }

    void rotateAfterLongPressFlagged()
    {
        GestureEngine g;
        g.onButton(true, 0);
        g.onTimer(500);
        auto r = g.onRotate(20, 600);
        QCOMPARE(r.size(), size_t(1));
        QVERIFY(r[0].afterLongPress);
    }

    void longPressDisabled()
    {
        GestureEngine g({.longPressEnabled = false});
        g.onButton(true, 0);
        QVERIFY(!g.nextDeadline());
        auto up = g.onButton(false, 3000);
        QCOMPARE(up.back().kind, GestureKind::Click);
    }

    void clickToggleLatch()
    {
        GestureEngine g({.clickToggleLatch = true});
        g.onButton(true, 0);
        auto up = g.onButton(false, 50);
        QCOMPARE(up.back().kind, GestureKind::LatchChanged);
        QVERIFY(up.back().latched);
        auto r = g.onRotate(10, 100);
        QVERIFY(r[0].pressed);
        g.onButton(true, 200);
        g.onButton(false, 250);
        QVERIFY(!g.isLatched());
    }

    void resetNeverEmits()
    {
        GestureEngine g;
        g.onButton(true, 0);
        g.reset();
        QVERIFY(!g.isDown());
        QVERIFY(!g.nextDeadline());
        QVERIFY(g.onButton(false, 100).empty()); // stale release after reset: no click
    }

    void randomizedNoStuckState()
    {
        // Property: whatever the sequence, after a final release and a long
        // idle period there is no pending deadline and the button is up.
        QRandomGenerator rng(1234);
        for (int iter = 0; iter < 2000; ++iter) {
            GestureEngine g({.maxClickCount = int(rng.bounded(1, 4))});
            qint64 t = 0;
            for (int i = 0; i < 40; ++i) {
                t += rng.bounded(1, 400);
                switch (rng.bounded(3)) {
                case 0: g.onButton(true, t); break;
                case 1: g.onButton(false, t); break;
                default: g.onRotate(rng.bounded(-30, 30), t); break;
                }
                drive(g, t);
            }
            g.onButton(false, t + 1000);
            drive(g, t + 10000);
            QVERIFY(!g.isDown());
            QVERIFY(!g.nextDeadline());
        }
    }
};

QTEST_GUILESS_MAIN(TestGesture)
#include "test_gesture.moc"
