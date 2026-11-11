// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QObject>

class QSystemTrayIcon;
class QMenu;

namespace opendial {

class Engine;

// StatusNotifierItem tray icon (Qt uses SNI over D-Bus on Linux).
class Tray : public QObject {
    Q_OBJECT
public:
    explicit Tray(Engine *engine, QObject *parent = nullptr);
    ~Tray() override;
    bool available() const;
private:
    void rebuild(const QJsonObject &state);
    Engine *m_engine;
    QSystemTrayIcon *m_icon = nullptr;
    QMenu *m_menu = nullptr;
    QString m_lastKey;
};

void launchSettingsApp(const QString &page = {});

} // namespace opendial
