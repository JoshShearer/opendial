// SPDX-License-Identifier: MIT
#pragma once

#include "Injector.h"
#include "Profiles.h"

#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QSocketNotifier>
#include <functional>
#include <optional>

struct zwlr_foreign_toplevel_manager_v1;
struct _XDisplay;

namespace opendial {

class FocusTracker : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual QString id() const = 0;
    virtual QString label() const = 0;
    virtual CapState state() const = 0;
    virtual void start() = 0;
    QString detail() const { return m_detail; }
    // Global pointer position in compositor logical coordinates, if known.
    virtual std::optional<QPoint> pointerPosition() { return std::nullopt; }
    QJsonObject toJson() const
    {
        return QJsonObject{{QStringLiteral("id"), id()}, {QStringLiteral("label"), label()},
                           {QStringLiteral("state"), capStateName(state())}, {QStringLiteral("detail"), m_detail}};
    }
Q_SIGNALS:
    void focusChanged(const opendial::FocusContext &ctx);
    void stateChanged();
protected:
    QString m_detail;
};

// ---------------------------------------------------------------- Hyprland
class HyprlandTracker : public FocusTracker {
    Q_OBJECT
public:
    explicit HyprlandTracker(QObject *parent = nullptr);
    static bool detected();
    QString id() const override { return QStringLiteral("hyprland"); }
    QString label() const override { return QStringLiteral("Hyprland IPC"); }
    CapState state() const override { return m_ok ? CapState::Supported : CapState::NotSupported; }
    void start() override;
    std::optional<QPoint> pointerPosition() override;
    // One-shot request on the command socket (synchronous, short timeout).
    static QByteArray request(const QByteArray &cmd, int timeoutMs = 300);
    // Runs a dispatcher. Hyprland 0.56 replaced `dispatch <name> <args>` with
    // Lua (`dispatch hl.dsp.…`), so the form that works is remembered and the
    // other one is tried when it fails. True only when Hyprland replied "ok".
    static bool dispatch(const QByteArray &legacy, const QByteArray &lua);
private:
    void onEvents();
    void refresh();
    static QString socketDir();
    QLocalSocket m_events;
    QByteArray m_buf;
    bool m_ok = false;
    bool m_refreshQueued = false;
};

// -------------------------------------------------------------------- Sway
class SwayTracker : public FocusTracker {
    Q_OBJECT
public:
    explicit SwayTracker(QObject *parent = nullptr);
    static bool detected();
    QString id() const override { return QStringLiteral("sway"); }
    QString label() const override { return QStringLiteral("Sway / i3 IPC"); }
    CapState state() const override { return m_ok ? CapState::Supported : CapState::NotSupported; }
    void start() override;
    static bool command(const QByteArray &cmd);
private:
    static QByteArray frame(quint32 type, const QByteArray &payload);
    void onReadable();
    void handleNode(const QJsonObject &node);
    QLocalSocket m_sock;
    QByteArray m_buf;
    bool m_ok = false;
};

// ------------------------------------------------- wlr-foreign-toplevel-mgmt
class WlrToplevelTracker : public FocusTracker {
    Q_OBJECT
public:
    explicit WlrToplevelTracker(QObject *parent = nullptr);
    static bool detected();
    QString id() const override { return QStringLiteral("wlr-foreign-toplevel"); }
    QString label() const override { return QStringLiteral("wlr foreign-toplevel protocol"); }
    CapState state() const override { return m_mgr ? CapState::Supported : CapState::NotSupported; }
    void start() override;

    // used by C callbacks
    struct Toplevel {
        QString appId, title;
        bool activated = false;
        bool pendingActivated = false;
    };
    void toplevelDone(void *handle);
    void toplevelClosed(void *handle);
    QHash<void *, Toplevel> toplevels;
    void addToplevel(void *handle);
private:
    zwlr_foreign_toplevel_manager_v1 *m_mgr = nullptr;
    void *m_lastActive = nullptr;
};

// --------------------------------------------------------------------- X11
class X11Tracker : public FocusTracker {
    Q_OBJECT
public:
    explicit X11Tracker(QObject *parent = nullptr);
    ~X11Tracker() override;
    static bool detected();
    QString id() const override { return QStringLiteral("x11"); }
    QString label() const override { return QStringLiteral("X11 EWMH (_NET_ACTIVE_WINDOW)"); }
    CapState state() const override { return m_dpy ? CapState::Supported : CapState::NotSupported; }
    void start() override;
    std::optional<QPoint> pointerPosition() override;
private:
    void onReadable();
    void refresh();
    _XDisplay *m_dpy = nullptr;
    QSocketNotifier *m_notifier = nullptr;
};

// --------------------------------------------- KWin / GNOME (D-Bus bridges)
// These compositors do not expose the focused window to ordinary clients;
// a KWin script or GNOME Shell extension calls our ReportFocus() method.
class BridgeTracker : public FocusTracker {
    Q_OBJECT
public:
    enum class Kind { KWin, Gnome, Generic };
    BridgeTracker(Kind kind, QObject *parent = nullptr);
    ~BridgeTracker() override;
    static bool kwinDetected();
    static bool gnomeDetected();
    QString id() const override;
    QString label() const override;
    CapState state() const override;
    void start() override;
    void report(const QJsonObject &payload);
    std::optional<QPoint> pointerPosition() override;
private:
    void loadKWinScript();
    void checkGnomeExtension();
    Kind m_kind;
    bool m_active = false;   // a report was received
    bool m_installed = false;
    std::optional<QPoint> m_lastPointer;
};

// Pick, enrich, and publish the focus context.
class ContextManager : public QObject {
    Q_OBJECT
public:
    explicit ContextManager(QObject *parent = nullptr);
    void start(const QString &preferred);
    FocusContext current() const { return m_simulated ? *m_simulated : m_current; }
    FocusTracker *tracker() const { return m_tracker; }
    BridgeTracker *bridge() const { return m_bridge; }
    void setSecureAppIds(const QStringList &ids) { m_userSecure = ids; }
    void simulate(const std::optional<FocusContext> &ctx);
    bool isSimulated() const { return m_simulated.has_value(); }
    std::optional<QPoint> pointerPosition();
    QJsonObject toJson(bool redact) const;
    static void enrich(FocusContext &ctx);
    bool isSecure(const FocusContext &ctx) const;

Q_SIGNALS:
    void contextChanged(const opendial::FocusContext &ctx);

private:
    void onFocus(const FocusContext &ctx);
    QPointer<FocusTracker> m_tracker;
    QPointer<BridgeTracker> m_bridge;
    FocusContext m_current;
    std::optional<FocusContext> m_simulated;
    QStringList m_userSecure;
    QList<FocusTracker *> m_all;
};

} // namespace opendial

Q_DECLARE_METATYPE(opendial::FocusContext)
