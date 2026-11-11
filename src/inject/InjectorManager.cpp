// SPDX-License-Identifier: MIT
#include "InjectorManager.h"
#include "Keymap.h"
#include "PortalInjector.h"
#include "UInputInjector.h"
#ifdef OPENDIAL_HAVE_WAYLAND
#include "WaylandInjector.h"
#endif
#ifdef OPENDIAL_HAVE_X11
#include "X11Injector.h"
#endif

#include <QLoggingCategory>
#include <xkbcommon/xkbcommon-keysyms.h>

Q_LOGGING_CATEGORY(lcInject, "opendial.inject")

namespace opendial {

QString capStateName(CapState s)
{
    switch (s) {
    case CapState::Supported: return QStringLiteral("supported");
    case CapState::PermissionNeeded: return QStringLiteral("permission-needed");
    case CapState::NotSupported: return QStringLiteral("not-supported");
    case CapState::FallbackAvailable: return QStringLiteral("fallback-available");
    }
    return {};
}

InjectorManager::InjectorManager(QObject *parent) : QObject(parent)
{
    m_holdTimer.setSingleShot(true);
    connect(&m_holdTimer, &QTimer::timeout, this, [this] {
        for (int m : std::as_const(m_heldMods))
            if (auto *a = active())
                a->modifier(Modifier(m), false);
        m_heldMods.clear();
        if (auto *a = active())
            a->flush();
    });
}

InjectorManager::~InjectorManager()
{
    releaseAll();
    qDeleteAll(m_backends);
}

void InjectorManager::configure(const InjectionSettings &s)
{
    const bool same = s.backend == m_settings.backend && s.allowUinput == m_settings.allowUinput
                      && s.keyboardLayout == m_settings.keyboardLayout && !m_backends.isEmpty();
    m_settings = s;
    if (!same)
        rebuild();
}

void InjectorManager::rebuild()
{
    releaseAll();
    qDeleteAll(m_backends);
    m_backends.clear();
    const XkbLayoutSpec layout = detectLayout(m_settings.keyboardLayout);
#ifdef OPENDIAL_HAVE_WAYLAND
    m_backends << new WaylandInjector(layout, this);
#endif
    m_portal = new PortalInjector(m_settings.portalRestoreToken, this);
    connect(m_portal, &PortalInjector::restoreTokenChanged, this, &InjectorManager::portalRestoreTokenChanged);
    connect(m_portal, &Injector::stateChanged, this, &InjectorManager::changed);
    m_backends << m_portal;
#ifdef OPENDIAL_HAVE_X11
    m_backends << new X11Injector(this);
#endif
    m_backends << new UInputInjector(m_settings.allowUinput, layout, this);
    // A stored portal grant is restored silently at startup.
    if (m_portal->available() && !m_settings.portalRestoreToken.isEmpty()) {
        bool better = false;
        for (auto *b : std::as_const(m_backends))
            if (b != m_portal && b->state() == CapState::Supported)
                better = true;
        if (!better && (m_settings.backend == QLatin1String("auto") || m_settings.backend == m_portal->id()))
            m_portal->requestPermission();
    }
    for (auto *b : std::as_const(m_backends))
        qCInfo(lcInject) << b->id() << capStateName(b->state()) << b->detail();
    Q_EMIT changed();
}

Injector *InjectorManager::active(bool forKeys) const
{
    auto usable = [&](Injector *b) { return forKeys ? b->canKeys() : b->canPointer(); };
    if (m_settings.backend != QLatin1String("auto")) {
        for (auto *b : m_backends)
            if (b->id() == m_settings.backend && usable(b))
                return b;
        return nullptr;
    }
    for (auto *b : m_backends) // ordered by preference
        if (usable(b) && (b->state() == CapState::Supported || b->state() == CapState::FallbackAvailable)
            && !(b->id() == QLatin1String("uinput") && !m_settings.allowUinput))
            return b;
    return nullptr;
}

QString InjectorManager::keyboardCapability() const
{
    if (active(true))
        return capStateName(CapState::Supported);
    for (auto *b : m_backends)
        if (b->state() == CapState::PermissionNeeded)
            return capStateName(CapState::PermissionNeeded);
    for (auto *b : m_backends)
        if (b->state() == CapState::FallbackAvailable)
            return capStateName(CapState::FallbackAvailable);
    return capStateName(CapState::NotSupported);
}

QString InjectorManager::pointerCapability() const
{
    if (active(false))
        return capStateName(CapState::Supported);
    for (auto *b : m_backends)
        if (b->state() == CapState::PermissionNeeded)
            return capStateName(CapState::PermissionNeeded);
    return capStateName(CapState::NotSupported);
}

QJsonObject InjectorManager::toJson() const
{
    QJsonArray arr;
    for (auto *b : m_backends)
        arr.append(b->toJson());
    auto *k = active(true);
    auto *p = active(false);
    return QJsonObject{{QStringLiteral("backends"), arr},
                       {QStringLiteral("activeKeyboard"), k ? k->id() : QString()},
                       {QStringLiteral("activePointer"), p ? p->id() : QString()},
                       {QStringLiteral("keyboard"), keyboardCapability()},
                       {QStringLiteral("pointer"), pointerCapability()},
                       {QStringLiteral("suppressed"), m_suppressed},
                       {QStringLiteral("preferred"), m_settings.backend}};
}

bool InjectorManager::press(quint32 sym, bool down)
{
    auto *a = active(true);
    return a && a->key(sym, down);
}

bool InjectorManager::tapSequence(const KeySequence &seq, int repeat)
{
    if (m_suppressed)
        return false;
    auto *a = active(true);
    if (!a)
        return false;
    repeat = qBound(1, repeat, 20);
    bool ok = true;
    for (int r = 0; r < repeat; ++r) {
        for (const auto &c : seq.chords) {
            const auto mods = modifierList(c.modifiers);
            for (auto m : mods)
                if (!m_heldMods.contains(int(m)))
                    ok &= a->modifier(m, true);
            if (c.keysym) {
                ok &= a->key(c.keysym, true);
                a->key(c.keysym, false);
            }
            for (auto it = mods.rbegin(); it != mods.rend(); ++it)
                if (!m_heldMods.contains(int(*it)))
                    a->modifier(*it, false);
        }
    }
    a->flush();
    return ok;
}

bool InjectorManager::tapHeld(const KeyChord &chord, int repeat, int releaseAfterMs)
{
    if (m_suppressed)
        return false;
    auto *a = active(true);
    if (!a)
        return false;
    const auto mods = modifierList(chord.modifiers);
    // Release held modifiers that are not part of this chord (e.g. Shift
    // when switching from Alt+Shift+Tab back to Alt+Tab), press new ones.
    for (int m : QSet<int>(m_heldMods))
        if (!(chord.modifiers & unsigned(m))) {
            a->modifier(Modifier(m), false);
            m_heldMods.remove(m);
        }
    for (auto m : mods)
        if (!m_heldMods.contains(int(m))) {
            a->modifier(m, true);
            m_heldMods.insert(int(m));
        }
    bool ok = true;
    for (int i = 0; i < qBound(1, repeat, 20); ++i)
        if (chord.keysym) {
            ok &= a->key(chord.keysym, true);
            a->key(chord.keysym, false);
        }
    a->flush();
    m_holdTimer.start(qBound(100, releaseAfterMs, 10000));
    return ok;
}

bool InjectorManager::typeText(const QString &text)
{
    if (m_suppressed)
        return false;
    auto *a = active(true);
    if (!a)
        return false;
    bool ok = true;
    for (quint32 s : keysymsForText(text.left(512))) {
        ok &= a->key(s, true);
        a->key(s, false);
    }
    a->flush();
    return ok;
}

bool InjectorManager::scroll(Qt::Orientation o, double clicks, bool smooth)
{
    if (m_suppressed)
        return false;
    auto *a = active(false);
    if (!a)
        return false;
    const bool ok = a->scroll(o, qBound(-50.0, clicks, 50.0), smooth);
    a->flush();
    return ok;
}

bool InjectorManager::ctrlScroll(double clicks)
{
    if (m_suppressed)
        return false;
    auto *k = active(true);
    auto *p = active(false);
    if (!k || !p)
        return false;
    k->modifier(ModCtrl, true);
    k->flush();
    const bool ok = p->scroll(Qt::Vertical, clicks, false);
    p->flush();
    k->modifier(ModCtrl, false);
    k->flush();
    return ok;
}

bool InjectorManager::click(int b)
{
    if (m_suppressed)
        return false;
    auto *a = active(false);
    if (!a)
        return false;
    a->button(b, true);
    a->button(b, false);
    a->flush();
    return true;
}

void InjectorManager::releaseAll()
{
    m_holdTimer.stop();
    if (auto *a = active(true)) {
        for (int m : std::as_const(m_heldMods))
            a->modifier(Modifier(m), false);
        a->flush();
    }
    m_heldMods.clear();
}

void InjectorManager::requestPortalPermission()
{
    if (m_portal)
        m_portal->requestPermission();
}

} // namespace opendial
