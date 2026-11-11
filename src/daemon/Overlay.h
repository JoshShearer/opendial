// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QTimer>

#include "SystemTheme.h"

class QQuickView;
class QScreen;

namespace opendial {

class Engine;

// Radial menu + OSD windows. On wlroots/KWin/Hyprland they are layer-shell
// overlay surfaces (no focus, no input, never tiled). Elsewhere they are
// frameless, input-transparent, always-on-top tool windows.
class Overlay : public QObject {
    Q_OBJECT
public:
    explicit Overlay(Engine *engine, QObject *parent = nullptr);
    ~Overlay() override;
    bool usesLayerShell() const { return m_layerShell; }
    QString placementDetail() const { return m_detail; }
    SystemTheme *theme() { return &m_theme; }

    void showMenu(const QJsonObject &menu);
    void hideMenu();
    void showOsd(const QJsonObject &osd);

private:
    QQuickView *createView(const QString &qml, const QString &scope);
    void applyTheme(QQuickView *v);
    void place(QQuickView *v, QScreen *screen, QPoint topLeftGlobal, bool osd, const QString &position);

    Engine *m_engine;
    SystemTheme m_theme;
    QQuickView *m_menu = nullptr;
    QQuickView *m_osd = nullptr;
    QTimer m_osdTimer;
    bool m_layerShell = false;
    QString m_detail;
};

} // namespace opendial
