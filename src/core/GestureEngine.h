// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <QtGlobal>
#include <optional>
#include <vector>

namespace opendial {

// Higher-level gestures derived from physical button/rotation signals.
// Physical events are always reported too (PhysicalDown/PhysicalUp) so
// diagnostics and plugins can distinguish raw from synthetic gestures.
enum class GestureKind {
    PhysicalDown,
    PhysicalUp,
    Click,       // count = 1, 2 or 3
    LongPress,   // hold threshold reached without rotation
    Release,     // button released after a long press or hold-and-turn
    Rotate,      // degrees signed (+ = clockwise), pressed flag
    LatchChanged // click-to-toggle accessibility latch changed (latched flag)
};

struct GestureEvent {
    GestureKind kind;
    qint64 timeMs = 0;
    int count = 0;          // Click
    double degrees = 0.0;   // Rotate
    bool pressed = false;   // Rotate: button physically held or latched
    bool afterLongPress = false; // Rotate: button still held after long press fired
    bool latched = false;   // LatchChanged
};

QString gestureKindName(GestureKind k);

struct GestureConfig {
    int longPressMs = 500;          // Windows default feels ~0.5 s
    int doubleClickMs = 300;
    int maxClickCount = 1;          // 1 = no multi-click wait (lowest latency), 2 or 3
    int debounceMs = 8;
    double holdRotateThresholdDeg = 3.0; // hysteresis before a press becomes hold-and-turn
    bool longPressEnabled = true;
    bool clickToggleLatch = false;  // accessibility: click latches "pressed" state
};

// Deterministic state machine. Caller supplies monotonic timestamps and must
// call onTimer(now) at or after nextDeadline().
class GestureEngine {
public:
    explicit GestureEngine(GestureConfig cfg = {});

    void setConfig(const GestureConfig &cfg);
    const GestureConfig &config() const { return m_cfg; }

    std::vector<GestureEvent> onButton(bool down, qint64 nowMs);
    std::vector<GestureEvent> onRotate(double degrees, qint64 nowMs);
    std::vector<GestureEvent> onTimer(qint64 nowMs);

    // Deadline (ms) at which onTimer must be invoked, if any.
    std::optional<qint64> nextDeadline() const;

    // Drop all transient state (device disconnect, lock screen). Never emits
    // actions: a disconnect must not trigger a click.
    void reset();

    bool isDown() const { return m_down; }
    bool isLatched() const { return m_latched; }

private:
    void emitClick(std::vector<GestureEvent> &out, int count, qint64 now);

    GestureConfig m_cfg;
    bool m_down = false;
    bool m_longFired = false;
    bool m_rotatedWhilePressed = false;
    bool m_latched = false;
    double m_pressAccum = 0.0;
    qint64 m_downAt = 0;
    qint64 m_upAt = 0;
    qint64 m_lastTransition = -1000000;
    int m_pendingClicks = 0;
};

} // namespace opendial
