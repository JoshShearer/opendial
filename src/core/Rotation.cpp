// SPDX-License-Identifier: MIT
#include "Rotation.h"

#include <QByteArray>
#include <algorithm>
#include <cmath>

namespace opendial {

RotationConfig RotationConfig::fromJson(const QJsonObject &o, const RotationConfig &base)
{
    RotationConfig c = base;
    if (o.contains(QLatin1String("mode")))
        c.mode = o.value(QLatin1String("mode")).toString() == QLatin1String("continuous")
                     ? Mode::Continuous
                     : Mode::Stepped;
    auto num = [&](const char *k, double &dst, double lo, double hi) {
        const auto v = o.value(QLatin1String(k));
        if (v.isDouble())
            dst = std::clamp(v.toDouble(), lo, hi);
    };
    num("detentDegrees", c.detentDeg, 0.1, 180.0);
    num("sensitivity", c.sensitivity, 0.05, 20.0);
    num("acceleration", c.acceleration, 0.0, 3.0);
    num("accelThresholdDps", c.accelThresholdDps, 10.0, 3600.0);
    num("maxAccelFactor", c.maxAccelFactor, 1.0, 10.0);
    num("smoothing", c.smoothing, 0.0, 0.95);
    num("fineFactor", c.fineFactor, 0.05, 1.0);
    if (o.value(QLatin1String("invert")).isBool())
        c.invert = o.value(QLatin1String("invert")).toBool();
    if (o.value(QLatin1String("resetOnReverse")).isBool())
        c.resetOnReverse = o.value(QLatin1String("resetOnReverse")).toBool();
    if (o.value(QLatin1String("maxStepsPerEvent")).isDouble())
        c.maxStepsPerEvent = std::clamp(o.value(QLatin1String("maxStepsPerEvent")).toInt(), 1, 500);
    return c;
}

RotationConfig RotationConfig::fromJson(const QJsonObject &o)
{
    return fromJson(o, RotationConfig());
}

QJsonObject RotationConfig::toJson() const
{
    return QJsonObject{
        {QStringLiteral("mode"), mode == Mode::Continuous ? QStringLiteral("continuous") : QStringLiteral("stepped")},
        {QStringLiteral("detentDegrees"), detentDeg},
        {QStringLiteral("sensitivity"), sensitivity},
        {QStringLiteral("invert"), invert},
        {QStringLiteral("acceleration"), acceleration},
        {QStringLiteral("accelThresholdDps"), accelThresholdDps},
        {QStringLiteral("maxAccelFactor"), maxAccelFactor},
        {QStringLiteral("smoothing"), smoothing},
        {QStringLiteral("resetOnReverse"), resetOnReverse},
        {QStringLiteral("maxStepsPerEvent"), maxStepsPerEvent},
        {QStringLiteral("fineFactor"), fineFactor},
    };
}

void RotationProcessor::reset()
{
    m_accum = 0.0;
    m_velocity = 0.0;
    m_lastAt = -1;
}

RotationOutput RotationProcessor::process(double deg, qint64 now, bool fine)
{
    RotationOutput out;
    if (!std::isfinite(deg) || deg == 0.0)
        return out;

    // Velocity estimate (deg/s), smoothed. A pause > 250 ms resets it.
    double instV = 0.0;
    if (m_lastAt >= 0) {
        const qint64 dt = now - m_lastAt;
        if (dt > 250 || dt < 0)
            m_velocity = 0.0;
        else
            instV = std::abs(deg) * 1000.0 / std::max<qint64>(dt, 1);
    }
    m_lastAt = now;
    const double a = std::clamp(m_cfg.smoothing, 0.0, 0.95);
    m_velocity = a * m_velocity + (1.0 - a) * instV;
    out.velocityDps = m_velocity;

    double accel = 1.0;
    if (m_cfg.acceleration > 0.0 && m_velocity > m_cfg.accelThresholdDps) {
        const double over = (m_velocity - m_cfg.accelThresholdDps) / m_cfg.accelThresholdDps;
        accel = std::min(1.0 + m_cfg.acceleration * over, m_cfg.maxAccelFactor);
    }

    double scaled = deg * m_cfg.sensitivity * accel * (fine ? m_cfg.fineFactor : 1.0);
    if (m_cfg.invert)
        scaled = -scaled;
    out.degrees = scaled;

    if (m_cfg.resetOnReverse && m_accum != 0.0 && std::signbit(m_accum) != std::signbit(scaled))
        m_accum = 0.0;
    m_accum += scaled;

    const double detent = std::max(m_cfg.detentDeg, 0.1);
    // Small epsilon so 3 x 3.333.. = 10 counts as a full step.
    const double eps = 1e-9 * detent;
    double whole = std::trunc((m_accum + (m_accum > 0 ? eps : -eps)) / detent);
    const int cap = std::max(1, m_cfg.maxStepsPerEvent);
    int steps = static_cast<int>(std::clamp(whole, -double(cap), double(cap)));
    m_accum -= steps * detent;
    // Bound carried excess so a burst cannot queue unbounded actions.
    const double bound = cap * detent;
    m_accum = std::clamp(m_accum, -bound, bound);
    if (std::abs(m_accum) < eps)
        m_accum = 0.0;
    out.steps = steps;
    return out;
}

HardwareMode hardwareModeFor(const HapticsRequest &req)
{
    HardwareMode m;
    const bool haptics = req.globalEnabled && req.toolEnabled;
    if (!haptics) {
        m.resolutionSteps = 3600;
        m.autoHaptics = false;
    } else {
        double d = std::max(req.detentDeg, 0.1);
        if (d < req.minHapticDetentDeg) {
            d = req.minHapticDetentDeg;
            m.fineHapticsClamped = true;
        }
        const long steps = std::lround(360.0 / d);
        m.resolutionSteps = static_cast<quint16>(std::clamp<long>(steps, 1, 3600));
        m.autoHaptics = true;
    }
    m.appliedDetentDeg = 360.0 / m.resolutionSteps;
    return m;
}

namespace hid {
QByteArray encodeModeFeature(const HardwareMode &m)
{
    const quint16 steps = std::clamp<quint16>(m.resolutionSteps, 0, 3600);
    QByteArray b(8, '\0');
    b[0] = char(kReportId);
    b[1] = char(steps & 0xff);
    b[2] = char((steps >> 8) & 0xff);
    b[3] = 0x00;                                 // repeat count
    b[4] = char(m.autoHaptics ? 0x03 : 0x02);    // auto trigger: click waveform / none
    b[5] = 0x00;                                 // waveform cutoff time
    b[6] = 0x00;                                 // retrigger period lo
    b[7] = 0x00;                                 // retrigger period hi
    return b;
}

QByteArray encodeManualHaptic(Waveform w, quint8 repeat, quint16 retriggerMs)
{
    retriggerMs = std::min<quint16>(retriggerMs, 2000);
    QByteArray b(5, '\0');
    b[0] = char(kReportId);
    b[1] = char(repeat);
    b[2] = char(static_cast<quint8>(w));
    b[3] = char(retriggerMs & 0xff);
    b[4] = char((retriggerMs >> 8) & 0xff);
    return b;
}
} // namespace hid

} // namespace opendial
