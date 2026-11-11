// SPDX-License-Identifier: MIT
#include "RadialMenu.h"
#include "Action.h"

namespace opendial {

namespace {
bool isFolder(const QJsonObject &t) { return t.value(QLatin1String("kind")).toString() == QLatin1String("folder"); }

bool containsTool(const QJsonArray &tools, const QString &id)
{
    for (const auto &v : tools) {
        const auto t = v.toObject();
        if (t.value(QLatin1String("id")).toString() == id)
            return true;
        if (isFolder(t) && containsTool(t.value(QLatin1String("children")).toArray(), id))
            return true;
    }
    return false;
}

QString hintFor(const QJsonObject &t)
{
    if (isFolder(t))
        return QStringLiteral("%1 tools").arg(t.value(QLatin1String("children")).toArray().size());
    const auto b = t.value(QLatin1String("bindings")).toObject();
    QStringList parts;
    auto add = [&](const QString &slot, const char *glyph) {
        if (b.value(slot).isObject() && b.value(slot).toObject().value(QLatin1String("type")).toString() != QLatin1String("none"))
            parts << QString::fromUtf8(glyph) + QLatin1Char(' ') + describeAction(b.value(slot).toObject());
    };
    add(slot::RotateCw, "↻");
    add(slot::RotateCcw, "↺");
    add(slot::Click, "●");
    return parts.join(QStringLiteral("  ·  "));
}
} // namespace

int RadialMenuModel::perPage(const Level &lvl, int depth) const
{
    // Slots available for tools on a ring: minus Back (when nested), minus
    // More (only when paging is needed).
    const int reserved = depth > 0 ? 1 : 0;
    const int n = lvl.tools.size();
    if (n + reserved <= m_maxPerRing)
        return qMax(1, n);
    return qMax(1, m_maxPerRing - reserved - 1);
}

int RadialMenuModel::pagesFor(const Level &lvl, int depth) const
{
    const int n = lvl.tools.size();
    const int pp = perPage(lvl, depth);
    return n == 0 ? 1 : (n + pp - 1) / pp;
}

int RadialMenuModel::pageCount() const
{
    return m_stack.isEmpty() ? 0 : pagesFor(m_stack.last(), depth());
}

QList<RadialMenuModel::Item> RadialMenuModel::buildVisible(const Level &lvl, int depth) const
{
    QList<Item> items;
    if (depth > 0)
        items.append({kBackId, QStringLiteral("Back"), QStringLiteral("back"), lvl.title, false, true, false});
    const int pp = perPage(lvl, depth);
    const int start = lvl.page * pp;
    for (int i = start; i < qMin(start + pp, int(lvl.tools.size())); ++i) {
        const auto t = lvl.tools.at(i).toObject();
        Item it;
        it.id = t.value(QLatin1String("id")).toString();
        it.name = t.value(QLatin1String("name")).toString();
        it.icon = t.value(QLatin1String("icon")).toString(isFolder(t) ? QStringLiteral("folder") : QStringLiteral("custom"));
        it.folder = isFolder(t);
        it.hint = hintFor(t);
        it.active = (it.id == m_activeId) || (it.folder && containsTool(t.value(QLatin1String("children")).toArray(), m_activeId));
        items.append(it);
    }
    if (pagesFor(lvl, depth) > 1)
        items.append({kMoreId, QStringLiteral("More"), QStringLiteral("more"),
                      QStringLiteral("Page %1 of %2").arg(lvl.page + 1).arg(pagesFor(lvl, depth)), false, true, false});
    return items;
}

QList<RadialMenuModel::Item> RadialMenuModel::visibleItems() const
{
    if (m_stack.isEmpty())
        return {};
    return buildVisible(m_stack.last(), depth());
}

void RadialMenuModel::open(const QJsonArray &tools, const QString &activeToolId)
{
    m_stack.clear();
    m_stack.append({tools, 0, QString()});
    m_activeId = activeToolId;
    m_open = true;
    m_highlight = 0;
    // find page + index of the active tool (or its folder)
    const int pp = perPage(m_stack.last(), 0);
    for (int i = 0; i < tools.size(); ++i) {
        const auto t = tools.at(i).toObject();
        if (t.value(QLatin1String("id")).toString() == activeToolId
            || (isFolder(t) && containsTool(t.value(QLatin1String("children")).toArray(), activeToolId))) {
            m_stack.last().page = i / pp;
            m_highlight = i % pp;
            break;
        }
    }
}

void RadialMenuModel::close()
{
    m_open = false;
    m_stack.clear();
    m_highlight = 0;
}

bool RadialMenuModel::rotate(int steps, bool *boundaryHit)
{
    if (boundaryHit) *boundaryHit = false;
    if (!m_open || steps == 0)
        return false;
    const int n = visibleItems().size();
    if (n <= 0)
        return false;
    const int before = m_highlight;
    if (m_wrap) {
        m_highlight = ((m_highlight + steps) % n + n) % n;
    } else {
        const int target = m_highlight + steps;
        m_highlight = qBound(0, target, n - 1);
        if ((target < 0 || target > n - 1) && boundaryHit)
            *boundaryHit = true;
    }
    return m_highlight != before;
}

RadialMenuModel::SelectResult RadialMenuModel::select()
{
    SelectResult r;
    if (!m_open)
        return r;
    const auto items = visibleItems();
    if (m_highlight < 0 || m_highlight >= items.size())
        return r;
    const Item it = items.at(m_highlight);
    if (it.id == kBackId) {
        back();
        r.kind = SelectKind::WentBack;
        return r;
    }
    if (it.id == kMoreId) {
        auto &lvl = m_stack.last();
        lvl.page = (lvl.page + 1) % pagesFor(lvl, depth());
        m_highlight = depth() > 0 ? 1 : 0;
        r.kind = SelectKind::NextPage;
        return r;
    }
    if (it.folder) {
        const auto &lvl = m_stack.last();
        for (const auto &v : lvl.tools) {
            const auto t = v.toObject();
            if (t.value(QLatin1String("id")).toString() == it.id) {
                m_stack.append({t.value(QLatin1String("children")).toArray(), 0, it.name});
                // highlight active child if present, else first real entry
                m_highlight = 1;
                const auto kids = t.value(QLatin1String("children")).toArray();
                const int pp = perPage(m_stack.last(), depth());
                for (int i = 0; i < kids.size(); ++i)
                    if (kids.at(i).toObject().value(QLatin1String("id")).toString() == m_activeId) {
                        m_stack.last().page = i / pp;
                        m_highlight = 1 + i % pp;
                    }
                break;
            }
        }
        r.kind = SelectKind::EnteredFolder;
        return r;
    }
    r.kind = SelectKind::ActivateTool;
    r.toolId = it.id;
    m_activeId = it.id;
    return r;
}

bool RadialMenuModel::back()
{
    if (m_stack.size() <= 1)
        return false;
    const QString title = m_stack.last().title;
    m_stack.removeLast();
    // highlight the folder we came from
    const auto items = visibleItems();
    m_highlight = 0;
    for (int i = 0; i < items.size(); ++i)
        if (items.at(i).folder && items.at(i).name == title)
            m_highlight = i;
    return true;
}

QString RadialMenuModel::breadcrumb() const
{
    QStringList l;
    for (const auto &lvl : m_stack)
        if (!lvl.title.isEmpty())
            l << lvl.title;
    return l.join(QStringLiteral(" › "));
}

QJsonObject RadialMenuModel::toJson() const
{
    QJsonArray items;
    for (const auto &it : visibleItems())
        items.append(QJsonObject{{QStringLiteral("id"), it.id},
                                 {QStringLiteral("name"), it.name},
                                 {QStringLiteral("icon"), it.icon},
                                 {QStringLiteral("hint"), it.hint},
                                 {QStringLiteral("folder"), it.folder},
                                 {QStringLiteral("synthetic"), it.synthetic},
                                 {QStringLiteral("active"), it.active}});
    return QJsonObject{{QStringLiteral("open"), m_open},
                       {QStringLiteral("items"), items},
                       {QStringLiteral("highlight"), m_highlight},
                       {QStringLiteral("depth"), depth()},
                       {QStringLiteral("page"), page()},
                       {QStringLiteral("pageCount"), pageCount()},
                       {QStringLiteral("breadcrumb"), breadcrumb()}};
}

} // namespace opendial
