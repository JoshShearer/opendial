// SPDX-License-Identifier: MIT
#pragma once

#include "Injector.h"
#include "KeyChord.h"

#include <QJsonArray>
#include <QPointer>
#include <QSet>
#include <QTimer>

namespace opendial {

class PortalInjector;

struct InjectionSettings {
    QString backend = QStringLiteral("auto"); // auto | wayland-virtual-input | xdg-portal-remotedesktop | x11-xtest | uinput
    bool allowUinput = false;
    QString keyboardLayout = QStringLiteral("auto");
    QString portalRestoreToken;
};

class InjectorManager : public QObject {
    Q_OBJECT
public:
    explicit InjectorManager(QObject *parent = nullptr);
    ~InjectorManager() override;

    void configure(const InjectionSettings &s);
    Injector *active(bool forKeys = true) const;
    QJsonObject toJson() const;
    // Per action category: supported | permission-needed | not-supported | fallback-available
    QString keyboardCapability() const;
    QString pointerCapability() const;

    bool tapSequence(const KeySequence &seq, int repeat = 1);
    // Keep the chord's modifiers held (Alt for Alt+Tab) until releaseAfterMs
    // of inactivity, then release them.
    bool tapHeld(const KeyChord &chord, int repeat, int releaseAfterMs);
    bool typeText(const QString &text);
    bool scroll(Qt::Orientation o, double clicks, bool smooth);
    bool ctrlScroll(double clicks);
    bool click(int button);
    // Release every key/modifier we are holding (shutdown, lock, disconnect).
    void releaseAll();
    void requestPortalPermission();

    void setSuppressed(bool s) { m_suppressed = s; if (s) releaseAll(); }
    bool suppressed() const { return m_suppressed; }

Q_SIGNALS:
    void changed();
    void portalRestoreTokenChanged(const QString &token);

private:
    void rebuild();
    bool press(quint32 sym, bool down);

    InjectionSettings m_settings;
    QList<Injector *> m_backends;
    QPointer<PortalInjector> m_portal;
    QSet<int> m_heldMods;     // Modifier values currently held by tapHeld
    QTimer m_holdTimer;
    bool m_suppressed = false;
};

} // namespace opendial
