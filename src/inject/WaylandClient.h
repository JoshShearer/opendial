// SPDX-License-Identifier: MIT
#pragma once

#include <QHash>
#include <QObject>
#include <QSocketNotifier>
#include <QString>

struct wl_display;
struct wl_registry;
struct wl_seat;
struct wl_interface;

namespace opendial {

// A private wl_display connection (separate from Qt's) for protocols Qt does
// not expose: virtual keyboard/pointer and foreign toplevel management.
class WaylandClient : public QObject {
    Q_OBJECT
public:
    static WaylandClient *instance();

    bool isConnected() const { return m_display != nullptr; }
    wl_display *display() const { return m_display; }
    bool hasGlobal(const QString &iface) const { return m_globals.contains(iface); }
    quint32 globalVersion(const QString &iface) const { return m_globals.value(iface).version; }
    QStringList globals() const { return m_globals.keys(); }
    void *bind(const wl_interface *iface, quint32 maxVersion);
    wl_seat *seat();
    void flush();
    void roundtrip();

    // registry callbacks (public for C trampolines)
    void addGlobal(quint32 name, const char *iface, quint32 version);
    void removeGlobal(quint32 name);

Q_SIGNALS:
    void globalsChanged();
    void connectionLost();

private:
    WaylandClient();
    ~WaylandClient() override;
    void onReadable();

    struct Global {
        quint32 name = 0;
        quint32 version = 0;
    };
    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    wl_seat *m_seat = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    QHash<QString, Global> m_globals;
};

} // namespace opendial
