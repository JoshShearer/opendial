// SPDX-License-Identifier: MIT
#include "Tray.h"
#include "Engine.h"
#include "IconProvider.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <functional>
#include <QMenu>
#include <QPixmap>
#include <QProcess>
#include <QStandardPaths>
#include <QSystemTrayIcon>

namespace opendial {

void launchSettingsApp(const QString &page)
{
    // prefer the binary next to ours (build tree), then PATH
    QString exe = QCoreApplication::applicationDirPath() + QStringLiteral("/opendial");
    if (!QFileInfo(exe).isExecutable())
        exe = QCoreApplication::applicationDirPath() + QStringLiteral("/../ui/opendial");
    if (!QFileInfo(exe).isExecutable())
        exe = QStandardPaths::findExecutable(QStringLiteral("opendial"));
    if (exe.isEmpty())
        return;
    QStringList args;
    if (!page.isEmpty())
        args << QStringLiteral("--page") << page;
    QProcess::startDetached(exe, args);
}

Tray::Tray(Engine *engine, QObject *parent) : QObject(parent), m_engine(engine)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_icon = new QSystemTrayIcon(this);
    QIcon icon = QIcon::fromTheme(QStringLiteral("io.github.opendial.OpenDial"));
    if (icon.isNull()) {
        initIconResources();
        icon = QIcon(QStringLiteral(":/opendial/icons/app.svg"));
    }
    m_icon->setIcon(icon);
    m_icon->setToolTip(QStringLiteral("OpenDial"));
    m_menu = new QMenu;
    m_icon->setContextMenu(m_menu);
    connect(m_icon, &QSystemTrayIcon::activated, this, [](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick)
            launchSettingsApp();
    });
    connect(engine, &Engine::stateChanged, this, &Tray::rebuild);
    rebuild(engine->stateJson());
    m_icon->show();
}

Tray::~Tray() { delete m_menu; }

bool Tray::available() const { return m_icon != nullptr; }

void Tray::rebuild(const QJsonObject &s)
{
    if (!m_menu)
        return;
    const QJsonObject dev = s.value(QStringLiteral("device")).toObject();
    const bool connected = dev.value(QStringLiteral("connected")).toBool();
    const QString battery = dev.value(QStringLiteral("battery")).isDouble() ? QStringLiteral(" · %1%").arg(dev.value(QStringLiteral("battery")).toInt()) : QString();
    // rebuild only when something shown in the menu changed
    const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7").arg(connected).arg(battery, s.value(QStringLiteral("activeTool")).toString(),
                                                                 s.value(QStringLiteral("profileName")).toString(), s.value(QStringLiteral("lockedProfile")).toString())
                            .arg(s.value(QStringLiteral("paused")).toBool()).arg(QString::number(qHash(QJsonDocument(s.value(QStringLiteral("menuTools")).toArray()).toJson())));
    if (key == m_lastKey)
        return;
    m_lastKey = key;
    m_icon->setToolTip(QStringLiteral("OpenDial: %1\n%2 · %3").arg(connected ? tr("Dial connected") + battery : tr("Dial not connected"),
                                                                 s.value(QStringLiteral("profileName")).toString(),
                                                                 s.value(QStringLiteral("activeToolName")).toString()));
    m_menu->clear();
    auto *status = m_menu->addAction(connected ? tr("Surface Dial connected") + battery : tr("Surface Dial not connected"));
    status->setEnabled(false);
    auto *prof = m_menu->addAction(tr("Profile: %1").arg(s.value(QStringLiteral("profileName")).toString()));
    prof->setEnabled(false);
    m_menu->addSeparator();

    auto *tools = m_menu->addMenu(tr("Active tool"));
    std::function<void(QMenu *, const QJsonArray &)> addTools = [&](QMenu *menu, const QJsonArray &arr) {
        for (const auto &v : arr) {
            const QJsonObject t = v.toObject();
            if (t.value(QStringLiteral("kind")).toString() == QLatin1String("folder")) {
                addTools(menu->addMenu(t.value(QStringLiteral("name")).toString()), t.value(QStringLiteral("children")).toArray());
                continue;
            }
            const QString id = t.value(QStringLiteral("id")).toString();
            auto *a = menu->addAction(t.value(QStringLiteral("name")).toString());
            a->setCheckable(true);
            a->setChecked(id == s.value(QStringLiteral("activeTool")).toString());
            connect(a, &QAction::triggered, m_engine, [this, id] { m_engine->setActiveTool(id); });
        }
    };
    addTools(tools, s.value(QStringLiteral("menuTools")).toArray());

    const bool locked = !s.value(QStringLiteral("lockedProfile")).toString().isEmpty();
    auto *lock = m_menu->addAction(locked ? tr("Unlock profile (follow focused app)") : tr("Lock current profile"));
    connect(lock, &QAction::triggered, m_engine, [this, locked] {
        if (locked)
            m_engine->unlockProfile();
        else
            m_engine->lockProfile(m_engine->activeProfileId());
    });
    auto *menuAct = m_menu->addAction(tr("Show radial menu"));
    connect(menuAct, &QAction::triggered, m_engine, [this] { m_engine->menuOp(QStringLiteral("open")); });
    const bool paused = s.value(QStringLiteral("paused")).toBool();
    auto *pause = m_menu->addAction(paused ? tr("Resume") : tr("Pause"));
    connect(pause, &QAction::triggered, m_engine, [this, paused] { m_engine->setPaused(!paused); });
    m_menu->addSeparator();
    connect(m_menu->addAction(tr("Settings…")), &QAction::triggered, this, [] { launchSettingsApp(); });
    connect(m_menu->addAction(tr("Diagnostics…")), &QAction::triggered, this, [] { launchSettingsApp(QStringLiteral("diagnostics")); });
    m_menu->addSeparator();
    connect(m_menu->addAction(tr("Quit OpenDial service")), &QAction::triggered, qApp, &QCoreApplication::quit);
}

} // namespace opendial
