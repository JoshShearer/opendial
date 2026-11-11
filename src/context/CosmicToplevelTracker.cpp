// SPDX-License-Identifier: MIT
// COSMIC focus tracking. cosmic-comp does not offer wlr-foreign-toplevel; it
// lists windows with ext_foreign_toplevel_list_v1 (app id, title) and adds
// their state (activated, …) through zcosmic_toplevel_info_v1 (version 2+).
#include "FocusTrackers.h"
#include "WaylandClient.h"

#include "cosmic-toplevel-info-unstable-v1-client-protocol.h"
#include "ext-foreign-toplevel-list-v1-client-protocol.h"

#include <wayland-client.h>

// zcosmic_toplevel_handle_v1 has workspace events typed with interfaces from
// two workspace protocols. OpenDial never binds a workspace manager, so the
// compositor never sends them; these placeholders only satisfy the linker.
// (`extern` first: a namespace-scope const would otherwise be internal.)
extern "C" {
extern const struct wl_interface zcosmic_workspace_handle_v1_interface;
extern const struct wl_interface ext_workspace_handle_v1_interface;
}
const struct wl_interface zcosmic_workspace_handle_v1_interface = {"zcosmic_workspace_handle_v1", 1, 0, nullptr, 0, nullptr};
const struct wl_interface ext_workspace_handle_v1_interface = {"ext_workspace_handle_v1", 1, 0, nullptr, 0, nullptr};

namespace opendial {

namespace {
CosmicToplevelTracker *self(void *d) { return static_cast<CosmicToplevelTracker *>(d); }

// ext_foreign_toplevel_handle_v1
void extClosed(void *d, ext_foreign_toplevel_handle_v1 *h) { self(d)->toplevelClosed(h); }
void extDone(void *d, ext_foreign_toplevel_handle_v1 *h) { self(d)->toplevelDone(h); }
void extTitle(void *d, ext_foreign_toplevel_handle_v1 *h, const char *t) { self(d)->toplevels[h].title = QString::fromUtf8(t); }
void extAppId(void *d, ext_foreign_toplevel_handle_v1 *h, const char *a) { self(d)->toplevels[h].appId = QString::fromUtf8(a); }
void extIdentifier(void *, ext_foreign_toplevel_handle_v1 *, const char *) {}
const ext_foreign_toplevel_handle_v1_listener kExt = {extClosed, extDone, extTitle, extAppId, extIdentifier};

// zcosmic_toplevel_handle_v1: only `state` matters; the rest are no-ops
void cNoop(void *, zcosmic_toplevel_handle_v1 *) {}
void cStr(void *, zcosmic_toplevel_handle_v1 *, const char *) {}
void cOut(void *, zcosmic_toplevel_handle_v1 *, wl_output *) {}
void cCosWs(void *, zcosmic_toplevel_handle_v1 *, zcosmic_workspace_handle_v1 *) {}
void cExtWs(void *, zcosmic_toplevel_handle_v1 *, ext_workspace_handle_v1 *) {}
void cGeom(void *, zcosmic_toplevel_handle_v1 *, wl_output *, int32_t, int32_t, int32_t, int32_t) {}
void cState(void *d, zcosmic_toplevel_handle_v1 *c, wl_array *states)
{
    bool act = false;
    const auto *s = static_cast<const uint32_t *>(states->data);
    for (size_t i = 0; i < states->size / sizeof(uint32_t); ++i)
        if (s[i] == ZCOSMIC_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)
            act = true;
    self(d)->cosmicState(c, act);
}
const zcosmic_toplevel_handle_v1_listener kCosmic = {cNoop, cNoop, cStr, cStr, cOut, cOut, cCosWs, cCosWs, cState, cGeom, cExtWs, cExtWs};

// managers
void listToplevel(void *d, ext_foreign_toplevel_list_v1 *, ext_foreign_toplevel_handle_v1 *h) { self(d)->addToplevel(h); }
void listFinished(void *, ext_foreign_toplevel_list_v1 *) {}
const ext_foreign_toplevel_list_v1_listener kList = {listToplevel, listFinished};

void infoToplevel(void *, zcosmic_toplevel_info_v1 *, zcosmic_toplevel_handle_v1 *) {} // v1 only
void infoFinished(void *, zcosmic_toplevel_info_v1 *) {}
void infoDone(void *d, zcosmic_toplevel_info_v1 *) { self(d)->infoDone(); }
const zcosmic_toplevel_info_v1_listener kInfo = {infoToplevel, infoFinished, infoDone};
} // namespace

CosmicToplevelTracker::CosmicToplevelTracker(QObject *parent) : FocusTracker(parent) {}

bool CosmicToplevelTracker::detected()
{
    auto *wc = WaylandClient::instance();
    return wc->hasGlobal(QStringLiteral("zcosmic_toplevel_info_v1")) && wc->hasGlobal(QStringLiteral("ext_foreign_toplevel_list_v1"));
}

void CosmicToplevelTracker::start()
{
    auto *wc = WaylandClient::instance();
    // get_cosmic_toplevel needs version 2; version 3 only renames workspace events
    m_info = static_cast<zcosmic_toplevel_info_v1 *>(wc->bind(&zcosmic_toplevel_info_v1_interface, 2));
    m_list = static_cast<ext_foreign_toplevel_list_v1 *>(wc->bind(&ext_foreign_toplevel_list_v1_interface, 1));
    if (!m_info || !m_list) {
        m_detail = QStringLiteral("compositor does not offer zcosmic_toplevel_info_v1 (v2+) and ext_foreign_toplevel_list_v1");
        return;
    }
    zcosmic_toplevel_info_v1_add_listener(m_info, &kInfo, this);
    ext_foreign_toplevel_list_v1_add_listener(m_list, &kList, this);
    wc->roundtrip();
    wc->roundtrip(); // the cosmic handles requested in the first round report their state
    m_detail = QStringLiteral("tracking %1 toplevels").arg(toplevels.size());
    Q_EMIT stateChanged();
}

void CosmicToplevelTracker::addToplevel(ext_foreign_toplevel_handle_v1 *h)
{
    toplevels.insert(h, Toplevel{});
    ext_foreign_toplevel_handle_v1_add_listener(h, &kExt, this);
    auto *c = zcosmic_toplevel_info_v1_get_cosmic_toplevel(m_info, h);
    zcosmic_toplevel_handle_v1_add_listener(c, &kCosmic, this);
    toplevels[h].cosmic = c;
    m_byCosmic.insert(c, h);
}

void CosmicToplevelTracker::cosmicState(zcosmic_toplevel_handle_v1 *c, bool activated)
{
    if (auto *h = m_byCosmic.value(c))
        toplevels[h].pendingActivated = activated;
}

void CosmicToplevelTracker::infoDone()
{
    for (auto it = toplevels.begin(); it != toplevels.end(); ++it)
        toplevelDone(it.key());
}

void CosmicToplevelTracker::toplevelDone(ext_foreign_toplevel_handle_v1 *h)
{
    auto it = toplevels.find(h);
    if (it == toplevels.end())
        return;
    Toplevel &t = *it;
    const bool changed = t.pendingActivated != t.activated || t.appId != t.reportedAppId || t.title != t.reportedTitle;
    t.activated = t.pendingActivated;
    if (t.activated && (changed || m_lastActive != h)) {
        m_lastActive = h;
        t.reportedAppId = t.appId;
        t.reportedTitle = t.title;
        FocusContext ctx;
        ctx.source = id();
        ctx.appId = t.appId;
        ctx.title = t.title;
        Q_EMIT focusChanged(ctx);
    }
}

void CosmicToplevelTracker::toplevelClosed(ext_foreign_toplevel_handle_v1 *h)
{
    auto it = toplevels.find(h);
    if (it == toplevels.end())
        return;
    if (it->cosmic) {
        m_byCosmic.remove(it->cosmic);
        zcosmic_toplevel_handle_v1_destroy(it->cosmic);
    }
    toplevels.erase(it);
    ext_foreign_toplevel_handle_v1_destroy(h);
    if (m_lastActive == h) {
        m_lastActive = nullptr;
        FocusContext ctx;
        ctx.source = id();
        Q_EMIT focusChanged(ctx);
    }
}

} // namespace opendial
