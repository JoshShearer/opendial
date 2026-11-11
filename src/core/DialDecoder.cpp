// SPDX-License-Identifier: MIT
#include "DialDecoder.h"

#include <QJsonDocument>
#include <QRegularExpression>

namespace opendial {

void DialDecoder::onConnected(qint64 nowUs, int guardMs)
{
    reset();
    m_guardUntilUs = nowUs + qint64(qMax(0, guardMs)) * 1000;
}

void DialDecoder::reset()
{
    m_frame.clear();
    m_dropping = false;
}

QList<DialSignal> DialDecoder::feed(const RawEvent &e)
{
    QList<DialSignal> out;
    if (e.type == ev::SYN) {
        if (e.code == ev::SYN_DROPPED) {
            // Kernel buffer overrun: the frame is incomplete. Discard until
            // the next SYN_REPORT (libevdev semantics, simplified).
            m_frame.clear();
            m_dropping = true;
            return out;
        }
        if (e.code == ev::SYN_REPORT) {
            if (m_dropping) {
                m_dropping = false;
                m_frame.clear();
                return out;
            }
            out = m_frame;
            m_frame.clear();
        }
        return out;
    }
    if (m_dropping)
        return out;
    if (m_frame.size() > 64) { // malformed: no SYN for a long run
        ++m_malformed;
        m_frame.clear();
    }
    DialSignal s;
    s.timeUs = e.timeUs;
    if (e.type == ev::KEY && e.code == ev::BTN_0) {
        if (e.value != 0 && e.value != 1) { // 2 = autorepeat: ignore
            if (e.value != 2)
                ++m_malformed;
            return out;
        }
        s.kind = DialSignal::Button;
        s.down = e.value == 1;
    } else if (e.type == ev::REL && e.code == ev::REL_DIAL) {
        if (e.timeUs < m_guardUntilUs) {
            ++m_guardDropped;
            return out;
        }
        if (e.value == 0)
            return out;
        // clamp to the descriptor's logical range
        s.kind = DialSignal::Rotate;
        s.units = qBound(-32767, e.value, 32767);
    } else if (e.type == ev::MSC) {
        return out; // MSC_SCAN etc.
    } else {
        s.kind = DialSignal::Unknown;
        s.raw = e;
    }
    m_frame.append(s);
    return out;
}

std::optional<HidInputReport> decodeHidInputReport(const QByteArray &r)
{
    if (r.size() < 4 || quint8(r.at(0)) != 0x01 || r.size() > 64)
        return std::nullopt;
    HidInputReport h;
    const quint8 b = quint8(r.at(1));
    h.button = b & 0x01;
    h.touch = b & 0x02;
    h.dial = qint16(quint16(quint8(r.at(2))) | (quint16(quint8(r.at(3))) << 8));
    if (r.size() >= 8) {
        h.x = qint16(quint16(quint8(r.at(4))) | (quint16(quint8(r.at(5))) << 8));
        h.y = qint16(quint16(quint8(r.at(6))) | (quint16(quint8(r.at(7))) << 8));
    }
    return h;
}

QList<FixtureLine> parseFixture(const QByteArray &data, QString *error)
{
    QList<FixtureLine> out;
    static const QRegularExpression evtest(
        QStringLiteral("^Event: time (\\d+)\\.(\\d{1,6}), type (\\d+) \\([^)]*\\), code (\\d+) \\([^)]*\\), value (-?\\d+)"));
    static const QRegularExpression evtestSyn(
        QStringLiteral("^Event: time (\\d+)\\.(\\d{1,6}), (-{10,}|-+ SYN_REPORT -+)"));
    int lineNo = 0;
    for (const QByteArray &rawLine : data.split('\n')) {
        ++lineNo;
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('{'))) {
            QJsonParseError pe{};
            const auto o = QJsonDocument::fromJson(line.toUtf8(), &pe).object();
            if (pe.error != QJsonParseError::NoError) {
                if (error) *error = QStringLiteral("line %1: %2").arg(lineNo).arg(pe.errorString());
                return {};
            }
            FixtureLine fl;
            if (o.value(QLatin1String("connect")).toBool()) {
                fl.kind = FixtureLine::Connect;
                fl.event.timeUs = o.value(QLatin1String("t")).toInteger();
            } else if (o.value(QLatin1String("disconnect")).toBool()) {
                fl.kind = FixtureLine::Disconnect;
                fl.event.timeUs = o.value(QLatin1String("t")).toInteger();
            } else {
                fl.event.timeUs = o.value(QLatin1String("t")).toInteger();
                fl.event.type = quint16(o.value(QLatin1String("type")).toInt());
                fl.event.code = quint16(o.value(QLatin1String("code")).toInt());
                fl.event.value = o.value(QLatin1String("value")).toInt();
            }
            out << fl;
            continue;
        }
        auto m = evtest.match(line);
        if (m.hasMatch()) {
            FixtureLine fl;
            QString frac = m.captured(2).leftJustified(6, QLatin1Char('0'));
            fl.event.timeUs = m.captured(1).toLongLong() * 1000000 + frac.toLongLong();
            fl.event.type = quint16(m.captured(3).toUInt());
            fl.event.code = quint16(m.captured(4).toUInt());
            fl.event.value = m.captured(5).toInt();
            out << fl;
            continue;
        }
        auto s = evtestSyn.match(line);
        if (s.hasMatch()) {
            FixtureLine fl;
            QString frac = s.captured(2).leftJustified(6, QLatin1Char('0'));
            fl.event.timeUs = s.captured(1).toLongLong() * 1000000 + frac.toLongLong();
            fl.event.type = ev::SYN;
            fl.event.code = ev::SYN_REPORT;
            out << fl;
            continue;
        }
        // evtest header lines ("Input driver version", "Event code", ...) are skipped
    }
    return out;
}

} // namespace opendial
