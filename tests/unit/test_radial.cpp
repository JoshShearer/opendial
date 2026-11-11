// SPDX-License-Identifier: MIT
#include "Config.h"
#include "RadialMenu.h"

#include <QtTest>

using namespace opendial;

namespace {
QJsonArray tools(int n, const QString &prefix = QStringLiteral("t"))
{
    QJsonArray a;
    for (int i = 0; i < n; ++i)
        a.append(QJsonObject{{"id", prefix + QString::number(i)}, {"name", prefix + QString::number(i)}});
    return a;
}
QStringList ids(const RadialMenuModel &m)
{
    QStringList l;
    for (const auto &i : m.visibleItems())
        l << i.id;
    return l;
}
} // namespace

class TestRadial : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void openHighlightsActive()
    {
        RadialMenuModel m;
        m.open(tools(5), "t3");
        QVERIFY(m.isOpen());
        QCOMPARE(m.highlight(), 3);
        QVERIFY(m.visibleItems().at(3).active);
    }

    void rotateWraps()
    {
        RadialMenuModel m;
        m.open(tools(4), "t0");
        m.rotate(-1);
        QCOMPARE(m.highlight(), 3);
        m.rotate(6);
        QCOMPARE(m.highlight(), 1);
    }

    void noWrapReportsBoundary()
    {
        RadialMenuModel m;
        m.setWrap(false);
        m.open(tools(4), "t0");
        bool hit = false;
        QVERIFY(!m.rotate(-1, &hit));
        QVERIFY(hit);
        m.rotate(10, &hit);
        QCOMPARE(m.highlight(), 3);
        QVERIFY(hit);
    }

    void selectActivates()
    {
        RadialMenuModel m;
        m.open(tools(4), "t0");
        m.rotate(2);
        auto r = m.select();
        QCOMPARE(r.kind, RadialMenuModel::SelectKind::ActivateTool);
        QCOMPARE(r.toolId, QStringLiteral("t2"));
    }

    void pagingBeyondSeven()
    {
        RadialMenuModel m;
        m.setMaxPerRing(7);
        m.open(tools(15), "t0");
        QCOMPARE(m.pageCount(), 3); // 6 per page + More
        QCOMPARE(ids(m).size(), 7);
        QCOMPARE(ids(m).last(), RadialMenuModel::kMoreId);
        m.rotate(-1); // to More
        auto r = m.select();
        QCOMPARE(r.kind, RadialMenuModel::SelectKind::NextPage);
        QCOMPARE(m.page(), 1);
        QCOMPARE(ids(m).first(), QStringLiteral("t6"));
        m.rotate(-1);
        m.select();
        m.rotate(-1);
        m.select(); // wraps to page 0
        QCOMPARE(m.page(), 0);
    }

    void exactlySevenNoPaging()
    {
        RadialMenuModel m;
        m.open(tools(7), "t0");
        QCOMPARE(m.pageCount(), 1);
        QVERIFY(!ids(m).contains(RadialMenuModel::kMoreId));
    }

    void activeOnLaterPage()
    {
        RadialMenuModel m;
        m.open(tools(15), "t13");
        QCOMPARE(m.page(), 2);
        QCOMPARE(ids(m).at(m.highlight()), QStringLiteral("t13"));
    }

    void foldersAndBack()
    {
        QJsonArray t = tools(3);
        t.append(QJsonObject{{"id", "f"}, {"name", "Folder"}, {"kind", "folder"}, {"children", tools(3, "c")}});
        RadialMenuModel m;
        m.open(t, "c1");
        QCOMPARE(m.highlight(), 3); // folder containing active tool
        QVERIFY(m.visibleItems().at(3).active);
        auto r = m.select();
        QCOMPARE(r.kind, RadialMenuModel::SelectKind::EnteredFolder);
        QCOMPARE(m.depth(), 1);
        QCOMPARE(ids(m).first(), RadialMenuModel::kBackId);
        QCOMPARE(ids(m).at(m.highlight()), QStringLiteral("c1"));
        QCOMPARE(m.breadcrumb(), QStringLiteral("Folder"));
        m.rotate(-2); // to Back
        r = m.select();
        QCOMPARE(r.kind, RadialMenuModel::SelectKind::WentBack);
        QCOMPARE(m.depth(), 0);
        QCOMPARE(m.highlight(), 3);
    }

    void defaultMenuHasSevenOrFewer()
    {
        const auto g = defaultConfig().value("profiles").toArray().first().toObject();
        RadialMenuModel m;
        m.open(g.value("tools").toArray(), "volume");
        QVERIFY(m.visibleItems().size() <= 7);
        QCOMPARE(m.pageCount(), 1);
        QVERIFY(m.visibleItems().first().hint.contains("Volume"));
    }

    void thousandTransitions()
    {
        RadialMenuModel m;
        QJsonArray t = tools(20);
        t.append(QJsonObject{{"id", "f"}, {"name", "F"}, {"kind", "folder"}, {"children", tools(9, "c")}});
        QRandomGenerator rng(7);
        for (int i = 0; i < 1000; ++i) {
            if (!m.isOpen())
                m.open(t, "t0");
            m.rotate(rng.bounded(-5, 6));
            auto r = m.select();
            QVERIFY(m.highlight() >= 0 && m.highlight() < m.visibleItems().size());
            if (r.kind == RadialMenuModel::SelectKind::ActivateTool)
                m.close();
        }
    }
};

QTEST_GUILESS_MAIN(TestRadial)
#include "test_radial.moc"
