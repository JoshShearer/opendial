// SPDX-License-Identifier: MIT
#pragma once

#include "KeyChord.h"

#include <QJsonObject>
#include <QObject>

namespace opendial {

// Capability states reported per backend / action (SRS section 7).
enum class CapState { Supported, PermissionNeeded, NotSupported, FallbackAvailable };
QString capStateName(CapState s);

// Linux button codes used by mouse actions.
namespace btn {
constexpr int Left = 0x110, Right = 0x111, Middle = 0x112, Side = 0x113, Extra = 0x114;
}

class Injector : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual QString id() const = 0;
    virtual QString label() const = 0;
    virtual CapState state() const = 0;
    QString detail() const { return m_detail; }
    virtual bool canKeys() const { return state() == CapState::Supported; }
    virtual bool canPointer() const { return state() == CapState::Supported; }

    // Press/release one modifier.
    virtual bool modifier(Modifier m, bool down) = 0;
    // Press/release the key producing keysym; the backend adds any level
    // modifiers (Shift/AltGr) the active layout needs for that symbol.
    virtual bool key(quint32 keysym, bool down) = 0;
    // Scroll: clicks > 0 = down / right. smooth => fractional pixel-style.
    virtual bool scroll(Qt::Orientation o, double clicks, bool smooth) = 0;
    virtual bool button(int code, bool down) = 0;
    virtual void flush() {}
    // Ask for consent where the platform requires it (portal dialog).
    virtual void requestPermission() {}

    QJsonObject toJson() const
    {
        return QJsonObject{{QStringLiteral("id"), id()},
                           {QStringLiteral("label"), label()},
                           {QStringLiteral("state"), capStateName(state())},
                           {QStringLiteral("detail"), m_detail}};
    }

Q_SIGNALS:
    void stateChanged();

protected:
    QString m_detail;
};

} // namespace opendial
