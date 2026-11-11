// SPDX-License-Identifier: MIT
#pragma once

#include "DialDecoder.h"
#include "Rotation.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSocketNotifier>
#include <QThread>

struct udev;
struct udev_monitor;
struct udev_device;

namespace opendial {

struct DeviceInfo {
    QString name;
    QString evdevNode;
    QString controlNode;   // "Surface Dial System Control" (diagnostics only)
    QString hidrawNode;
    QString hidSysPath;
    QString uniq;          // BT address (treated as sensitive: redacted in exports)
    quint16 bus = 0, vendor = 0, product = 0, version = 0;
    bool evdevReadable = false;
    bool hidrawWritable = false;
    bool grabbed = false;
    QString hidrawError;
    QString batteryPath;   // power_supply node if the kernel exposes one
    QJsonObject toJson(bool redact) const;
};

// Writes HID feature/output reports on its own thread so a slow BLE round
// trip never stalls the input pipeline.
class HapticsWorker : public QObject {
    Q_OBJECT
public:
    explicit HapticsWorker(QObject *parent = nullptr) : QObject(parent) {}
    ~HapticsWorker() override;
public Q_SLOTS:
    void openNode(const QString &hidrawNode);
    void closeNode();
    void applyMode(quint16 resolutionSteps, bool autoHaptics, quint64 requestId);
    void buzz(int waveform, int repeat);
Q_SIGNALS:
    void opened(bool ok, const QString &error);
    void modeApplied(quint16 resolutionSteps, bool autoHaptics, bool ok, const QString &error, quint64 requestId);
private:
    int m_fd = -1;
};

class DialDevice : public QObject {
    Q_OBJECT
public:
    explicit DialDevice(QObject *parent = nullptr);
    ~DialDevice() override;

    void start();
    void setGrab(bool grab) { m_wantGrab = grab; }
    void setConnectGuardMs(int ms) { m_guardMs = ms; }

    bool isConnected() const { return m_evFd >= 0; }
    const DeviceInfo &info() const { return m_info; }
    bool hapticsAvailable() const { return m_hapticsOpen; }
    HardwareMode appliedMode() const { return m_applied; }
    HardwareMode requestedMode() const { return m_requested; }
    QString hapticsError() const { return m_hapticsError; }
    int guardDropped() const { return m_decoder.droppedByGuard(); }
    int malformed() const { return m_decoder.malformed(); }
    std::optional<int> batteryPercent() const;

    // Enumerate every matching node currently present (diagnostics).
    QJsonArray scan() const;

    void requestMode(const HardwareMode &m);
    void buzz(hid::Waveform w, int repeat = 1);
    void forceReconnect();

    // When true, raw evdev events are also emitted via rawEvent().
    void setRawStreaming(bool on) { m_rawStreaming = on; }

Q_SIGNALS:
    void connected(const QJsonObject &info);
    void disconnected(const QString &reason);
    void signalReceived(const opendial::DialSignal &s);
    void rawEvent(const opendial::RawEvent &e, bool control);
    void hapticsChanged();

private:
    void onUdev();
    void onEvdevReadable();
    void onControlReadable();
    void tryOpen(udev_device *dev);
    void closeDevice(const QString &reason);
    bool matchesDial(udev_device *dev, bool *isControl) const;
    void findHidraw();
    void findBattery();

    struct udev *m_udev = nullptr;
    udev_monitor *m_mon = nullptr;
    QSocketNotifier *m_monNotifier = nullptr;
    int m_evFd = -1;
    int m_ctlFd = -1;
    QSocketNotifier *m_evNotifier = nullptr;
    QSocketNotifier *m_ctlNotifier = nullptr;
    DeviceInfo m_info;
    DialDecoder m_decoder;
    bool m_wantGrab = true;
    int m_guardMs = 800;
    bool m_rawStreaming = false;

    QThread m_hapticsThread;
    HapticsWorker *m_worker = nullptr;
    bool m_hapticsOpen = false;
    QString m_hapticsError;
    HardwareMode m_applied;     // last acknowledged by the device
    HardwareMode m_requested;
    quint64 m_requestSeq = 0;
};

} // namespace opendial

Q_DECLARE_METATYPE(opendial::DialSignal)
Q_DECLARE_METATYPE(opendial::RawEvent)
