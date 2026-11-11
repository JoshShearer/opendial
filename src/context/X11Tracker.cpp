// SPDX-License-Identifier: MIT
#include "FocusTrackers.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace opendial {

namespace {
int ignoreErrors(Display *, XErrorEvent *) { return 0; }

QByteArray prop(Display *d, Window w, const char *name, Atom type, long max = 4096)
{
    Atom a = XInternAtom(d, name, False);
    Atom actual;
    int fmt;
    unsigned long n, after;
    unsigned char *data = nullptr;
    if (XGetWindowProperty(d, w, a, 0, max, False, type, &actual, &fmt, &n, &after, &data) != Success || !data)
        return {};
    QByteArray out(reinterpret_cast<char *>(data), int(n * (fmt == 32 ? sizeof(long) : fmt / 8)));
    XFree(data);
    return out;
}
} // namespace

X11Tracker::X11Tracker(QObject *parent) : FocusTracker(parent) {}

X11Tracker::~X11Tracker()
{
    if (m_dpy)
        XCloseDisplay(m_dpy);
}

bool X11Tracker::detected()
{
    return !qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
           && qEnvironmentVariable("XDG_SESSION_TYPE") != QLatin1String("wayland");
}

void X11Tracker::start()
{
    m_dpy = XOpenDisplay(nullptr);
    if (!m_dpy) {
        m_detail = QStringLiteral("cannot open X display");
        return;
    }
    XSetErrorHandler(ignoreErrors);
    XSelectInput(m_dpy, DefaultRootWindow(m_dpy), PropertyChangeMask);
    XFlush(m_dpy);
    m_notifier = new QSocketNotifier(ConnectionNumber(m_dpy), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &X11Tracker::onReadable);
    m_detail = QStringLiteral("watching _NET_ACTIVE_WINDOW");
    refresh();
}

void X11Tracker::onReadable()
{
    bool changed = false;
    const Atom active = XInternAtom(m_dpy, "_NET_ACTIVE_WINDOW", False);
    const Atom desk = XInternAtom(m_dpy, "_NET_CURRENT_DESKTOP", False);
    const Atom name = XInternAtom(m_dpy, "_NET_WM_NAME", False);
    while (XPending(m_dpy)) {
        XEvent ev;
        XNextEvent(m_dpy, &ev);
        if (ev.type == PropertyNotify && (ev.xproperty.atom == active || ev.xproperty.atom == desk || ev.xproperty.atom == name))
            changed = true;
    }
    if (changed)
        refresh();
}

void X11Tracker::refresh()
{
    const QByteArray a = prop(m_dpy, DefaultRootWindow(m_dpy), "_NET_ACTIVE_WINDOW", XA_WINDOW, 1);
    FocusContext c;
    c.source = id();
    if (a.size() >= int(sizeof(long))) {
        const Window w = Window(*reinterpret_cast<const long *>(a.constData()));
        // watch the title of the active window too
        XSelectInput(m_dpy, w, PropertyChangeMask);
        XClassHint hint{};
        if (w && XGetClassHint(m_dpy, w, &hint)) {
            c.appId = QString::fromUtf8(hint.res_class ? hint.res_class : "");
            c.windowClass = QString::fromUtf8(hint.res_name ? hint.res_name : "");
            if (hint.res_name) XFree(hint.res_name);
            if (hint.res_class) XFree(hint.res_class);
        }
        c.title = QString::fromUtf8(prop(m_dpy, w, "_NET_WM_NAME", XInternAtom(m_dpy, "UTF8_STRING", False)));
        const QByteArray pid = prop(m_dpy, w, "_NET_WM_PID", XA_CARDINAL, 1);
        if (pid.size() >= int(sizeof(long)))
            c.pid = *reinterpret_cast<const long *>(pid.constData());
    }
    const QByteArray d = prop(m_dpy, DefaultRootWindow(m_dpy), "_NET_CURRENT_DESKTOP", XA_CARDINAL, 1);
    if (d.size() >= int(sizeof(long)))
        c.workspace = QString::number(*reinterpret_cast<const long *>(d.constData()) + 1);
    Q_EMIT focusChanged(c);
}

std::optional<QPoint> X11Tracker::pointerPosition()
{
    if (!m_dpy)
        return std::nullopt;
    Window root, child;
    int rx, ry, wx, wy;
    unsigned mask;
    if (XQueryPointer(m_dpy, DefaultRootWindow(m_dpy), &root, &child, &rx, &ry, &wx, &wy, &mask))
        return QPoint(rx, ry);
    return std::nullopt;
}

} // namespace opendial
