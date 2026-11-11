// SPDX-License-Identifier: MIT
#include "KeyChord.h"

#include <QHash>
#include <QRegularExpression>
#include <xkbcommon/xkbcommon.h>

namespace opendial {

namespace {
const QHash<QString, QString> &aliases()
{
    static const QHash<QString, QString> a = {
        {QStringLiteral("esc"), QStringLiteral("Escape")},
        {QStringLiteral("escape"), QStringLiteral("Escape")},
        {QStringLiteral("enter"), QStringLiteral("Return")},
        {QStringLiteral("return"), QStringLiteral("Return")},
        {QStringLiteral("space"), QStringLiteral("space")},
        {QStringLiteral("tab"), QStringLiteral("Tab")},
        {QStringLiteral("backspace"), QStringLiteral("BackSpace")},
        {QStringLiteral("del"), QStringLiteral("Delete")},
        {QStringLiteral("delete"), QStringLiteral("Delete")},
        {QStringLiteral("ins"), QStringLiteral("Insert")},
        {QStringLiteral("insert"), QStringLiteral("Insert")},
        {QStringLiteral("home"), QStringLiteral("Home")},
        {QStringLiteral("end"), QStringLiteral("End")},
        {QStringLiteral("pgup"), QStringLiteral("Prior")},
        {QStringLiteral("pageup"), QStringLiteral("Prior")},
        {QStringLiteral("page_up"), QStringLiteral("Prior")},
        {QStringLiteral("pgdn"), QStringLiteral("Next")},
        {QStringLiteral("pagedown"), QStringLiteral("Next")},
        {QStringLiteral("page_down"), QStringLiteral("Next")},
        {QStringLiteral("left"), QStringLiteral("Left")},
        {QStringLiteral("right"), QStringLiteral("Right")},
        {QStringLiteral("up"), QStringLiteral("Up")},
        {QStringLiteral("down"), QStringLiteral("Down")},
        {QStringLiteral("["), QStringLiteral("bracketleft")},
        {QStringLiteral("]"), QStringLiteral("bracketright")},
        {QStringLiteral("{"), QStringLiteral("braceleft")},
        {QStringLiteral("}"), QStringLiteral("braceright")},
        {QStringLiteral("-"), QStringLiteral("minus")},
        {QStringLiteral("_"), QStringLiteral("underscore")},
        {QStringLiteral("="), QStringLiteral("equal")},
        {QStringLiteral("+"), QStringLiteral("plus")},
        {QStringLiteral(","), QStringLiteral("comma")},
        {QStringLiteral("."), QStringLiteral("period")},
        {QStringLiteral("/"), QStringLiteral("slash")},
        {QStringLiteral("\\"), QStringLiteral("backslash")},
        {QStringLiteral(";"), QStringLiteral("semicolon")},
        {QStringLiteral("'"), QStringLiteral("apostrophe")},
        {QStringLiteral("`"), QStringLiteral("grave")},
        {QStringLiteral("volumeup"), QStringLiteral("XF86AudioRaiseVolume")},
        {QStringLiteral("volumedown"), QStringLiteral("XF86AudioLowerVolume")},
        {QStringLiteral("mute"), QStringLiteral("XF86AudioMute")},
        {QStringLiteral("playpause"), QStringLiteral("XF86AudioPlay")},
        {QStringLiteral("nexttrack"), QStringLiteral("XF86AudioNext")},
        {QStringLiteral("prevtrack"), QStringLiteral("XF86AudioPrev")},
        {QStringLiteral("brightnessup"), QStringLiteral("XF86MonBrightnessUp")},
        {QStringLiteral("brightnessdown"), QStringLiteral("XF86MonBrightnessDown")},
        {QStringLiteral("print"), QStringLiteral("Print")},
        {QStringLiteral("menu"), QStringLiteral("Menu")},
    };
    return a;
}

bool modifierFromToken(const QString &t, unsigned &mods)
{
    const QString l = t.toLower();
    if (l == QLatin1String("ctrl") || l == QLatin1String("control") || l == QLatin1String("ctl"))
        mods |= ModCtrl;
    else if (l == QLatin1String("shift"))
        mods |= ModShift;
    else if (l == QLatin1String("alt") || l == QLatin1String("meta") || l == QLatin1String("option"))
        mods |= ModAlt;
    else if (l == QLatin1String("super") || l == QLatin1String("win") || l == QLatin1String("logo")
             || l == QLatin1String("cmd") || l == QLatin1String("mod4"))
        mods |= ModSuper;
    else if (l == QLatin1String("altgr") || l == QLatin1String("iso_level3_shift"))
        mods |= ModAltGr;
    else
        return false;
    return true;
}

QString prettyKey(const QString &name)
{
    if (name.size() == 1)
        return name.toUpper();
    static const QHash<QString, QString> pretty = {
        {QStringLiteral("plus"), QStringLiteral("Plus")},
        {QStringLiteral("minus"), QStringLiteral("Minus")},
        {QStringLiteral("equal"), QStringLiteral("Equal")},
        {QStringLiteral("bracketleft"), QStringLiteral("[")},
        {QStringLiteral("bracketright"), QStringLiteral("]")},
        {QStringLiteral("comma"), QStringLiteral("Comma")},
        {QStringLiteral("period"), QStringLiteral("Period")},
        {QStringLiteral("space"), QStringLiteral("Space")},
        {QStringLiteral("Prior"), QStringLiteral("Page_Up")},
        {QStringLiteral("Next"), QStringLiteral("Page_Down")},
    };
    return pretty.value(name, name);
}
} // namespace

quint32 keysymFromName(const QString &name)
{
    const QByteArray n = name.toUtf8();
    xkb_keysym_t s = xkb_keysym_from_name(n.constData(), XKB_KEYSYM_NO_FLAGS);
    if (s == XKB_KEY_NoSymbol)
        s = xkb_keysym_from_name(n.constData(), XKB_KEYSYM_CASE_INSENSITIVE);
    return s;
}

QString keysymName(quint32 keysym)
{
    char buf[64];
    const int n = xkb_keysym_get_name(keysym, buf, sizeof buf);
    return n > 0 ? QString::fromUtf8(buf, qMin<int>(n, int(sizeof buf) - 1)) : QString();
}

quint32 keysymForModifier(Modifier m)
{
    switch (m) {
    case ModCtrl: return XKB_KEY_Control_L;
    case ModShift: return XKB_KEY_Shift_L;
    case ModAlt: return XKB_KEY_Alt_L;
    case ModSuper: return XKB_KEY_Super_L;
    case ModAltGr: return XKB_KEY_ISO_Level3_Shift;
    default: return 0;
    }
}

QList<Modifier> modifierList(unsigned mods)
{
    QList<Modifier> l;
    for (Modifier m : {ModCtrl, ModShift, ModAlt, ModSuper, ModAltGr})
        if (mods & m)
            l << m;
    return l;
}

QString KeyChord::toString() const
{
    QStringList parts;
    if (modifiers & ModCtrl) parts << QStringLiteral("Ctrl");
    if (modifiers & ModShift) parts << QStringLiteral("Shift");
    if (modifiers & ModAlt) parts << QStringLiteral("Alt");
    if (modifiers & ModSuper) parts << QStringLiteral("Super");
    if (modifiers & ModAltGr) parts << QStringLiteral("AltGr");
    if (keysym)
        parts << prettyKey(keyName);
    return parts.join(QLatin1Char('+'));
}

QString KeySequence::toString() const
{
    QStringList l;
    for (const auto &c : chords)
        l << c.toString();
    return l.join(QLatin1Char(' '));
}

KeyParseResult parseKeyChord(const QString &input)
{
    KeyParseResult r;
    QString text = input.trimmed();
    if (text.isEmpty()) {
        r.error = QStringLiteral("empty shortcut");
        return r;
    }
    QStringList tokens;
    // Handle a trailing "++" meaning the plus key, and a lone "+".
    if (text == QLatin1String("+")) {
        tokens << QStringLiteral("+");
    } else {
        bool trailingPlus = text.endsWith(QLatin1String("++"));
        if (trailingPlus)
            text.chop(2);
        tokens = text.split(QLatin1Char('+'), Qt::KeepEmptyParts);
        if (trailingPlus)
            tokens << QStringLiteral("+");
    }
    KeyChord c;
    for (int i = 0; i < tokens.size(); ++i) {
        const QString t = tokens.at(i).trimmed();
        if (t.isEmpty()) {
            r.error = QStringLiteral("malformed shortcut '%1'").arg(input);
            return r;
        }
        if (modifierFromToken(t, c.modifiers) && (i < tokens.size() - 1 || tokens.size() == 1)) {
            if (tokens.size() == 1) {
                // modifier-only chord, e.g. "Ctrl" (used with hold modifiers)
            }
            continue;
        }
        if (i != tokens.size() - 1) {
            r.error = QStringLiteral("'%1' is not a modifier (only the last key may be a non-modifier)").arg(t);
            return r;
        }
        QString name = aliases().value(t.toLower(), t);
        quint32 sym = keysymFromName(name);
        if (!sym && name.size() == 1)
            sym = xkb_utf32_to_keysym(name.at(0).unicode());
        if (!sym) {
            r.error = QStringLiteral("unknown key '%1'").arg(t);
            return r;
        }
        // Store letters lower-case: Shift is expressed as a modifier.
        if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z)
            sym = sym - XKB_KEY_A + XKB_KEY_a;
        c.keysym = sym;
        c.keyName = keysymName(sym);
    }
    if (!c.isValid()) {
        r.error = QStringLiteral("empty shortcut");
        return r;
    }
    r.sequence.chords << c;
    return r;
}

KeyParseResult parseKeySequence(const QString &text)
{
    KeyParseResult r;
    static const QRegularExpression sep(QStringLiteral("[\\s,]+"));
    QString t = text.trimmed();
    // A literal comma key would be split; accept "Ctrl+comma" spelling instead.
    const QStringList parts = t.split(sep, Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        r.error = QStringLiteral("empty shortcut");
        return r;
    }
    if (parts.size() > 16) {
        r.error = QStringLiteral("sequence too long (max 16 chords)");
        return r;
    }
    for (const auto &p : parts) {
        auto one = parseKeyChord(p);
        if (!one.ok()) {
            r.error = one.error;
            r.sequence = {};
            return r;
        }
        r.sequence.chords << one.sequence.chords;
    }
    return r;
}

QList<quint32> keysymsForText(const QString &text)
{
    QList<quint32> out;
    const auto ucs4 = text.toUcs4();
    for (char32_t cp : ucs4) {
        if (cp == U'\n') {
            out << XKB_KEY_Return;
            continue;
        }
        if (cp == U'\t') {
            out << XKB_KEY_Tab;
            continue;
        }
        const xkb_keysym_t s = xkb_utf32_to_keysym(cp);
        if (s != XKB_KEY_NoSymbol)
            out << s;
    }
    return out;
}

} // namespace opendial
