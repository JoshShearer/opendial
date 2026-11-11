// SPDX-License-Identifier: MIT
#include "Profiles.h"

#include <QRegularExpression>
#include <QSet>

namespace opendial {

QJsonObject FocusContext::toJson(bool redact) const
{
    QJsonObject o{{QStringLiteral("appId"), appId},
                  {QStringLiteral("windowClass"), windowClass},
                  {QStringLiteral("exe"), exe},
                  {QStringLiteral("flatpakId"), flatpakId},
                  {QStringLiteral("workspace"), workspace},
                  {QStringLiteral("pid"), pid},
                  {QStringLiteral("source"), source},
                  {QStringLiteral("secure"), secure}};
    o.insert(QStringLiteral("title"), redact ? (title.isEmpty() ? QString() : QStringLiteral("<redacted>")) : title);
    return o;
}

FocusContext FocusContext::fromJson(const QJsonObject &o)
{
    FocusContext c;
    c.appId = o.value(QLatin1String("appId")).toString();
    c.windowClass = o.value(QLatin1String("windowClass")).toString();
    c.title = o.value(QLatin1String("title")).toString();
    c.exe = o.value(QLatin1String("exe")).toString();
    c.flatpakId = o.value(QLatin1String("flatpakId")).toString();
    c.workspace = o.value(QLatin1String("workspace")).toString();
    c.pid = o.value(QLatin1String("pid")).toInteger();
    c.source = o.value(QLatin1String("source")).toString();
    c.secure = o.value(QLatin1String("secure")).toBool();
    return c;
}

QString normalizeAppId(const QString &id)
{
    QString s = id.trimmed().toLower();
    if (s.endsWith(QLatin1String(".desktop")))
        s.chop(8);
    if (s.endsWith(QLatin1String("-bin")))
        s.chop(4);
    return s;
}

QStringList appIdCandidates(const FocusContext &ctx)
{
    QStringList c;
    auto add = [&](const QString &v) {
        const QString n = normalizeAppId(v);
        if (!n.isEmpty() && !c.contains(n))
            c << n;
    };
    add(ctx.appId);
    add(ctx.windowClass);
    add(ctx.flatpakId);
    add(ctx.exe);
    // org.kde.krita -> krita (reverse-DNS last component)
    for (const QString &v : QStringList{ctx.appId, ctx.flatpakId}) {
        const QString n = normalizeAppId(v);
        if (n.count(QLatin1Char('.')) >= 2)
            add(n.section(QLatin1Char('.'), -1));
    }
    return c;
}

QString layerName(ProfileLayer l)
{
    switch (l) {
    case ProfileLayer::Global: return QStringLiteral("global");
    case ProfileLayer::Workspace: return QStringLiteral("workspace");
    case ProfileLayer::App: return QStringLiteral("app");
    case ProfileLayer::Window: return QStringLiteral("window");
    case ProfileLayer::Lock: return QStringLiteral("lock");
    }
    return {};
}

QJsonObject Resolution::toJson() const
{
    return QJsonObject{{QStringLiteral("profileId"), profileId},
                       {QStringLiteral("layer"), layerName(layer)},
                       {QStringLiteral("reason"), reason},
                       {QStringLiteral("candidates"), QJsonArray::fromStringList(candidates)}};
}

namespace {
bool idMatches(const QJsonArray &patterns, const QStringList &cands, QString *matched)
{
    for (const auto &pv : patterns) {
        const QString pat = normalizeAppId(pv.toString());
        if (pat.isEmpty())
            continue;
        const bool glob = pat.contains(QLatin1Char('*')) || pat.contains(QLatin1Char('?'));
        QRegularExpression re;
        if (glob)
            re = QRegularExpression(QRegularExpression::wildcardToRegularExpression(pat));
        // A reverse-DNS pattern (org.kde.krita) also matches its short name
        // (krita), which is what X11 WM_CLASS and many exes report.
        const QString shortPat = (!glob && pat.count(QLatin1Char('.')) >= 2) ? pat.section(QLatin1Char('.'), -1) : QString();
        for (const auto &c : cands) {
            if ((glob && re.match(c).hasMatch()) || (!glob && (c == pat || (!shortPat.isEmpty() && c == shortPat)))) {
                if (matched) *matched = c;
                return true;
            }
        }
    }
    return false;
}

struct Match {
    QString id;
    ProfileLayer layer;
    int priority;
    int index;
    QString reason;
};
} // namespace

Resolution resolveProfile(const QJsonArray &profiles, const FocusContext &ctx, const QString &locked)
{
    Resolution r;
    QList<Match> matches;
    const QStringList cands = appIdCandidates(ctx);
    for (int i = 0; i < profiles.size(); ++i) {
        const auto p = profiles.at(i).toObject();
        const QString id = p.value(QLatin1String("id")).toString();
        const auto m = p.value(QLatin1String("match")).toObject();
        const QString kind = m.value(QLatin1String("kind")).toString();
        const int prio = p.value(QLatin1String("priority")).toInt(0);
        if (!locked.isEmpty() && id == locked) {
            matches.append({id, ProfileLayer::Lock, prio, i, QStringLiteral("manually locked")});
            continue;
        }
        if (!p.value(QLatin1String("enabled")).toBool(true))
            continue;
        QString which;
        if (kind == QLatin1String("global")) {
            matches.append({id, ProfileLayer::Global, prio, i, QStringLiteral("global default")});
        } else if (kind == QLatin1String("app")) {
            if (idMatches(m.value(QLatin1String("appIds")).toArray(), cands, &which))
                matches.append({id, ProfileLayer::App, prio, i, QStringLiteral("app id '%1'").arg(which)});
        } else if (kind == QLatin1String("window")) {
            if (!idMatches(m.value(QLatin1String("appIds")).toArray(), cands, &which))
                continue;
            const QString cls = m.value(QLatin1String("windowClass")).toString();
            if (!cls.isEmpty() && normalizeAppId(cls) != normalizeAppId(ctx.windowClass))
                continue;
            const QString tre = m.value(QLatin1String("titleRegex")).toString();
            if (!tre.isEmpty()) {
                QRegularExpression re(tre, QRegularExpression::CaseInsensitiveOption);
                if (!re.isValid() || !re.match(ctx.title).hasMatch())
                    continue;
            }
            matches.append({id, ProfileLayer::Window, prio, i, QStringLiteral("window of '%1'").arg(which)});
        } else if (kind == QLatin1String("workspace")) {
            for (const auto &w : m.value(QLatin1String("workspaces")).toArray())
                if (!ctx.workspace.isEmpty() && w.toString() == ctx.workspace) {
                    matches.append({id, ProfileLayer::Workspace, prio, i, QStringLiteral("workspace '%1'").arg(ctx.workspace)});
                    break;
                }
        }
        // kind == manual: only selectable by lock
    }
    std::stable_sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) {
        if (a.layer != b.layer)
            return int(a.layer) > int(b.layer);
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.index < b.index;
    });
    for (const auto &m : matches)
        r.candidates << m.id;
    if (matches.isEmpty()) {
        r.profileId = QStringLiteral("global");
        r.reason = QStringLiteral("no profile matched; using built-in global");
        return r;
    }
    r.profileId = matches.first().id;
    r.layer = matches.first().layer;
    r.reason = matches.first().reason;
    return r;
}

QJsonObject profileById(const QJsonArray &profiles, const QString &id)
{
    for (const auto &v : profiles)
        if (v.toObject().value(QLatin1String("id")).toString() == id)
            return v.toObject();
    return {};
}

QJsonObject globalProfile(const QJsonArray &profiles)
{
    for (const auto &v : profiles)
        if (v.toObject().value(QLatin1String("match")).toObject().value(QLatin1String("kind")).toString() == QLatin1String("global"))
            return v.toObject();
    return {};
}

QJsonArray enabledTools(const QJsonArray &tools)
{
    QJsonArray out;
    for (const auto &v : tools) {
        auto t = v.toObject();
        if (!t.value(QLatin1String("enabled")).toBool(true))
            continue;
        if (t.value(QLatin1String("kind")).toString() == QLatin1String("folder")) {
            const auto kids = enabledTools(t.value(QLatin1String("children")).toArray());
            if (kids.isEmpty())
                continue;
            t.insert(QStringLiteral("children"), kids);
        }
        out.append(t);
    }
    return out;
}

QJsonArray composeMenuTools(const QJsonArray &profiles, const QString &profileId, const QJsonArray &extra)
{
    QJsonObject p = profileById(profiles, profileId);
    const QJsonObject g = globalProfile(profiles);
    if (p.isEmpty())
        p = g;
    QJsonArray out;
    QSet<QString> ids;
    auto addAll = [&](const QJsonArray &tools) {
        for (const auto &v : tools) {
            const QString id = v.toObject().value(QLatin1String("id")).toString();
            if (ids.contains(id))
                continue;
            ids.insert(id);
            out.append(v);
        }
    };
    addAll(p.value(QLatin1String("tools")).toArray());
    if (p.value(QLatin1String("menuMode")).toString() == QLatin1String("extend")
        && p.value(QLatin1String("id")) != g.value(QLatin1String("id")))
        addAll(g.value(QLatin1String("tools")).toArray());
    addAll(extra);
    return enabledTools(out);
}

} // namespace opendial
