// SPDX-License-Identifier: MIT
#include "DialDevice.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QTimer>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <libudev.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcDevice, "opendial.device")

namespace opendial {

namespace {
constexpr const char *kHidId = "0005:0000045E:0000091B"; // BLE : Microsoft : Surface Dial

bool testBit(const unsigned long *bits, int bit)
{
    return bits[bit / (8 * sizeof(long))] & (1UL << (bit % (8 * sizeof(long))));
}

qint64 monotonicUs()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return qint64(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
}

QString prop(udev_device *d, const char *k)
{
    const char *v = d ? udev_device_get_property_value(d, k) : nullptr;
    return v ? QString::fromUtf8(v) : QString();
}
} // namespace

QJsonObject DeviceInfo::toJson(bool redact) const
{
    auto hex = [](quint16 v) { return QStringLiteral("%1").arg(v, 4, 16, QLatin1Char('0')); };
    return QJsonObject{{QStringLiteral("name"), name},
                       {QStringLiteral("evdevNode"), evdevNode},
                       {QStringLiteral("controlNode"), controlNode},
                       {QStringLiteral("hidrawNode"), hidrawNode},
                       {QStringLiteral("hidSysPath"), redact ? QString() : hidSysPath},
                       {QStringLiteral("uniq"), redact && !uniq.isEmpty() ? QStringLiteral("<redacted>") : uniq},
                       {QStringLiteral("bus"), hex(bus)},
                       {QStringLiteral("vendor"), hex(vendor)},
                       {QStringLiteral("product"), hex(product)},
                       {QStringLiteral("version"), hex(version)},
                       {QStringLiteral("evdevReadable"), evdevReadable},
                       {QStringLiteral("hidrawWritable"), hidrawWritable},
                       {QStringLiteral("grabbed"), grabbed},
                       {QStringLiteral("hidrawError"), hidrawError},
                       {QStringLiteral("batteryPath"), redact ? QString() : batteryPath}};
}

// ------------------------------------------------------------ HapticsWorker

HapticsWorker::~HapticsWorker() { closeNode(); }

void HapticsWorker::openNode(const QString &node)
{
    closeNode();
    m_fd = ::open(QFile::encodeName(node).constData(), O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (m_fd < 0) {
        const QString e = QString::fromLocal8Bit(strerror(errno));
        Q_EMIT opened(false, errno == EACCES ? QStringLiteral("permission denied on %1 (install the OpenDial udev rule and re-plug/re-connect the Dial)").arg(node)
                                             : QStringLiteral("%1: %2").arg(node, e));
        return;
    }
    // Verify we really talk to a Surface Dial before writing anything.
    hidraw_devinfo info{};
    if (ioctl(m_fd, HIDIOCGRAWINFO, &info) < 0 || quint16(info.vendor) != hid::kVendorMicrosoft
        || quint16(info.product) != hid::kProductSurfaceDial) {
        closeNode();
        Q_EMIT opened(false, QStringLiteral("%1 is not a Surface Dial").arg(node));
        return;
    }
    Q_EMIT opened(true, QString());
}

void HapticsWorker::closeNode()
{
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
}

void HapticsWorker::applyMode(quint16 steps, bool autoHaptics, quint64 requestId)
{
    if (m_fd < 0) {
        Q_EMIT modeApplied(0, autoHaptics, false, QStringLiteral("hidraw not open"), requestId);
        return;
    }
    HardwareMode m;
    m.resolutionSteps = steps;
    m.autoHaptics = autoHaptics;
    QByteArray buf = hid::encodeModeFeature(m);
    int r = -1;
    for (int attempt = 0; attempt < 3; ++attempt) {
        r = ioctl(m_fd, HIDIOCSFEATURE(buf.size()), buf.data());
        if (r >= 0 || (errno != EAGAIN && errno != EINTR && errno != ETIMEDOUT))
            break;
        QThread::msleep(15);
    }
    if (r < 0) {
        Q_EMIT modeApplied(0, autoHaptics, false, QStringLiteral("HIDIOCSFEATURE failed: %1").arg(QString::fromLocal8Bit(strerror(errno))), requestId);
        return;
    }
    // Read back to verify the device accepted the multiplier.
    QByteArray rd(8, '\0');
    rd[0] = char(hid::kReportId);
    if (ioctl(m_fd, HIDIOCGFEATURE(rd.size()), rd.data()) >= 0) {
        const quint16 got = quint16(quint8(rd[1])) | (quint16(quint8(rd[2])) << 8);
        if (got != steps) {
            Q_EMIT modeApplied(got, autoHaptics, false, QStringLiteral("device reports resolution %1 after setting %2").arg(got).arg(steps), requestId);
            return;
        }
    }
    Q_EMIT modeApplied(steps, autoHaptics, true, QString(), requestId);
}

void HapticsWorker::buzz(int waveform, int repeat)
{
    if (m_fd < 0)
        return;
    const QByteArray b = hid::encodeManualHaptic(static_cast<hid::Waveform>(waveform), quint8(qBound(0, repeat, 10)));
    if (::write(m_fd, b.constData(), size_t(b.size())) < 0)
        qCDebug(lcDevice) << "haptic output report failed:" << strerror(errno);
}

// --------------------------------------------------------------- DialDevice

DialDevice::DialDevice(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<opendial::DialSignal>();
    qRegisterMetaType<opendial::RawEvent>();
    m_worker = new HapticsWorker;
    m_worker->moveToThread(&m_hapticsThread);
    connect(&m_hapticsThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &HapticsWorker::opened, this, [this](bool ok, const QString &err) {
        m_hapticsOpen = ok;
        m_hapticsError = err;
        m_info.hidrawWritable = ok;
        m_info.hidrawError = err;
        if (!ok)
            qCWarning(lcDevice) << "haptics unavailable:" << err;
        // Kernel default resolution after connect (verified via read-back once we can write)
        m_applied = HardwareMode{};
        if (ok)
            requestMode(m_requested);
        Q_EMIT hapticsChanged();
    });
    connect(m_worker, &HapticsWorker::modeApplied, this,
            [this](quint16 steps, bool autoH, bool ok, const QString &err, quint64 id) {
                // steps == 0: nothing reached the device; keep the last known mode.
                // !ok with steps != 0: read-back shows what the device really uses.
                if (steps != 0) {
                    m_applied.resolutionSteps = steps;
                    if (ok)
                        m_applied.autoHaptics = autoH;
                    m_applied.appliedDetentDeg = 360.0 / m_applied.resolutionSteps;
                }
                m_hapticsError = ok ? QString() : err;
                if (!ok)
                    qCWarning(lcDevice) << "haptic mode not applied:" << err;
                Q_EMIT hapticsChanged();
            });
    m_hapticsThread.setObjectName(QStringLiteral("opendial-haptics"));
    m_hapticsThread.start();
}

DialDevice::~DialDevice()
{
    closeDevice(QStringLiteral("shutdown"));
    m_hapticsThread.quit();
    m_hapticsThread.wait(2000);
    if (m_mon)
        udev_monitor_unref(m_mon);
    if (m_udev)
        udev_unref(m_udev);
}

void DialDevice::start()
{
    m_udev = udev_new();
    if (!m_udev) {
        qCCritical(lcDevice) << "udev_new failed";
        return;
    }
    m_mon = udev_monitor_new_from_netlink(m_udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(m_mon, "input", nullptr);
    udev_monitor_filter_add_match_subsystem_devtype(m_mon, "hidraw", nullptr);
    udev_monitor_enable_receiving(m_mon);
    m_monNotifier = new QSocketNotifier(udev_monitor_get_fd(m_mon), QSocketNotifier::Read, this);
    connect(m_monNotifier, &QSocketNotifier::activated, this, &DialDevice::onUdev);

    // Enumerate devices already present (paired + connected before we started).
    udev_enumerate *en = udev_enumerate_new(m_udev);
    udev_enumerate_add_match_subsystem(en, "input");
    udev_enumerate_add_match_sysname(en, "event*");
    udev_enumerate_scan_devices(en);
    udev_list_entry *e;
    udev_list_entry_foreach(e, udev_enumerate_get_list_entry(en))
    {
        udev_device *d = udev_device_new_from_syspath(m_udev, udev_list_entry_get_name(e));
        if (d) {
            tryOpen(d);
            udev_device_unref(d);
        }
    }
    udev_enumerate_unref(en);
    if (!isConnected())
        qCInfo(lcDevice) << "no Surface Dial connected; waiting for hotplug";
}

bool DialDevice::matchesDial(udev_device *dev, bool *isControl) const
{
    const char *sysname = udev_device_get_sysname(dev);
    if (!sysname || strncmp(sysname, "event", 5) != 0)
        return false;
    udev_device *hidp = udev_device_get_parent_with_subsystem_devtype(dev, "hid", nullptr);
    if (!hidp || prop(hidp, "HID_ID").compare(QLatin1String(kHidId), Qt::CaseInsensitive) != 0)
        return false;
    udev_device *inp = udev_device_get_parent_with_subsystem_devtype(dev, "input", nullptr);
    const QString name = prop(inp, "NAME");
    if (isControl)
        *isControl = name.contains(QLatin1String("Control"), Qt::CaseInsensitive);
    return true;
}

void DialDevice::tryOpen(udev_device *dev)
{
    bool control = false;
    if (!matchesDial(dev, &control))
        return;
    const char *node = udev_device_get_devnode(dev);
    if (!node)
        return;
    const QString path = QString::fromUtf8(node);

    if (control) {
        if (m_ctlFd >= 0)
            return;
        m_ctlFd = ::open(node, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (m_ctlFd >= 0) {
            m_info.controlNode = path;
            m_ctlNotifier = new QSocketNotifier(m_ctlFd, QSocketNotifier::Read, this);
            connect(m_ctlNotifier, &QSocketNotifier::activated, this, &DialDevice::onControlReadable);
        }
        return;
    }
    if (m_evFd >= 0)
        return; // already have a Dial (single active device; see docs)

    const int fd = ::open(node, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        qCWarning(lcDevice) << "cannot open" << path << strerror(errno)
                            << "- install the OpenDial udev rule (uaccess) for the Surface Dial";
        m_info.evdevNode = path;
        m_info.evdevReadable = false;
        Q_EMIT disconnected(QStringLiteral("permission denied on %1").arg(path));
        return;
    }
    // Verify capabilities: must report REL_DIAL.
    unsigned long relBits[(REL_MAX + 1) / (8 * sizeof(long)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof relBits), relBits) < 0 || !testBit(relBits, REL_DIAL)) {
        ::close(fd);
        return;
    }
    input_id id{};
    ioctl(fd, EVIOCGID, &id);
    char nameBuf[256] = {};
    ioctl(fd, EVIOCGNAME(sizeof nameBuf - 1), nameBuf);
    char uniqBuf[64] = {};
    ioctl(fd, EVIOCGUNIQ(sizeof uniqBuf - 1), uniqBuf);
    // Use CLOCK_MONOTONIC timestamps to compare against our own clock.
    int clk = CLOCK_MONOTONIC;
    ioctl(fd, EVIOCSCLOCKID, &clk);

    m_evFd = fd;
    m_info.name = QString::fromUtf8(nameBuf);
    m_info.evdevNode = path;
    m_info.uniq = QString::fromUtf8(uniqBuf);
    m_info.bus = id.bustype;
    m_info.vendor = id.vendor;
    m_info.product = id.product;
    m_info.version = id.version;
    m_info.evdevReadable = true;
    m_info.grabbed = false;
    if (m_wantGrab) {
        // Exclusive access: nothing else (libinput, another daemon) consumes
        // the Dial's events in parallel.
        if (ioctl(fd, EVIOCGRAB, 1) == 0)
            m_info.grabbed = true;
        else
            qCWarning(lcDevice) << "EVIOCGRAB failed (another process grabbed the Dial?)" << strerror(errno);
    }
    udev_device *hidp = udev_device_get_parent_with_subsystem_devtype(dev, "hid", nullptr);
    m_info.hidSysPath = hidp ? QString::fromUtf8(udev_device_get_syspath(hidp)) : QString();

    m_evNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_evNotifier, &QSocketNotifier::activated, this, &DialDevice::onEvdevReadable);
    m_decoder.onConnected(monotonicUs(), m_guardMs);
    m_applied = HardwareMode{};
    findHidraw();
    findBattery();
    qCInfo(lcDevice) << "Surface Dial connected:" << m_info.name << path << "grabbed:" << m_info.grabbed;
    Q_EMIT connected(m_info.toJson(true));

    // The control node may already exist; scan siblings.
    if (m_ctlFd < 0 && hidp) {
        udev_enumerate *en = udev_enumerate_new(m_udev);
        udev_enumerate_add_match_subsystem(en, "input");
        udev_enumerate_add_match_parent(en, hidp);
        udev_enumerate_scan_devices(en);
        udev_list_entry *e;
        udev_list_entry_foreach(e, udev_enumerate_get_list_entry(en))
        {
            udev_device *d = udev_device_new_from_syspath(m_udev, udev_list_entry_get_name(e));
            if (d) {
                bool c = false;
                if (matchesDial(d, &c) && c)
                    tryOpen(d);
                udev_device_unref(d);
            }
        }
        udev_enumerate_unref(en);
    }
}

void DialDevice::findHidraw()
{
    if (m_info.hidSysPath.isEmpty())
        return;
    udev_enumerate *en = udev_enumerate_new(m_udev);
    udev_enumerate_add_match_subsystem(en, "hidraw");
    udev_enumerate_scan_devices(en);
    udev_list_entry *e;
    QString found;
    udev_list_entry_foreach(e, udev_enumerate_get_list_entry(en))
    {
        udev_device *d = udev_device_new_from_syspath(m_udev, udev_list_entry_get_name(e));
        if (!d)
            continue;
        udev_device *p = udev_device_get_parent_with_subsystem_devtype(d, "hid", nullptr);
        if (p && QString::fromUtf8(udev_device_get_syspath(p)) == m_info.hidSysPath && udev_device_get_devnode(d))
            found = QString::fromUtf8(udev_device_get_devnode(d));
        udev_device_unref(d);
    }
    udev_enumerate_unref(en);
    m_info.hidrawNode = found;
    if (found.isEmpty()) {
        m_hapticsOpen = false;
        m_hapticsError = QStringLiteral("no hidraw node for the Dial (kernel CONFIG_HIDRAW?)");
        Q_EMIT hapticsChanged();
        return;
    }
    QMetaObject::invokeMethod(m_worker, "openNode", Qt::QueuedConnection, Q_ARG(QString, found));
}

void DialDevice::findBattery()
{
    m_info.batteryPath.clear();
    // hid-input exposes /sys/class/power_supply/hid-<uniq>-battery when the
    // HID descriptor reports battery strength. The Surface Dial's descriptor
    // does not, so this normally stays empty (BlueZ Battery1 is checked too).
    QDir ps(QStringLiteral("/sys/class/power_supply"));
    for (const auto &n : ps.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!m_info.uniq.isEmpty() && n.contains(m_info.uniq, Qt::CaseInsensitive))
            m_info.batteryPath = ps.filePath(n);
    }
}

std::optional<int> DialDevice::batteryPercent() const
{
    if (m_info.batteryPath.isEmpty())
        return std::nullopt;
    QFile f(m_info.batteryPath + QStringLiteral("/capacity"));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    bool ok = false;
    const int v = f.readAll().trimmed().toInt(&ok);
    return ok ? std::optional<int>(v) : std::nullopt;
}

void DialDevice::onUdev()
{
    udev_device *d = udev_monitor_receive_device(m_mon);
    if (!d)
        return;
    const QString action = QString::fromUtf8(udev_device_get_action(d) ? udev_device_get_action(d) : "");
    const QString node = QString::fromUtf8(udev_device_get_devnode(d) ? udev_device_get_devnode(d) : "");
    const QString subsystem = QString::fromUtf8(udev_device_get_subsystem(d) ? udev_device_get_subsystem(d) : "");
    if (action == QLatin1String("add") && subsystem == QLatin1String("input")) {
        // udev applies ACLs (uaccess) right after "add"; open on the next turn.
        const QString syspath = QString::fromUtf8(udev_device_get_syspath(d));
        QTimer::singleShot(150, this, [this, syspath] {
            udev_device *dd = udev_device_new_from_syspath(m_udev, syspath.toUtf8().constData());
            if (dd) {
                tryOpen(dd);
                udev_device_unref(dd);
            }
        });
    } else if (action == QLatin1String("add") && subsystem == QLatin1String("hidraw") && isConnected() && m_info.hidrawNode.isEmpty()) {
        QTimer::singleShot(150, this, &DialDevice::findHidraw);
    } else if (action == QLatin1String("remove") && !node.isEmpty()) {
        if (node == m_info.evdevNode)
            closeDevice(QStringLiteral("device removed"));
        else if (node == m_info.controlNode && m_ctlFd >= 0) {
            delete m_ctlNotifier;
            m_ctlNotifier = nullptr;
            ::close(m_ctlFd);
            m_ctlFd = -1;
            m_info.controlNode.clear();
        }
    }
    udev_device_unref(d);
}

void DialDevice::onEvdevReadable()
{
    input_event buf[64];
    for (;;) {
        const ssize_t n = ::read(m_evFd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR)
                return;
            closeDevice(QStringLiteral("read error: %1").arg(QString::fromLocal8Bit(strerror(errno))));
            return;
        }
        if (n == 0) {
            closeDevice(QStringLiteral("end of stream"));
            return;
        }
        if (n % ssize_t(sizeof(input_event)) != 0) {
            qCWarning(lcDevice) << "short evdev read" << n;
            continue;
        }
        const int count = int(n / ssize_t(sizeof(input_event)));
        for (int i = 0; i < count; ++i) {
            RawEvent e;
            e.timeUs = qint64(buf[i].input_event_sec) * 1000000 + buf[i].input_event_usec;
            e.type = buf[i].type;
            e.code = buf[i].code;
            e.value = buf[i].value;
            if (m_rawStreaming)
                Q_EMIT rawEvent(e, false);
            for (const auto &s : m_decoder.feed(e))
                Q_EMIT signalReceived(s);
        }
        if (count < 64)
            return;
    }
}

void DialDevice::onControlReadable()
{
    input_event buf[32];
    const ssize_t n = ::read(m_ctlFd, buf, sizeof buf);
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        delete m_ctlNotifier;
        m_ctlNotifier = nullptr;
        ::close(m_ctlFd);
        m_ctlFd = -1;
        return;
    }
    if (!m_rawStreaming)
        return;
    for (int i = 0; i < int(n / ssize_t(sizeof(input_event))); ++i) {
        RawEvent e{qint64(buf[i].input_event_sec) * 1000000 + buf[i].input_event_usec, buf[i].type, buf[i].code, buf[i].value};
        Q_EMIT rawEvent(e, true);
    }
}

void DialDevice::closeDevice(const QString &reason)
{
    if (m_evFd < 0)
        return;
    delete m_evNotifier;
    m_evNotifier = nullptr;
    if (m_info.grabbed)
        ioctl(m_evFd, EVIOCGRAB, 0);
    ::close(m_evFd);
    m_evFd = -1;
    if (m_ctlFd >= 0) {
        delete m_ctlNotifier;
        m_ctlNotifier = nullptr;
        ::close(m_ctlFd);
        m_ctlFd = -1;
    }
    QMetaObject::invokeMethod(m_worker, "closeNode", Qt::QueuedConnection);
    m_hapticsOpen = false;
    m_decoder.reset();
    const QString keepNode = m_info.evdevNode;
    m_info = DeviceInfo{};
    m_info.evdevNode = keepNode;
    qCInfo(lcDevice) << "Surface Dial disconnected:" << reason;
    Q_EMIT disconnected(reason);
    Q_EMIT hapticsChanged();
}

void DialDevice::forceReconnect()
{
    closeDevice(QStringLiteral("reconnect requested"));
    QTimer::singleShot(300, this, [this] {
        udev_enumerate *en = udev_enumerate_new(m_udev);
        udev_enumerate_add_match_subsystem(en, "input");
        udev_enumerate_add_match_sysname(en, "event*");
        udev_enumerate_scan_devices(en);
        udev_list_entry *e;
        udev_list_entry_foreach(e, udev_enumerate_get_list_entry(en))
        {
            udev_device *d = udev_device_new_from_syspath(m_udev, udev_list_entry_get_name(e));
            if (d) {
                tryOpen(d);
                udev_device_unref(d);
            }
        }
        udev_enumerate_unref(en);
    });
}

QJsonArray DialDevice::scan() const
{
    QJsonArray out;
    if (!m_udev)
        return out;
    udev_enumerate *en = udev_enumerate_new(m_udev);
    udev_enumerate_add_match_property(en, "HID_ID", kHidId);
    udev_enumerate_scan_devices(en);
    udev_list_entry *e;
    udev_list_entry_foreach(e, udev_enumerate_get_list_entry(en))
    {
        udev_device *hid = udev_device_new_from_syspath(m_udev, udev_list_entry_get_name(e));
        if (!hid)
            continue;
        QJsonObject o{{QStringLiteral("hidName"), prop(hid, "HID_NAME")},
                      {QStringLiteral("driver"), QString::fromUtf8(udev_device_get_driver(hid) ? udev_device_get_driver(hid) : "")}};
        QJsonArray nodes;
        udev_enumerate *ch = udev_enumerate_new(m_udev);
        udev_enumerate_add_match_parent(ch, hid);
        udev_enumerate_scan_devices(ch);
        udev_list_entry *c;
        udev_list_entry_foreach(c, udev_enumerate_get_list_entry(ch))
        {
            udev_device *d = udev_device_new_from_syspath(m_udev, udev_list_entry_get_name(c));
            if (d && udev_device_get_devnode(d)) {
                const QString n = QString::fromUtf8(udev_device_get_devnode(d));
                const QByteArray nb = QFile::encodeName(n);
                nodes.append(QJsonObject{{QStringLiteral("node"), n},
                                         {QStringLiteral("subsystem"), QString::fromUtf8(udev_device_get_subsystem(d))},
                                         {QStringLiteral("readable"), ::access(nb.constData(), R_OK) == 0},
                                         {QStringLiteral("writable"), ::access(nb.constData(), W_OK) == 0}});
            }
            if (d)
                udev_device_unref(d);
        }
        udev_enumerate_unref(ch);
        o.insert(QStringLiteral("nodes"), nodes);
        out.append(o);
        udev_device_unref(hid);
    }
    udev_enumerate_unref(en);
    return out;
}

void DialDevice::requestMode(const HardwareMode &m)
{
    m_requested = m;
    if (!m_hapticsOpen) {
        Q_EMIT hapticsChanged();
        return;
    }
    if (m_applied == m && m_applied.resolutionSteps == m.resolutionSteps)
        return;
    QMetaObject::invokeMethod(m_worker, "applyMode", Qt::QueuedConnection, Q_ARG(quint16, m.resolutionSteps),
                              Q_ARG(bool, m.autoHaptics), Q_ARG(quint64, ++m_requestSeq));
}

void DialDevice::buzz(hid::Waveform w, int repeat)
{
    if (!m_hapticsOpen)
        return;
    QMetaObject::invokeMethod(m_worker, "buzz", Qt::QueuedConnection, Q_ARG(int, int(w)), Q_ARG(int, repeat));
}

} // namespace opendial
