// SPDX-License-Identifier: MIT
#pragma once

#include "Injector.h"
#include "Keymap.h"

#include <memory>

struct zwp_virtual_keyboard_manager_v1;
struct zwp_virtual_keyboard_v1;
struct zwlr_virtual_pointer_manager_v1;
struct zwlr_virtual_pointer_v1;
struct xkb_state;

namespace opendial {

// wlroots-family compositors (Hyprland, Sway, river, labwc, Wayfire, ...):
// zwp_virtual_keyboard_v1 + zwlr_virtual_pointer_v1.
class WaylandInjector : public Injector {
    Q_OBJECT
public:
    explicit WaylandInjector(const XkbLayoutSpec &layout, QObject *parent = nullptr);
    ~WaylandInjector() override;
    QString id() const override { return QStringLiteral("wayland-virtual-input"); }
    QString label() const override { return QStringLiteral("Wayland virtual keyboard/pointer"); }
    CapState state() const override;
    bool canKeys() const override { return m_kbd != nullptr; }
    bool canPointer() const override { return m_ptr != nullptr; }

    bool modifier(Modifier m, bool down) override;
    bool key(quint32 keysym, bool down) override;
    bool scroll(Qt::Orientation o, double clicks, bool smooth) override;
    bool button(int code, bool down) override;
    void flush() override;

private:
    bool sendKeycode(quint32 xkbKeycode, bool down);
    quint32 now() const;

    Keymap m_keymap;
    zwp_virtual_keyboard_manager_v1 *m_kbdMgr = nullptr;
    zwp_virtual_keyboard_v1 *m_kbd = nullptr;
    zwlr_virtual_pointer_manager_v1 *m_ptrMgr = nullptr;
    zwlr_virtual_pointer_v1 *m_ptr = nullptr;
    xkb_state *m_state = nullptr;
    QHash<quint32, int> m_levelModRefs; // modifiers we pressed for levels
};

} // namespace opendial
