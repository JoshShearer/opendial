// SPDX-License-Identifier: MIT
#pragma once

#include "Injector.h"

struct _XDisplay;

namespace opendial {

// X11 sessions (and XWayland-only fallbacks are NOT used: XTest into
// XWayland reaches X clients only). Uses the server keymap directly.
class X11Injector : public Injector {
    Q_OBJECT
public:
    explicit X11Injector(QObject *parent = nullptr);
    ~X11Injector() override;
    QString id() const override { return QStringLiteral("x11-xtest"); }
    QString label() const override { return QStringLiteral("X11 XTest"); }
    CapState state() const override { return m_dpy ? CapState::Supported : CapState::NotSupported; }

    bool modifier(Modifier m, bool down) override;
    bool key(quint32 keysym, bool down) override;
    bool scroll(Qt::Orientation o, double clicks, bool smooth) override;
    bool button(int code, bool down) override;
    void flush() override;

private:
    _XDisplay *m_dpy = nullptr;
};

} // namespace opendial
