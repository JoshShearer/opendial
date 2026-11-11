// SPDX-License-Identifier: MIT
#include "WaylandClient.h"

#include <QLoggingCategory>
#include <wayland-client.h>

Q_LOGGING_CATEGORY(lcWayland, "opendial.wayland")

namespace opendial {

namespace {
void onGlobal(void *data, wl_registry *, uint32_t name, const char *iface, uint32_t version)
{
    static_cast<WaylandClient *>(data)->addGlobal(name, iface, version);
}
void onGlobalRemove(void *data, wl_registry *, uint32_t name)
{
    static_cast<WaylandClient *>(data)->removeGlobal(name);
}
const wl_registry_listener kRegistryListener = {onGlobal, onGlobalRemove};
} // namespace

WaylandClient *WaylandClient::instance()
{
    static WaylandClient *c = new WaylandClient;
    return c;
}

WaylandClient::WaylandClient()
{
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_SOCKET"))
        return;
    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        qCInfo(lcWayland) << "no Wayland display";
        return;
    }
    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &kRegistryListener, this);
    wl_display_roundtrip(m_display);
    m_notifier = new QSocketNotifier(wl_display_get_fd(m_display), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &WaylandClient::onReadable);
    qCInfo(lcWayland) << "connected; globals:" << m_globals.size();
}

WaylandClient::~WaylandClient()
{
    if (m_display)
        wl_display_disconnect(m_display);
}

void WaylandClient::addGlobal(quint32 name, const char *iface, quint32 version)
{
    m_globals.insert(QString::fromLatin1(iface), Global{name, version});
    Q_EMIT globalsChanged();
}

void WaylandClient::removeGlobal(quint32 name)
{
    for (auto it = m_globals.begin(); it != m_globals.end(); ++it)
        if (it->name == name) {
            m_globals.erase(it);
            break;
        }
    Q_EMIT globalsChanged();
}

void *WaylandClient::bind(const wl_interface *iface, quint32 maxVersion)
{
    if (!m_display)
        return nullptr;
    const auto it = m_globals.constFind(QString::fromLatin1(iface->name));
    if (it == m_globals.constEnd())
        return nullptr;
    return wl_registry_bind(m_registry, it->name, iface, std::min(it->version, maxVersion));
}

wl_seat *WaylandClient::seat()
{
    if (!m_seat)
        m_seat = static_cast<wl_seat *>(bind(&wl_seat_interface, 1));
    return m_seat;
}

void WaylandClient::flush()
{
    if (m_display)
        wl_display_flush(m_display);
}

void WaylandClient::roundtrip()
{
    if (m_display)
        wl_display_roundtrip(m_display);
}

void WaylandClient::onReadable()
{
    if (wl_display_dispatch(m_display) < 0) {
        qCWarning(lcWayland) << "Wayland connection lost";
        m_notifier->setEnabled(false);
        Q_EMIT connectionLost();
    }
}

} // namespace opendial
