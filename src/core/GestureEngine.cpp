// SPDX-License-Identifier: MIT
#include "GestureEngine.h"

#include <algorithm>
#include <cmath>

namespace opendial {

QString gestureKindName(GestureKind k)
{
    switch (k) {
    case GestureKind::PhysicalDown: return QStringLiteral("physical_down");
    case GestureKind::PhysicalUp: return QStringLiteral("physical_up");
    case GestureKind::Click: return QStringLiteral("click");
    case GestureKind::LongPress: return QStringLiteral("long_press");
    case GestureKind::Release: return QStringLiteral("release");
    case GestureKind::Rotate: return QStringLiteral("rotate");
    case GestureKind::LatchChanged: return QStringLiteral("latch_changed");
    }
    return QStringLiteral("unknown");
}

GestureEngine::GestureEngine(GestureConfig cfg) { setConfig(cfg); }

void GestureEngine::setConfig(const GestureConfig &cfg)
{
    m_cfg = cfg;
    m_cfg.longPressMs = std::clamp(m_cfg.longPressMs, 150, 5000);
    m_cfg.doubleClickMs = std::clamp(m_cfg.doubleClickMs, 80, 1500);
    m_cfg.maxClickCount = std::clamp(m_cfg.maxClickCount, 1, 3);
    m_cfg.debounceMs = std::clamp(m_cfg.debounceMs, 0, 100);
    m_cfg.holdRotateThresholdDeg = std::clamp(m_cfg.holdRotateThresholdDeg, 0.0, 45.0);
    if (!m_cfg.clickToggleLatch)
        m_latched = false;
}

void GestureEngine::reset()
{
    m_down = false;
    m_longFired = false;
    m_rotatedWhilePressed = false;
    m_pressAccum = 0.0;
    m_pendingClicks = 0;
}

void GestureEngine::emitClick(std::vector<GestureEvent> &out, int count, qint64 now)
{
    m_pendingClicks = 0;
    if (m_cfg.clickToggleLatch && count == 1) {
        m_latched = !m_latched;
        GestureEvent e{GestureKind::LatchChanged, now};
        e.latched = m_latched;
        out.push_back(e);
        return;
    }
    GestureEvent e{GestureKind::Click, now};
    e.count = count;
    out.push_back(e);
}

std::vector<GestureEvent> GestureEngine::onButton(bool down, qint64 now)
{
    std::vector<GestureEvent> out;
    if (down == m_down)
        return out; // duplicate state report (e.g. kernel repeat) - ignore
    if (down && now - m_lastTransition < m_cfg.debounceMs) {
        // A press within the debounce window after a release is contact
        // bounce. Only presses are suppressed: suppressing a release could
        // leave the button logically stuck down.
        return out;
    }
    m_lastTransition = now;

    if (down) {
        m_down = true;
        m_downAt = now;
        m_longFired = false;
        m_rotatedWhilePressed = false;
        m_pressAccum = 0.0;
        out.push_back({GestureKind::PhysicalDown, now});
        return out;
    }

    m_down = false;
    m_upAt = now;
    out.push_back({GestureKind::PhysicalUp, now});
    if (m_longFired || m_rotatedWhilePressed) {
        m_pendingClicks = 0;
        out.push_back({GestureKind::Release, now});
        return out;
    }
    ++m_pendingClicks;
    if (m_pendingClicks >= m_cfg.maxClickCount)
        emitClick(out, m_pendingClicks, now);
    return out;
}

std::vector<GestureEvent> GestureEngine::onRotate(double degrees, qint64 now)
{
    std::vector<GestureEvent> out;
    if (degrees == 0.0 || !std::isfinite(degrees))
        return out;

    if (m_down) {
        if (m_longFired) {
            GestureEvent e{GestureKind::Rotate, now};
            e.degrees = degrees;
            e.pressed = true;
            e.afterLongPress = true;
            out.push_back(e);
            return out;
        }
        if (!m_rotatedWhilePressed) {
            m_pressAccum += degrees;
            if (std::abs(m_pressAccum) < m_cfg.holdRotateThresholdDeg)
                return out; // hysteresis: tiny wobble while clicking is not a turn
            m_rotatedWhilePressed = true;
            m_pendingClicks = 0; // a hold-and-turn cancels any multi-click sequence
            degrees = m_pressAccum;
            m_pressAccum = 0.0;
        }
        GestureEvent e{GestureKind::Rotate, now};
        e.degrees = degrees;
        e.pressed = true;
        out.push_back(e);
        return out;
    }

    if (m_pendingClicks > 0) // turning ends the multi-click window early
        emitClick(out, m_pendingClicks, now);

    GestureEvent e{GestureKind::Rotate, now};
    e.degrees = degrees;
    e.pressed = m_latched;
    out.push_back(e);
    return out;
}

std::optional<qint64> GestureEngine::nextDeadline() const
{
    if (m_down && !m_longFired && !m_rotatedWhilePressed && m_cfg.longPressEnabled)
        return m_downAt + m_cfg.longPressMs;
    if (!m_down && m_pendingClicks > 0)
        return m_upAt + m_cfg.doubleClickMs;
    return std::nullopt;
}

std::vector<GestureEvent> GestureEngine::onTimer(qint64 now)
{
    std::vector<GestureEvent> out;
    if (m_down && !m_longFired && !m_rotatedWhilePressed && m_cfg.longPressEnabled
        && now >= m_downAt + m_cfg.longPressMs) {
        m_longFired = true;
        m_pendingClicks = 0;
        out.push_back({GestureKind::LongPress, now});
    } else if (!m_down && m_pendingClicks > 0 && now >= m_upAt + m_cfg.doubleClickMs) {
        emitClick(out, m_pendingClicks, now);
    }
    return out;
}

} // namespace opendial
