// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace opendial {

// Navigation model for the radial menu: pages ("More") when a ring holds more
// than maxPerRing entries, nested folders with a "Back" entry. Pure logic; the
// overlay renders visibleItems() and highlight().
class RadialMenuModel {
public:
    static inline const QString kMoreId = QStringLiteral("__more");
    static inline const QString kBackId = QStringLiteral("__back");

    struct Item {
        QString id;
        QString name;
        QString icon;
        QString hint;      // e.g. "⟳ Volume up · ⟲ down · ● mute"
        bool folder = false;
        bool synthetic = false; // More / Back
        bool active = false;    // the currently selected tool
    };

    enum class SelectKind { None, ActivateTool, EnteredFolder, WentBack, NextPage };
    struct SelectResult {
        SelectKind kind = SelectKind::None;
        QString toolId;
    };

    void setMaxPerRing(int n) { m_maxPerRing = qBound(3, n, 12); }
    void setWrap(bool w) { m_wrap = w; }

    // Open on the top level with the active tool highlighted (searching into
    // folders: the folder containing it is highlighted).
    void open(const QJsonArray &tools, const QString &activeToolId);
    void close();
    bool isOpen() const { return m_open; }

    // Move highlight by signed steps. Returns true if highlight changed;
    // boundaryHit set when wrap is off and the edge was reached.
    bool rotate(int steps, bool *boundaryHit = nullptr);
    SelectResult select();
    bool back(); // go up a level; returns false at the top level

    QList<Item> visibleItems() const;
    int highlight() const { return m_highlight; }
    int depth() const { return int(m_stack.size()) - 1; }
    int page() const { return m_stack.isEmpty() ? 0 : m_stack.last().page; }
    int pageCount() const;
    QString breadcrumb() const;
    QJsonObject toJson() const;

private:
    struct Level {
        QJsonArray tools;
        int page = 0;
        QString title;
    };
    QList<Item> buildVisible(const Level &lvl, int depth) const;
    int pagesFor(const Level &lvl, int depth) const;
    int perPage(const Level &lvl, int depth) const;

    QList<Level> m_stack;
    int m_highlight = 0;
    int m_maxPerRing = 7;
    bool m_wrap = true;
    bool m_open = false;
    QString m_activeId;
};

} // namespace opendial
