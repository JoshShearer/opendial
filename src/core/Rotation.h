// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QtGlobal>

namespace opendial {

struct RotationConfig {
    enum class Mode { Stepped, Continuous };
    Mode mode = Mode::Stepped;
    double detentDeg = 10.0;      // software step size (Stepped); fine step in Continuous
    double sensitivity = 1.0;     // linear multiplier
    bool invert = false;
    double acceleration = 0.0;    // 0 = off, up to 3
    double accelThresholdDps = 180.0; // deg/s where acceleration starts
    double maxAccelFactor = 4.0;
    double smoothing = 0.3;       // EMA factor for velocity estimate (0 = none)
    bool resetOnReverse = true;   // drop partial step on direction reversal
    int maxStepsPerEvent = 40;    // safe overflow limit; excess carried, bounded
    double fineFactor = 0.25;     // sensitivity multiplier for fine control while pressed

    static RotationConfig fromJson(const QJsonObject &o, const RotationConfig &base);
    static RotationConfig fromJson(const QJsonObject &o);
    QJsonObject toJson() const;
};

struct RotationOutput {
    int steps = 0;          // signed whole steps (+ = clockwise after inversion)
    double degrees = 0.0;   // scaled signed degrees
    double velocityDps = 0; // smoothed velocity estimate
};

// Converts physical degrees into action steps with sensitivity, acceleration,
// detents and inversion. Coalesces sub-step motion and preserves total delta
// (within the overflow bound).
class RotationProcessor {
public:
    explicit RotationProcessor(RotationConfig cfg = {}) : m_cfg(cfg) {}
    void setConfig(const RotationConfig &cfg) { m_cfg = cfg; }
    const RotationConfig &config() const { return m_cfg; }

    RotationOutput process(double physicalDegrees, qint64 nowMs, bool fine = false);
    void reset();
    double pendingDegrees() const { return m_accum; }

private:
    RotationConfig m_cfg;
    double m_accum = 0.0;
    double m_velocity = 0.0;
    qint64 m_lastAt = -1;
};

// Physical device configuration requested from the haptics policy.
struct HardwareMode {
    quint16 resolutionSteps = 3600; // HID Resolution Multiplier: subdivisions per turn
    bool autoHaptics = false;       // HID auto-trigger (detent click per subdivision)
    double appliedDetentDeg = 0.1;  // 360 / resolutionSteps
    bool fineHapticsClamped = false; // requested haptic detent was finer than allowed
    double unitsToDegrees() const { return 360.0 / resolutionSteps; }
    bool operator==(const HardwareMode &o) const
    {
        return resolutionSteps == o.resolutionSteps && autoHaptics == o.autoHaptics;
    }
};

struct HapticsRequest {
    bool globalEnabled = true;
    bool toolEnabled = true;
    double detentDeg = 10.0;
    double minHapticDetentDeg = 2.0; // Microsoft guidance: avoid haptics at fine resolution
};

// Pure policy: decide the hardware mode for a requested tool configuration.
HardwareMode hardwareModeFor(const HapticsRequest &req);

// HID report encoding (validated against the device's report descriptor;
// see docs/hardware-protocol.md).
namespace hid {
constexpr quint16 kVendorMicrosoft = 0x045E;
constexpr quint16 kProductSurfaceDial = 0x091B;
constexpr quint8 kReportId = 0x01;

enum class Waveform : quint8 { Click = 3, BuzzContinuous = 4, RumbleContinuous = 5 };

// Feature report 1: [id, resolution lo, resolution hi, repeat count,
//                    auto trigger, waveform cutoff, retrigger lo, retrigger hi]
QByteArray encodeModeFeature(const HardwareMode &m);
// Output report 1: [id, repeat count, manual trigger, retrigger lo, retrigger hi]
QByteArray encodeManualHaptic(Waveform w, quint8 repeat, quint16 retriggerMs = 0);
} // namespace hid

} // namespace opendial
