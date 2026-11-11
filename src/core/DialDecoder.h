// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <optional>

namespace opendial {

struct RawEvent {
    qint64 timeUs = 0;   // event timestamp (microseconds)
    quint16 type = 0;
    quint16 code = 0;
    qint32 value = 0;
};

// Device-level signals decoded from a SYN_REPORT-delimited evdev frame.
struct DialSignal {
    enum Kind { Button, Rotate, Unknown } kind = Unknown;
    qint64 timeUs = 0;
    bool down = false;     // Button
    qint32 units = 0;      // Rotate: raw REL_DIAL units (sign: + clockwise)
    RawEvent raw;          // Unknown: raw event for diagnostics
};

// Decodes evdev events from the "Surface Dial System Multi Axis" node
// (EV_KEY BTN_0, EV_REL REL_DIAL). Frames are emitted on SYN_REPORT; events
// within SYN_DROPPED are discarded until the next SYN_REPORT.
class DialDecoder {
public:
    // Discard rotation for guardMs after connect: the Dial buffers turns made
    // while asleep and floods them on reconnect.
    void onConnected(qint64 nowUs, int guardMs);
    QList<DialSignal> feed(const RawEvent &e);
    int droppedByGuard() const { return m_guardDropped; }
    int malformed() const { return m_malformed; }
    void reset();

private:
    QList<DialSignal> m_frame;
    bool m_dropping = false;
    qint64 m_guardUntilUs = 0;
    int m_guardDropped = 0;
    int m_malformed = 0;
};

// Raw HID input report 1 (diagnostics / hidraw fallback):
// [id=1][bit0 button, bit1 touch][int16 dial][int16 x][int16 y][u8 width]
struct HidInputReport {
    bool button = false;
    bool touch = false;
    qint16 dial = 0;
    std::optional<qint16> x, y;
};
std::optional<HidInputReport> decodeHidInputReport(const QByteArray &report);

// Replay fixtures: JSON lines {"t":us,"type":n,"code":n,"value":n} or evtest
// text lines ("Event: time 1.000000, type 2 (EV_REL), code 7 (REL_DIAL), value 1").
// Also accepts control lines {"connect":true} / {"disconnect":true}.
struct FixtureLine {
    enum Kind { Event, Connect, Disconnect } kind = Event;
    RawEvent event;
};
QList<FixtureLine> parseFixture(const QByteArray &data, QString *error = nullptr);

// Linux input constants (from linux/input-event-codes.h) used by the decoder.
namespace ev {
constexpr quint16 SYN = 0x00, KEY = 0x01, REL = 0x02, MSC = 0x04;
constexpr quint16 SYN_REPORT = 0, SYN_DROPPED = 3;
constexpr quint16 BTN_0 = 0x100;
constexpr quint16 REL_DIAL = 0x07, REL_WHEEL = 0x08, REL_HWHEEL = 0x06;
} // namespace ev

} // namespace opendial
