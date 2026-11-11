// SPDX-License-Identifier: MIT
#pragma once

#include "KeyChord.h"

#include <QHash>
#include <QString>
#include <optional>

struct xkb_context;
struct xkb_keymap;

namespace opendial {

struct XkbLayoutSpec {
    QString layout = QStringLiteral("us");
    QString variant;
    QString options;
    QString model = QStringLiteral("pc105");
    QString source; // where it was detected
};

// Detect the session keyboard layout (setting > XKB_DEFAULT_* > compositor >
// /etc/default/keyboard > X11 conf > "us").
XkbLayoutSpec detectLayout(const QString &setting);

class Keymap {
public:
    Keymap();
    ~Keymap();
    Keymap(const Keymap &) = delete;
    Keymap &operator=(const Keymap &) = delete;

    bool load(const XkbLayoutSpec &spec, QString *error = nullptr);
    bool isLoaded() const { return m_keymap != nullptr; }
    xkb_keymap *raw() const { return m_keymap; }
    QByteArray asString() const;
    XkbLayoutSpec spec() const { return m_spec; }

    struct KeyPos {
        quint32 xkbKeycode = 0;  // evdev code + 8
        unsigned levelMods = ModNone; // ModShift and/or ModAltGr needed for this level
    };
    std::optional<KeyPos> lookup(quint32 keysym) const;
    quint32 modifierKeycode(Modifier m) const;

private:
    void index();
    xkb_context *m_ctx = nullptr;
    xkb_keymap *m_keymap = nullptr;
    XkbLayoutSpec m_spec;
    QHash<quint32, KeyPos> m_map;
};

} // namespace opendial
