// SPDX-License-Identifier: MIT
#include "X11Injector.h"

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <cmath>

namespace opendial {

X11Injector::X11Injector(QObject *parent) : Injector(parent)
{
    const bool waylandSession = !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
                                || qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland");
    if (waylandSession) {
        m_detail = QStringLiteral("Wayland session: XTest would only reach XWayland clients");
        return;
    }
    if (qEnvironmentVariableIsEmpty("DISPLAY")) {
        m_detail = QStringLiteral("no DISPLAY");
        return;
    }
    m_dpy = XOpenDisplay(nullptr);
    int ev, er, maj, min;
    if (m_dpy && !XTestQueryExtension(m_dpy, &ev, &er, &maj, &min)) {
        XCloseDisplay(m_dpy);
        m_dpy = nullptr;
        m_detail = QStringLiteral("XTest extension missing");
        return;
    }
    m_detail = m_dpy ? QStringLiteral("XTest %1.%2").arg(maj).arg(min) : QStringLiteral("cannot open X display");
}

X11Injector::~X11Injector()
{
    if (m_dpy)
        XCloseDisplay(m_dpy);
}

bool X11Injector::modifier(Modifier m, bool down) { return key(keysymForModifier(m), down); }

bool X11Injector::key(quint32 keysym, bool down)
{
    if (!m_dpy)
        return false;
    const KeyCode kc = XKeysymToKeycode(m_dpy, keysym);
    if (!kc)
        return false;
    // Does the symbol need Shift on this keycode?
    const bool needShift = XkbKeycodeToKeysym(m_dpy, kc, 0, 0) != keysym && XkbKeycodeToKeysym(m_dpy, kc, 0, 1) == keysym;
    const KeyCode shift = XKeysymToKeycode(m_dpy, XK_Shift_L);
    if (down && needShift)
        XTestFakeKeyEvent(m_dpy, shift, True, CurrentTime);
    XTestFakeKeyEvent(m_dpy, kc, down ? True : False, CurrentTime);
    if (!down && needShift)
        XTestFakeKeyEvent(m_dpy, shift, False, CurrentTime);
    return true;
}

bool X11Injector::scroll(Qt::Orientation o, double clicks, bool)
{
    if (!m_dpy)
        return false;
    const int n = int(std::lround(clicks));
    const unsigned b = o == Qt::Vertical ? (n > 0 ? 5 : 4) : (n > 0 ? 7 : 6);
    for (int i = 0; i < std::abs(n) && i < 50; ++i) {
        XTestFakeButtonEvent(m_dpy, b, True, CurrentTime);
        XTestFakeButtonEvent(m_dpy, b, False, CurrentTime);
    }
    return n != 0;
}

bool X11Injector::button(int code, bool down)
{
    if (!m_dpy)
        return false;
    unsigned b = 1;
    switch (code) {
    case btn::Left: b = 1; break;
    case btn::Middle: b = 2; break;
    case btn::Right: b = 3; break;
    case btn::Side: b = 8; break;
    case btn::Extra: b = 9; break;
    default: return false;
    }
    XTestFakeButtonEvent(m_dpy, b, down ? True : False, CurrentTime);
    return true;
}

void X11Injector::flush()
{
    if (m_dpy)
        XFlush(m_dpy);
}

} // namespace opendial
