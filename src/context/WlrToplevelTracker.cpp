// SPDX-License-Identifier: MIT
#include "FocusTrackers.h"
#include "WaylandClient.h"

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#include <wayland-client.h>

namespace opendial {

namespace {
WlrToplevelTracker *self(void *d) { return static_cast<WlrToplevelTracker *>(d); }

void tlTitle(void *d, zwlr_foreign_toplevel_handle_v1 *h, const char *t) { self(d)->toplevels[h].title = QString::fromUtf8(t); }
void tlAppId(void *d, zwlr_foreign_toplevel_handle_v1 *h, const char *a) { self(d)->toplevels[h].appId = QString::fromUtf8(a); }
void tlOutEnter(void *, zwlr_foreign_toplevel_handle_v1 *, wl_output *) {}
void tlOutLeave(void *, zwlr_foreign_toplevel_handle_v1 *, wl_output *) {}
void tlState(void *d, zwlr_foreign_toplevel_handle_v1 *h, wl_array *states)
{
    bool act = false;
    const auto *s = static_cast<const uint32_t *>(states->data);
    for (size_t i = 0; i < states->size / sizeof(uint32_t); ++i)
        if (s[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)
            act = true;
    self(d)->toplevels[h].pendingActivated = act;
}
void tlDone(void *d, zwlr_foreign_toplevel_handle_v1 *h) { self(d)->toplevelDone(h); }
void tlClosed(void *d, zwlr_foreign_toplevel_handle_v1 *h)
{
    self(d)->toplevelClosed(h);
    zwlr_foreign_toplevel_handle_v1_destroy(h);
}
void tlParent(void *, zwlr_foreign_toplevel_handle_v1 *, zwlr_foreign_toplevel_handle_v1 *) {}

const zwlr_foreign_toplevel_handle_v1_listener kTl = {tlTitle, tlAppId, tlOutEnter, tlOutLeave, tlState, tlDone, tlClosed, tlParent};

void mgrToplevel(void *d, zwlr_foreign_toplevel_manager_v1 *, zwlr_foreign_toplevel_handle_v1 *h)
{
    self(d)->addToplevel(h);
    zwlr_foreign_toplevel_handle_v1_add_listener(h, &kTl, d);
}
void mgrFinished(void *, zwlr_foreign_toplevel_manager_v1 *) {}
const zwlr_foreign_toplevel_manager_v1_listener kMgr = {mgrToplevel, mgrFinished};
} // namespace

WlrToplevelTracker::WlrToplevelTracker(QObject *parent) : FocusTracker(parent) {}

bool WlrToplevelTracker::detected()
{
    return WaylandClient::instance()->hasGlobal(QStringLiteral("zwlr_foreign_toplevel_manager_v1"));
}

void WlrToplevelTracker::start()
{
    m_mgr = static_cast<zwlr_foreign_toplevel_manager_v1 *>(
        WaylandClient::instance()->bind(&zwlr_foreign_toplevel_manager_v1_interface, 3));
    if (!m_mgr) {
        m_detail = QStringLiteral("compositor does not offer zwlr_foreign_toplevel_manager_v1");
        return;
    }
    zwlr_foreign_toplevel_manager_v1_add_listener(m_mgr, &kMgr, this);
    WaylandClient::instance()->roundtrip();
    m_detail = QStringLiteral("tracking %1 toplevels").arg(toplevels.size());
    Q_EMIT stateChanged();
}

void WlrToplevelTracker::addToplevel(void *h) { toplevels.insert(h, Toplevel{}); }

void WlrToplevelTracker::toplevelDone(void *h)
{
    auto &t = toplevels[h];
    const bool was = t.activated;
    t.activated = t.pendingActivated;
    if (t.activated && (!was || m_lastActive == h)) {
        m_lastActive = h;
        FocusContext c;
        c.source = id();
        c.appId = t.appId;
        c.title = t.title;
        Q_EMIT focusChanged(c);
    }
}

void WlrToplevelTracker::toplevelClosed(void *h)
{
    toplevels.remove(h);
    if (m_lastActive == h) {
        m_lastActive = nullptr;
        FocusContext c;
        c.source = id();
        Q_EMIT focusChanged(c);
    }
}

} // namespace opendial
