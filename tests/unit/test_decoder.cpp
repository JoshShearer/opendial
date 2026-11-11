// SPDX-License-Identifier: MIT
#include "DialDecoder.h"
#include "GestureEngine.h"
#include "Rotation.h"

#include <QtTest>

using namespace opendial;

namespace {
QByteArray readFixture(const char *name)
{
    QFile f(QStringLiteral(OPENDIAL_FIXTURE_DIR "/") + QString::fromLatin1(name));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return f.readAll();
}

struct Replay {
    QList<DialSignal> signals_;
    std::vector<GestureEvent> gestures;
    int connects = 0, disconnects = 0;
};

Replay replay(const QByteArray &data, int guardMs = 0, double unitsToDeg = 0.1)
{
    Replay r;
    DialDecoder dec;
    GestureEngine ge;
    auto pump = [&](qint64 nowMs) {
        while (auto d = ge.nextDeadline()) {
            if (*d > nowMs)
                break;
            auto e = ge.onTimer(*d);
            r.gestures.insert(r.gestures.end(), e.begin(), e.end());
        }
    };
    for (const auto &l : parseFixture(data)) {
        const qint64 ms = l.event.timeUs / 1000;
        pump(ms);
        if (l.kind == FixtureLine::Connect) {
            dec.onConnected(l.event.timeUs, guardMs);
            ++r.connects;
            continue;
        }
        if (l.kind == FixtureLine::Disconnect) {
            ge.reset();
            ++r.disconnects;
            continue;
        }
        for (const auto &s : dec.feed(l.event)) {
            r.signals_ << s;
            std::vector<GestureEvent> e;
            if (s.kind == DialSignal::Button)
                e = ge.onButton(s.down, ms);
            else if (s.kind == DialSignal::Rotate)
                e = ge.onRotate(s.units * unitsToDeg, ms);
            r.gestures.insert(r.gestures.end(), e.begin(), e.end());
        }
    }
    pump(std::numeric_limits<qint64>::max() / 2);
    return r;
}
} // namespace

class TestDecoder : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void clickRotateFixture()
    {
        auto r = replay(readFixture("synthetic-click-rotate.jsonl"));
        QCOMPARE(r.connects, 1);
        QCOMPARE(r.disconnects, 1);
        QList<GestureKind> semantic;
        double cw = 0, ccw = 0, pressedDeg = 0;
        for (const auto &g : r.gestures) {
            if (g.kind == GestureKind::Click || g.kind == GestureKind::LongPress || g.kind == GestureKind::Release)
                semantic << g.kind;
            if (g.kind == GestureKind::Rotate) {
                if (g.pressed)
                    pressedDeg += g.degrees;
                else if (g.degrees > 0)
                    cw += g.degrees;
                else
                    ccw += g.degrees;
            }
        }
        QCOMPARE(semantic, (QList<GestureKind>{GestureKind::Click, GestureKind::LongPress, GestureKind::Release, GestureKind::Release}));
        QCOMPARE(cw, 30.0);
        QCOMPARE(ccw, -20.0);
        QCOMPARE(pressedDeg, 15.0);
    }

    void evtestFormatWithDroppedFrame()
    {
        const auto lines = parseFixture(readFixture("synthetic-evtest.txt"));
        QVERIFY(lines.size() >= 10);
        DialDecoder dec;
        int total = 0;
        for (const auto &l : lines)
            for (const auto &s : dec.feed(l.event))
                if (s.kind == DialSignal::Rotate)
                    total += s.units;
        // 10 + 10 + (dropped frame discarded) + -5 = 15
        QCOMPARE(total, 15);
    }

    void reconnectFloodGuard()
    {
        const auto data = readFixture("synthetic-reconnect-flood.jsonl");
        auto unguarded = replay(data, 0);
        auto guarded = replay(data, 800);
        double u = 0, g = 0;
        for (const auto &e : unguarded.gestures)
            if (e.kind == GestureKind::Rotate) u += e.degrees;
        for (const auto &e : guarded.gestures)
            if (e.kind == GestureKind::Rotate) g += e.degrees;
        QCOMPARE(u, 410.0); // 41 reports * 10 deg
        QCOMPARE(g, 10.0);  // only the deliberate turn after the guard
    }

    void hardwareResolutionMapping()
    {
        // at 36 steps/turn (haptic detents every 10 deg) one unit = one detent
        HardwareMode m = hardwareModeFor({.detentDeg = 10});
        QCOMPARE(m.unitsToDegrees(), 10.0);
        RotationConfig rc;
        rc.detentDeg = m.appliedDetentDeg;
        RotationProcessor p(rc);
        QCOMPARE(p.process(1 * m.unitsToDegrees(), 0).steps, 1);
    }

    void malformedInputIsBounded()
    {
        DialDecoder dec;
        QRandomGenerator rng(99);
        for (int i = 0; i < 100000; ++i) {
            RawEvent e{i, quint16(rng.bounded(0, 6)), quint16(rng.bounded(0, 0x200)), qint32(rng.generate())};
            const auto out = dec.feed(e);
            QVERIFY(out.size() <= 65);
            for (const auto &s : out)
                if (s.kind == DialSignal::Rotate)
                    QVERIFY(qAbs(s.units) <= 32767);
        }
    }

    void hidInputReports()
    {
        auto r = decodeHidInputReport(QByteArray::fromHex("0101f6ff"));
        QVERIFY(r);
        QVERIFY(r->button);
        QCOMPARE(r->dial, qint16(-10));
        QVERIFY(!decodeHidInputReport(QByteArray::fromHex("02010000")));
        QVERIFY(!decodeHidInputReport(QByteArray::fromHex("01")));
        QVERIFY(!decodeHidInputReport(QByteArray(200, '\x01')));
    }

    void badJsonFixtureReportsError()
    {
        QString err;
        auto l = parseFixture("{\"t\": 1, oops}\n", &err);
        QVERIFY(l.isEmpty());
        QVERIFY(err.contains("line 1"));
    }
};

QTEST_GUILESS_MAIN(TestDecoder)
#include "test_decoder.moc"
