// SPDX-License-Identifier: MIT
#include "WaylandInjector.h"
#include "WaylandClient.h"

#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <cmath>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

Q_LOGGING_CATEGORY(lcWlInject, "opendial.inject.wayland")

namespace opendial {

WaylandInjector::WaylandInjector(const XkbLayoutSpec &layout, QObject *parent) : Injector(parent)
{
    auto *wc = WaylandClient::instance();
    if (!wc->isConnected()) {
        m_detail = QStringLiteral("not a Wayland session");
        return;
    }
    wl_seat *seat = wc->seat();
    m_kbdMgr = static_cast<zwp_virtual_keyboard_manager_v1 *>(wc->bind(&zwp_virtual_keyboard_manager_v1_interface, 1));
    m_ptrMgr = static_cast<zwlr_virtual_pointer_manager_v1 *>(wc->bind(&zwlr_virtual_pointer_manager_v1_interface, 2));
    if (!seat) {
        m_detail = QStringLiteral("no wl_seat");
        return;
    }
    QString err;
    if (m_kbdMgr && m_keymap.load(layout, &err)) {
        m_kbd = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(m_kbdMgr, seat);
        const QByteArray km = m_keymap.asString();
        const int fd = memfd_create("opendial-keymap", MFD_CLOEXEC);
        if (fd >= 0 && ::write(fd, km.constData(), size_t(km.size()) + 1) == ssize_t(km.size()) + 1) {
            zwp_virtual_keyboard_v1_keymap(m_kbd, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, uint32_t(km.size()) + 1);
        } else {
            qCWarning(lcWlInject) << "failed to upload keymap";
        }
        if (fd >= 0)
            ::close(fd);
        m_state = xkb_state_new(m_keymap.raw());
    }
    if (m_ptrMgr)
        m_ptr = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(m_ptrMgr, seat);
    wc->roundtrip();
    QStringList parts;
    if (!m_kbdMgr)
        parts << QStringLiteral("compositor lacks zwp_virtual_keyboard_manager_v1");
    else if (!err.isEmpty())
        parts << err;
    if (!m_ptrMgr)
        parts << QStringLiteral("compositor lacks zwlr_virtual_pointer_manager_v1");
    m_detail = parts.isEmpty() ? QStringLiteral("keyboard layout %1 (%2)").arg(m_keymap.spec().layout, m_keymap.spec().source)
                               : parts.join(QStringLiteral("; "));
}

WaylandInjector::~WaylandInjector()
{
    if (m_kbd)
        zwp_virtual_keyboard_v1_destroy(m_kbd);
    if (m_ptr)
        zwlr_virtual_pointer_v1_destroy(m_ptr);
    if (m_state)
        xkb_state_unref(m_state);
    WaylandClient::instance()->flush();
}

CapState WaylandInjector::state() const
{
    if (m_kbd && m_ptr)
        return CapState::Supported;
    if (m_kbd || m_ptr)
        return CapState::FallbackAvailable; // partial
    return CapState::NotSupported;
}

quint32 WaylandInjector::now() const
{
    static QElapsedTimer t = [] { QElapsedTimer e; e.start(); return e; }();
    return quint32(t.elapsed());
}

bool WaylandInjector::sendKeycode(quint32 kc, bool down)
{
    if (!m_kbd || kc < 8)
        return false;
    zwp_virtual_keyboard_v1_key(m_kbd, now(), kc - 8, down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    if (m_state) {
        xkb_state_update_key(m_state, kc, down ? XKB_KEY_DOWN : XKB_KEY_UP);
        zwp_virtual_keyboard_v1_modifiers(m_kbd, xkb_state_serialize_mods(m_state, XKB_STATE_MODS_DEPRESSED),
                                          xkb_state_serialize_mods(m_state, XKB_STATE_MODS_LATCHED),
                                          xkb_state_serialize_mods(m_state, XKB_STATE_MODS_LOCKED),
                                          xkb_state_serialize_layout(m_state, XKB_STATE_LAYOUT_EFFECTIVE));
    }
    return true;
}

bool WaylandInjector::modifier(Modifier m, bool down)
{
    return sendKeycode(m_keymap.modifierKeycode(m), down);
}

bool WaylandInjector::key(quint32 keysym, bool down)
{
    if (!m_kbd)
        return false;
    auto pos = m_keymap.lookup(keysym);
    if (!pos) {
        qCInfo(lcWlInject) << "keysym" << Qt::hex << keysym << "not in layout" << m_keymap.spec().layout;
        return false;
    }
    const auto mods = modifierList(pos->levelMods);
    if (down)
        for (auto mm : mods)
            modifier(mm, true);
    sendKeycode(pos->xkbKeycode, down);
    if (!down)
        for (auto it = mods.rbegin(); it != mods.rend(); ++it)
            modifier(*it, false);
    return true;
}

bool WaylandInjector::scroll(Qt::Orientation o, double clicks, bool smooth)
{
    if (!m_ptr || clicks == 0.0)
        return false;
    const uint32_t axis = o == Qt::Vertical ? WL_POINTER_AXIS_VERTICAL_SCROLL : WL_POINTER_AXIS_HORIZONTAL_SCROLL;
    const quint32 t = now();
    if (smooth) {
        zwlr_virtual_pointer_v1_axis_source(m_ptr, WL_POINTER_AXIS_SOURCE_FINGER);
        zwlr_virtual_pointer_v1_axis(m_ptr, t, axis, wl_fixed_from_double(clicks * 15.0));
    } else {
        const int n = int(std::lround(clicks));
        if (n == 0)
            return false;
        zwlr_virtual_pointer_v1_axis_source(m_ptr, WL_POINTER_AXIS_SOURCE_WHEEL);
        zwlr_virtual_pointer_v1_axis_discrete(m_ptr, t, axis, wl_fixed_from_double(n * 15.0), n);
    }
    zwlr_virtual_pointer_v1_frame(m_ptr);
    return true;
}

bool WaylandInjector::button(int code, bool down)
{
    if (!m_ptr)
        return false;
    zwlr_virtual_pointer_v1_button(m_ptr, now(), uint32_t(code), down ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(m_ptr);
    return true;
}

void WaylandInjector::flush() { WaylandClient::instance()->flush(); }

} // namespace opendial
