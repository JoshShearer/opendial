// SPDX-License-Identifier: MIT
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace opendial {

enum Modifier : unsigned {
    ModNone = 0,
    ModCtrl = 1u << 0,
    ModShift = 1u << 1,
    ModAlt = 1u << 2,
    ModSuper = 1u << 3,
    ModAltGr = 1u << 4,
};

// One chord: modifiers plus a single key identified by its XKB keysym.
// Keysyms (not raw keycodes) are stored so a shortcut means the same
// character on QWERTY, AZERTY, etc.; backends resolve keysym -> keycode
// against the active keymap at injection time.
struct KeyChord {
    unsigned modifiers = ModNone;
    quint32 keysym = 0;   // 0 = modifier-only chord (e.g. "Ctrl")
    QString keyName;      // canonical XKB name, e.g. "z", "Page_Down"

    bool isValid() const { return keysym != 0 || modifiers != ModNone; }
    QString toString() const;  // canonical "Ctrl+Shift+Z"
    bool operator==(const KeyChord &o) const { return modifiers == o.modifiers && keysym == o.keysym; }
};

struct KeySequence {
    QList<KeyChord> chords;
    bool isValid() const { return !chords.isEmpty(); }
    QString toString() const;
};

struct KeyParseResult {
    KeySequence sequence;
    QString error;
    bool ok() const { return error.isEmpty() && sequence.isValid(); }
};

// Accepts "Ctrl+Shift+Z", "ctrl+z", "Ctrl++" (plus key), "Super+Page_Down",
// sequences separated by spaces or commas: "Ctrl+K Ctrl+C", aliases such as
// esc, enter, pgup, del, [, ], -, =, VolumeUp.
KeyParseResult parseKeySequence(const QString &text);
KeyParseResult parseKeyChord(const QString &text);

// keysym helpers (wrap xkbcommon)
quint32 keysymFromName(const QString &name); // case-insensitive fallback
QString keysymName(quint32 keysym);
quint32 keysymForModifier(Modifier m);
QList<Modifier> modifierList(unsigned mods);
// Keysyms needed to type text (one per UTF-32 code point).
QList<quint32> keysymsForText(const QString &text);

} // namespace opendial
