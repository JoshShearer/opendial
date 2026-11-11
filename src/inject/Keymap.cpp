// SPDX-License-Identifier: MIT
#include "Keymap.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <xkbcommon/xkbcommon.h>

Q_LOGGING_CATEGORY(lcKeymap, "opendial.keymap")

namespace opendial {

namespace {
QString readKeyValue(const QString &path, const QString &key)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QRegularExpression re(QStringLiteral("^\\s*%1\\s*=\\s*\"?([^\"\\n]*)\"?").arg(key), QRegularExpression::MultilineOption);
    auto m = re.match(QString::fromUtf8(f.readAll()));
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}
} // namespace

XkbLayoutSpec detectLayout(const QString &setting)
{
    XkbLayoutSpec s;
    if (!setting.isEmpty() && setting != QLatin1String("auto")) {
        // "de" or "de(nodeadkeys)" or "us,de"
        static const QRegularExpression re(QStringLiteral("^([^()]+)(?:\\(([^)]*)\\))?$"));
        auto m = re.match(setting);
        s.layout = m.hasMatch() ? m.captured(1) : setting;
        s.variant = m.hasMatch() ? m.captured(2) : QString();
        s.source = QStringLiteral("settings");
        return s;
    }
    if (!qEnvironmentVariableIsEmpty("XKB_DEFAULT_LAYOUT")) {
        s.layout = qEnvironmentVariable("XKB_DEFAULT_LAYOUT");
        s.variant = qEnvironmentVariable("XKB_DEFAULT_VARIANT");
        s.options = qEnvironmentVariable("XKB_DEFAULT_OPTIONS");
        s.source = QStringLiteral("XKB_DEFAULT_LAYOUT");
        return s;
    }
    if (!qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE") && !QStandardPaths::findExecutable(QStringLiteral("hyprctl")).isEmpty()) {
        auto get = [](const char *opt) {
            QProcess p;
            p.start(QStringLiteral("hyprctl"), {QStringLiteral("-j"), QStringLiteral("getoption"), QString::fromLatin1(opt)});
            if (!p.waitForFinished(1000))
                return QString();
            const QString v = QJsonDocument::fromJson(p.readAllStandardOutput()).object().value(QLatin1String("str")).toString().trimmed();
            // hyprctl reports unset string options as the literal "[[EMPTY]]"
            return v == QLatin1String("[[EMPTY]]") ? QString() : v;
        };
        const QString l = get("input:kb_layout");
        if (!l.isEmpty()) {
            s.layout = l;
            s.variant = get("input:kb_variant");
            s.options = get("input:kb_options");
            s.source = QStringLiteral("hyprland");
            return s;
        }
    }
    const QString deb = readKeyValue(QStringLiteral("/etc/default/keyboard"), QStringLiteral("XKBLAYOUT"));
    if (!deb.isEmpty()) {
        s.layout = deb;
        s.variant = readKeyValue(QStringLiteral("/etc/default/keyboard"), QStringLiteral("XKBVARIANT"));
        s.options = readKeyValue(QStringLiteral("/etc/default/keyboard"), QStringLiteral("XKBOPTIONS"));
        s.source = QStringLiteral("/etc/default/keyboard");
        return s;
    }
    QFile x(QStringLiteral("/etc/X11/xorg.conf.d/00-keyboard.conf"));
    if (x.open(QIODevice::ReadOnly)) {
        const QString t = QString::fromUtf8(x.readAll());
        auto m = QRegularExpression(QStringLiteral("Option\\s+\"XkbLayout\"\\s+\"([^\"]+)\"")).match(t);
        if (m.hasMatch()) {
            s.layout = m.captured(1);
            auto v = QRegularExpression(QStringLiteral("Option\\s+\"XkbVariant\"\\s+\"([^\"]+)\"")).match(t);
            if (v.hasMatch())
                s.variant = v.captured(1);
            s.source = QStringLiteral("00-keyboard.conf");
            return s;
        }
    }
    s.source = QStringLiteral("default");
    return s;
}

Keymap::Keymap() { m_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS); }

Keymap::~Keymap()
{
    if (m_keymap)
        xkb_keymap_unref(m_keymap);
    if (m_ctx)
        xkb_context_unref(m_ctx);
}

bool Keymap::load(const XkbLayoutSpec &spec, QString *error)
{
    if (!m_ctx) {
        if (error) *error = QStringLiteral("xkb context unavailable");
        return false;
    }
    // Only the first layout of a multi-layout list is used for injection.
    const QByteArray layout = spec.layout.section(QLatin1Char(','), 0, 0).toUtf8();
    const QByteArray variant = spec.variant.section(QLatin1Char(','), 0, 0).toUtf8();
    const QByteArray options = spec.options.toUtf8();
    const QByteArray model = spec.model.toUtf8();
    xkb_rule_names names{"evdev", model.constData(), layout.constData(), variant.constData(), options.constData()};
    xkb_keymap *km = xkb_keymap_new_from_names(m_ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!km && (!variant.isEmpty() || !options.isEmpty() || !model.isEmpty())) {
        // A bad variant/option string must not cost the whole layout.
        qCWarning(lcKeymap) << "keymap" << spec.layout << spec.variant << spec.options << "failed; retrying with the layout only";
        xkb_rule_names plain{"evdev", nullptr, layout.constData(), nullptr, nullptr};
        km = xkb_keymap_new_from_names(m_ctx, &plain, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
    if (!km) {
        if (error) *error = QStringLiteral("cannot compile XKB keymap for layout '%1'").arg(spec.layout);
        return false;
    }
    if (m_keymap)
        xkb_keymap_unref(m_keymap);
    m_keymap = km;
    m_spec = spec;
    index();
    return true;
}

QByteArray Keymap::asString() const
{
    if (!m_keymap)
        return {};
    char *s = xkb_keymap_get_as_string(m_keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    QByteArray b(s);
    free(s);
    return b;
}

void Keymap::index()
{
    m_map.clear();
    const xkb_mod_index_t shiftIdx = xkb_keymap_mod_get_index(m_keymap, XKB_MOD_NAME_SHIFT);
    const xkb_mod_index_t l3Idx = xkb_keymap_mod_get_index(m_keymap, "Mod5");
    const xkb_keycode_t minK = xkb_keymap_min_keycode(m_keymap);
    const xkb_keycode_t maxK = std::min<xkb_keycode_t>(xkb_keymap_max_keycode(m_keymap), 255 + 8);
    for (xkb_keycode_t kc = minK; kc <= maxK; ++kc) {
        const xkb_level_index_t levels = xkb_keymap_num_levels_for_key(m_keymap, kc, 0);
        for (xkb_level_index_t lvl = 0; lvl < levels && lvl < 4; ++lvl) {
            const xkb_keysym_t *syms = nullptr;
            const int n = xkb_keymap_key_get_syms_by_level(m_keymap, kc, 0, lvl, &syms);
            if (n != 1)
                continue;
            xkb_mod_mask_t masks[4] = {};
            const size_t nm = xkb_keymap_key_get_mods_for_level(m_keymap, kc, 0, lvl, masks, 4);
            if (nm == 0)
                continue;
            unsigned need = ModNone;
            const xkb_mod_mask_t m = masks[0];
            if (shiftIdx != XKB_MOD_INVALID && (m & (1u << shiftIdx)))
                need |= ModShift;
            if (l3Idx != XKB_MOD_INVALID && (m & (1u << l3Idx)))
                need |= ModAltGr;
            if (m & ~((shiftIdx != XKB_MOD_INVALID ? (1u << shiftIdx) : 0) | (l3Idx != XKB_MOD_INVALID ? (1u << l3Idx) : 0)))
                continue; // needs exotic modifiers (e.g. NumLock levels) - skip
            auto it = m_map.find(syms[0]);
            // Prefer the lowest level and the lowest keycode (main block over keypad duplicates).
            const bool better = it == m_map.end() || __builtin_popcount(need) < __builtin_popcount(it->levelMods);
            if (better)
                m_map.insert(syms[0], KeyPos{kc, need});
        }
    }
    // Lower-case letters fall back to upper-case positions (always present).
}

std::optional<Keymap::KeyPos> Keymap::lookup(quint32 keysym) const
{
    auto it = m_map.find(keysym);
    if (it != m_map.end())
        return *it;
    // Upper-case letter typed via Shift on its lower-case key.
    const xkb_keysym_t lower = xkb_keysym_to_lower(keysym);
    if (lower != keysym) {
        auto l = m_map.find(lower);
        if (l != m_map.end())
            return KeyPos{l->xkbKeycode, l->levelMods | ModShift};
    }
    return std::nullopt;
}

quint32 Keymap::modifierKeycode(Modifier m) const
{
    auto p = lookup(keysymForModifier(m));
    if (p)
        return p->xkbKeycode;
    // evdev fallbacks
    switch (m) {
    case ModCtrl: return 29 + 8;
    case ModShift: return 42 + 8;
    case ModAlt: return 56 + 8;
    case ModSuper: return 125 + 8;
    case ModAltGr: return 100 + 8;
    default: return 0;
    }
}

} // namespace opendial
