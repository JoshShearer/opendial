// SPDX-License-Identifier: MIT
#include "UInputInjector.h"

#include <QThread>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace opendial {

UInputInjector::UInputInjector(bool optedIn, const XkbLayoutSpec &layout, QObject *parent)
    : Injector(parent), m_optedIn(optedIn)
{
    m_accessible = ::access("/dev/uinput", W_OK) == 0;
    if (!optedIn) {
        m_detail = m_accessible ? QStringLiteral("available but not enabled (Settings > Integrations > allow uinput)")
                                : QStringLiteral("disabled; needs opt-in setting and the opt-in uinput udev rule");
        return;
    }
    if (!m_accessible) {
        m_detail = QStringLiteral("opted in, but /dev/uinput is not writable: install 71-opendial-uinput-optin.rules");
        return;
    }
    QString err;
    m_keymap.load(layout, &err);
    m_fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0) {
        m_detail = QStringLiteral("open /dev/uinput: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        return;
    }
    ioctl(m_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(m_fd, UI_SET_EVBIT, EV_REL);
    ioctl(m_fd, UI_SET_EVBIT, EV_SYN);
    for (int k = 1; k < 248; ++k)
        ioctl(m_fd, UI_SET_KEYBIT, k);
    for (int b = btn::Left; b <= btn::Extra; ++b)
        ioctl(m_fd, UI_SET_KEYBIT, b);
    ioctl(m_fd, UI_SET_RELBIT, REL_X);
    ioctl(m_fd, UI_SET_RELBIT, REL_Y);
    ioctl(m_fd, UI_SET_RELBIT, REL_WHEEL);
    ioctl(m_fd, UI_SET_RELBIT, REL_HWHEEL);
#ifdef REL_WHEEL_HI_RES
    ioctl(m_fd, UI_SET_RELBIT, REL_WHEEL_HI_RES);
    ioctl(m_fd, UI_SET_RELBIT, REL_HWHEEL_HI_RES);
#endif
    uinput_setup us{};
    us.id.bustype = BUS_VIRTUAL;
    us.id.vendor = 0x4f44; // "OD"
    us.id.product = 0x0001;
    strncpy(us.name, "OpenDial virtual input", UINPUT_MAX_NAME_SIZE - 1);
    if (ioctl(m_fd, UI_DEV_SETUP, &us) < 0 || ioctl(m_fd, UI_DEV_CREATE) < 0) {
        m_detail = QStringLiteral("uinput device creation failed: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        ::close(m_fd);
        m_fd = -1;
        return;
    }
    m_detail = QStringLiteral("virtual device created; layout %1").arg(m_keymap.spec().layout);
}

UInputInjector::~UInputInjector()
{
    if (m_fd >= 0) {
        ioctl(m_fd, UI_DEV_DESTROY);
        ::close(m_fd);
    }
}

CapState UInputInjector::state() const
{
    if (m_fd >= 0)
        return CapState::Supported;
    if (m_accessible && !m_optedIn)
        return CapState::FallbackAvailable;
    return m_optedIn ? CapState::PermissionNeeded : CapState::FallbackAvailable;
}

void UInputInjector::emitEv(int type, int code, int value)
{
    input_event ev{};
    ev.type = quint16(type);
    ev.code = quint16(code);
    ev.value = value;
    if (::write(m_fd, &ev, sizeof ev) < 0 && errno == EAGAIN) {
        QThread::usleep(500);
        (void)!::write(m_fd, &ev, sizeof ev);
    }
}

void UInputInjector::syn() { emitEv(EV_SYN, SYN_REPORT, 0); }

bool UInputInjector::modifier(Modifier m, bool down)
{
    if (m_fd < 0)
        return false;
    emitEv(EV_KEY, int(m_keymap.modifierKeycode(m)) - 8, down ? 1 : 0);
    syn();
    return true;
}

bool UInputInjector::key(quint32 keysym, bool down)
{
    if (m_fd < 0)
        return false;
    auto pos = m_keymap.lookup(keysym);
    if (!pos)
        return false;
    const auto mods = modifierList(pos->levelMods);
    if (down)
        for (auto m : mods)
            modifier(m, true);
    emitEv(EV_KEY, int(pos->xkbKeycode) - 8, down ? 1 : 0);
    syn();
    if (!down)
        for (auto it = mods.rbegin(); it != mods.rend(); ++it)
            modifier(*it, false);
    return true;
}

bool UInputInjector::scroll(Qt::Orientation o, double clicks, bool smooth)
{
    if (m_fd < 0)
        return false;
#ifdef REL_WHEEL_HI_RES
    const int hires = int(std::lround(clicks * 120.0));
    // Kernel/libinput convention: positive REL_WHEEL = up; our clicks > 0 = down.
    if (o == Qt::Vertical) {
        emitEv(EV_REL, REL_WHEEL_HI_RES, -hires);
        if (!smooth)
            emitEv(EV_REL, REL_WHEEL, -int(std::lround(clicks)));
    } else {
        emitEv(EV_REL, REL_HWHEEL_HI_RES, hires);
        if (!smooth)
            emitEv(EV_REL, REL_HWHEEL, int(std::lround(clicks)));
    }
#else
    emitEv(EV_REL, o == Qt::Vertical ? REL_WHEEL : REL_HWHEEL, o == Qt::Vertical ? -int(std::lround(clicks)) : int(std::lround(clicks)));
#endif
    syn();
    return true;
}

bool UInputInjector::button(int code, bool down)
{
    if (m_fd < 0)
        return false;
    emitEv(EV_KEY, code, down ? 1 : 0);
    syn();
    return true;
}

} // namespace opendial
