// SPDX-License-Identifier: MIT
#pragma once

#include "Injector.h"
#include "Keymap.h"

namespace opendial {

// /dev/uinput virtual keyboard + wheel. Documented fallback only: requires an
// explicit opt-in setting AND an opt-in udev rule granting the seat user
// access to /dev/uinput (see docs/security-threat-model.md).
class UInputInjector : public Injector {
    Q_OBJECT
public:
    UInputInjector(bool optedIn, const XkbLayoutSpec &layout, QObject *parent = nullptr);
    ~UInputInjector() override;
    QString id() const override { return QStringLiteral("uinput"); }
    QString label() const override { return QStringLiteral("Kernel uinput (opt-in fallback)"); }
    CapState state() const override;

    bool modifier(Modifier m, bool down) override;
    bool key(quint32 keysym, bool down) override;
    bool scroll(Qt::Orientation o, double clicks, bool smooth) override;
    bool button(int code, bool down) override;

private:
    void emitEv(int type, int code, int value);
    void syn();
    int m_fd = -1;
    bool m_optedIn = false;
    bool m_accessible = false;
    Keymap m_keymap;
};

} // namespace opendial
