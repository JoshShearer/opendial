// SPDX-License-Identifier: MIT
#include "Rotation.h"

#include <QtTest>

using namespace opendial;

class TestRotation : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void steppedAccumulatesSubDetent()
    {
        RotationConfig c;
        c.detentDeg = 10;
        RotationProcessor p(c);
        QCOMPARE(p.process(4, 0).steps, 0);
        QCOMPARE(p.process(4, 10).steps, 0);
        QCOMPARE(p.process(4, 20).steps, 1); // 12 deg -> 1 step, 2 carried
        QCOMPARE(p.pendingDegrees(), 2.0);
    }

    void preservesTotalDelta()
    {
        RotationConfig c;
        c.detentDeg = 3.6;
        c.resetOnReverse = false;
        RotationProcessor p(c);
        int total = 0;
        for (int i = 0; i < 1000; ++i)
            total += p.process(0.36, i * 5).steps; // 360 deg in 0.1-deg-ish pieces
        QCOMPARE(total, 100);
    }

    void directionAndInversion()
    {
        RotationConfig c;
        c.detentDeg = 10;
        RotationProcessor p(c);
        QCOMPARE(p.process(-20, 0).steps, -2);
        c.invert = true;
        p.setConfig(c);
        QCOMPARE(p.process(-20, 100).steps, 2);
    }

    void rapidReversalDropsPartial()
    {
        RotationConfig c;
        c.detentDeg = 10;
        RotationProcessor p(c);
        p.process(8, 0);
        QCOMPARE(p.process(-3, 10).steps, 0); // partial +8 dropped, now -3
        QCOMPARE(p.pendingDegrees(), -3.0);
        QCOMPARE(p.process(-7, 20).steps, -1);
    }

    void sensitivityAndFine()
    {
        RotationConfig c;
        c.detentDeg = 10;
        c.sensitivity = 2.0;
        RotationProcessor p(c);
        QCOMPARE(p.process(10, 0).steps, 2);
        p.reset();
        QCOMPARE(p.process(40, 1000, /*fine=*/true).steps, 2); // 40*2*0.25 = 20
    }

    void accelerationAtSpeed()
    {
        RotationConfig c;
        c.detentDeg = 10;
        c.acceleration = 1.0;
        c.accelThresholdDps = 100;
        c.smoothing = 0;
        RotationProcessor slow(c), fast(c);
        int s = 0, f = 0;
        for (int i = 0; i < 10; ++i) {
            s += slow.process(10, i * 200).steps; // 50 deg/s
            f += fast.process(10, i * 20).steps;  // 500 deg/s
        }
        QCOMPARE(s, 10);
        QVERIFY2(f > 20, qPrintable(QString::number(f)));
    }

    void overflowBounded()
    {
        RotationConfig c;
        c.detentDeg = 1;
        c.maxStepsPerEvent = 40;
        RotationProcessor p(c);
        auto o = p.process(100000, 0);
        QCOMPARE(o.steps, 40);
        QVERIFY(std::abs(p.pendingDegrees()) <= 40.0);
    }

    void ignoresNonFinite()
    {
        RotationProcessor p;
        QCOMPARE(p.process(std::numeric_limits<double>::quiet_NaN(), 0).steps, 0);
        QCOMPARE(p.process(std::numeric_limits<double>::infinity(), 0).steps, 0);
    }

    void jsonRoundTripAndClamping()
    {
        auto c = RotationConfig::fromJson(QJsonObject{{"detentDegrees", 0.0}, {"sensitivity", 100.0}, {"mode", "continuous"}});
        QCOMPARE(c.detentDeg, 0.1);
        QCOMPARE(c.sensitivity, 20.0);
        QCOMPARE(c.mode, RotationConfig::Mode::Continuous);
        auto c2 = RotationConfig::fromJson(c.toJson());
        QCOMPARE(c2.detentDeg, c.detentDeg);
    }

    void hapticsPolicy()
    {
        auto off = hardwareModeFor({.globalEnabled = false});
        QCOMPARE(off.resolutionSteps, quint16(3600));
        QVERIFY(!off.autoHaptics);

        auto ten = hardwareModeFor({.globalEnabled = true, .toolEnabled = true, .detentDeg = 10});
        QCOMPARE(ten.resolutionSteps, quint16(36));
        QVERIFY(ten.autoHaptics);
        QCOMPARE(ten.appliedDetentDeg, 10.0);

        auto toolOff = hardwareModeFor({.globalEnabled = true, .toolEnabled = false, .detentDeg = 10});
        QVERIFY(!toolOff.autoHaptics);

        auto fine = hardwareModeFor({.globalEnabled = true, .toolEnabled = true, .detentDeg = 0.5, .minHapticDetentDeg = 2});
        QVERIFY(fine.fineHapticsClamped);
        QCOMPARE(fine.resolutionSteps, quint16(180));

        auto odd = hardwareModeFor({.detentDeg = 7});
        QCOMPARE(odd.resolutionSteps, quint16(51));
        QVERIFY(std::abs(odd.appliedDetentDeg - 7.0588) < 0.001);
    }

    void hidReports()
    {
        HardwareMode m;
        m.resolutionSteps = 36;
        m.autoHaptics = true;
        QCOMPARE(hid::encodeModeFeature(m), QByteArray::fromHex("0124000003000000"));
        m.resolutionSteps = 3600;
        m.autoHaptics = false;
        QCOMPARE(hid::encodeModeFeature(m), QByteArray::fromHex("01100e0002000000"));
        QCOMPARE(hid::encodeManualHaptic(hid::Waveform::Click, 1), QByteArray::fromHex("0101030000"));
        QCOMPARE(hid::encodeManualHaptic(hid::Waveform::BuzzContinuous, 2, 5000), QByteArray::fromHex("010204d007"));
    }
};

QTEST_GUILESS_MAIN(TestRotation)
#include "test_rotation.moc"
