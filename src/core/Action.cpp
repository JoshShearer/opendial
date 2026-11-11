// SPDX-License-Identifier: MIT
#include "Action.h"
#include "KeyChord.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace opendial {

QStringList slot::all()
{
    return {RotateCw, RotateCcw, Click, DoubleClick, TripleClick, LongPress, PressedCw, PressedCcw, Release};
}

QStringList actionTypes()
{
    return {QStringLiteral("none"),    QStringLiteral("key"),        QStringLiteral("text"),
            QStringLiteral("mouse"),   QStringLiteral("scroll"),     QStringLiteral("zoom"),
            QStringLiteral("volume"),  QStringLiteral("mute"),       QStringLiteral("media"),
            QStringLiteral("brightness"), QStringLiteral("workspace"), QStringLiteral("tool"),
            QStringLiteral("profile"), QStringLiteral("menu"),       QStringLiteral("haptic"),
            QStringLiteral("value"),   QStringLiteral("osc"),        QStringLiteral("launch"),
            QStringLiteral("command"), QStringLiteral("dbus"),       QStringLiteral("macro"),
            QStringLiteral("if"),      QStringLiteral("plugin"),     QStringLiteral("osd")};
}

namespace {
QString str(const QJsonObject &o, const char *k) { return o.value(QLatin1String(k)).toString(); }

void err(ValidationIssues &is, const QString &p, const QString &m) { is.append({p, m, false}); }
void warn(ValidationIssues &is, const QString &p, const QString &m) { is.append({p, m, true}); }

bool oneOf(const QString &v, std::initializer_list<const char *> opts)
{
    for (auto *o : opts)
        if (v == QLatin1String(o))
            return true;
    return false;
}

void needNumber(const QJsonObject &o, const char *k, const QString &path, ValidationIssues &is,
                double lo, double hi, bool required)
{
    const auto v = o.value(QLatin1String(k));
    if (v.isUndefined()) {
        if (required)
            err(is, path + QLatin1Char('.') + QLatin1String(k), QStringLiteral("required number"));
        return;
    }
    if (!v.isDouble()) {
        err(is, path + QLatin1Char('.') + QLatin1String(k), QStringLiteral("must be a number"));
        return;
    }
    if (v.toDouble() < lo || v.toDouble() > hi)
        err(is, path + QLatin1Char('.') + QLatin1String(k),
            QStringLiteral("must be between %1 and %2").arg(lo).arg(hi));
}

bool isLoopbackHost(const QString &h)
{
    if (h == QLatin1String("localhost"))
        return true;
    QHostAddress a(h);
    return !a.isNull() && a.isLoopback();
}
} // namespace

void validateAction(const QJsonObject &a, const QString &path, ValidationIssues &is, int depth)
{
    if (depth > kMaxActionDepth) {
        err(is, path, QStringLiteral("actions nested deeper than %1 levels").arg(kMaxActionDepth));
        return;
    }
    const QString t = str(a, "type");
    if (t.isEmpty()) {
        err(is, path + QStringLiteral(".type"), QStringLiteral("missing action type"));
        return;
    }
    if (!actionTypes().contains(t)) {
        err(is, path + QStringLiteral(".type"), QStringLiteral("unknown action type '%1'").arg(t));
        return;
    }
    if (t == QLatin1String("key")) {
        auto r = parseKeySequence(str(a, "keys"));
        if (!r.ok())
            err(is, path + QStringLiteral(".keys"), r.error);
        needNumber(a, "releaseAfterMs", path, is, 50, 10000, false);
    } else if (t == QLatin1String("text")) {
        if (str(a, "text").isEmpty())
            err(is, path + QStringLiteral(".text"), QStringLiteral("text is empty"));
        else if (str(a, "text").size() > 512)
            err(is, path + QStringLiteral(".text"), QStringLiteral("text longer than 512 characters"));
    } else if (t == QLatin1String("mouse")) {
        if (!oneOf(str(a, "button"), {"left", "right", "middle", "back", "forward"}))
            err(is, path + QStringLiteral(".button"), QStringLiteral("button must be left/right/middle/back/forward"));
    } else if (t == QLatin1String("scroll")) {
        if (a.contains(QLatin1String("axis")) && !oneOf(str(a, "axis"), {"vertical", "horizontal"}))
            err(is, path + QStringLiteral(".axis"), QStringLiteral("axis must be vertical or horizontal"));
        needNumber(a, "amount", path, is, -50, 50, false);
    } else if (t == QLatin1String("zoom")) {
        if (!oneOf(str(a, "direction"), {"in", "out", "reset"}))
            err(is, path + QStringLiteral(".direction"), QStringLiteral("direction must be in/out/reset"));
        if (a.contains(QLatin1String("method")) && !oneOf(str(a, "method"), {"ctrlScroll", "keys"}))
            err(is, path + QStringLiteral(".method"), QStringLiteral("method must be ctrlScroll or keys"));
    } else if (t == QLatin1String("volume")) {
        needNumber(a, "delta", path, is, -100, 100, true);
        needNumber(a, "max", path, is, 10, 200, false);
        if (a.contains(QLatin1String("target")) && !oneOf(str(a, "target"), {"sink", "source"}))
            err(is, path + QStringLiteral(".target"), QStringLiteral("target must be sink or source"));
    } else if (t == QLatin1String("mute")) {
        if (a.contains(QLatin1String("target")) && !oneOf(str(a, "target"), {"sink", "source"}))
            err(is, path + QStringLiteral(".target"), QStringLiteral("target must be sink or source"));
    } else if (t == QLatin1String("media")) {
        if (!oneOf(str(a, "command"), {"playpause", "play", "pause", "stop", "next", "previous", "seek", "rate", "volume"}))
            err(is, path + QStringLiteral(".command"), QStringLiteral("unknown media command"));
        needNumber(a, "offsetMs", path, is, -600000, 600000, str(a, "command") == QLatin1String("seek"));
    } else if (t == QLatin1String("brightness")) {
        needNumber(a, "delta", path, is, -100, 100, true);
    } else if (t == QLatin1String("workspace")) {
        if (!oneOf(str(a, "direction"), {"next", "previous"}))
            err(is, path + QStringLiteral(".direction"), QStringLiteral("direction must be next or previous"));
    } else if (t == QLatin1String("tool")) {
        if (!oneOf(str(a, "op"), {"next", "previous", "select"}))
            err(is, path + QStringLiteral(".op"), QStringLiteral("op must be next/previous/select"));
        if (str(a, "op") == QLatin1String("select") && str(a, "id").isEmpty())
            err(is, path + QStringLiteral(".id"), QStringLiteral("select needs a tool id"));
    } else if (t == QLatin1String("profile")) {
        if (!oneOf(str(a, "op"), {"cycle", "lock", "unlock"}))
            err(is, path + QStringLiteral(".op"), QStringLiteral("op must be cycle/lock/unlock"));
    } else if (t == QLatin1String("menu")) {
        if (!oneOf(str(a, "op"), {"open", "close", "toggle"}))
            err(is, path + QStringLiteral(".op"), QStringLiteral("op must be open/close/toggle"));
    } else if (t == QLatin1String("haptic")) {
        if (!oneOf(str(a, "waveform"), {"click", "buzz", "rumble"}))
            err(is, path + QStringLiteral(".waveform"), QStringLiteral("waveform must be click/buzz/rumble"));
        needNumber(a, "repeat", path, is, 0, 10, false);
    } else if (t == QLatin1String("value")) {
        static const QRegularExpression id(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.-]{0,63}$"));
        if (!id.match(str(a, "variable")).hasMatch())
            err(is, path + QStringLiteral(".variable"), QStringLiteral("variable name must be an identifier"));
        needNumber(a, "step", path, is, -1e6, 1e6, true);
        needNumber(a, "min", path, is, -1e9, 1e9, false);
        needNumber(a, "max", path, is, -1e9, 1e9, false);
        if (a.value(QLatin1String("min")).toDouble(0) > a.value(QLatin1String("max")).toDouble(1e9))
            err(is, path, QStringLiteral("min greater than max"));
        if (a.value(QLatin1String("output")).isObject())
            validateAction(a.value(QLatin1String("output")).toObject(), path + QStringLiteral(".output"), is, depth + 1);
    } else if (t == QLatin1String("osc")) {
        if (str(a, "host").isEmpty())
            err(is, path + QStringLiteral(".host"), QStringLiteral("host required"));
        needNumber(a, "port", path, is, 1, 65535, true);
        if (!str(a, "address").startsWith(QLatin1Char('/')))
            err(is, path + QStringLiteral(".address"), QStringLiteral("OSC address must start with '/'"));
    } else if (t == QLatin1String("launch")) {
        const int n = int(!str(a, "desktopId").isEmpty()) + int(!str(a, "url").isEmpty()) + int(!str(a, "path").isEmpty());
        if (n != 1)
            err(is, path, QStringLiteral("launch needs exactly one of desktopId, url, path"));
        const QString url = str(a, "url");
        if (!url.isEmpty() && !url.startsWith(QLatin1String("https://")) && !url.startsWith(QLatin1String("http://"))
            && !url.startsWith(QLatin1String("mailto:")))
            err(is, path + QStringLiteral(".url"), QStringLiteral("only http(s) and mailto URLs are allowed"));
    } else if (t == QLatin1String("command")) {
        const auto argv = a.value(QLatin1String("argv")).toArray();
        if (argv.isEmpty() || argv.first().toString().isEmpty())
            err(is, path + QStringLiteral(".argv"), QStringLiteral("argv must be a non-empty array (no shell strings)"));
        for (const auto &v : argv)
            if (!v.isString())
                err(is, path + QStringLiteral(".argv"), QStringLiteral("argv entries must be strings"));
        needNumber(a, "timeoutMs", path, is, 100, 600000, false);
    } else if (t == QLatin1String("dbus")) {
        for (const char *k : {"service", "path", "interface", "method"})
            if (str(a, k).isEmpty())
                err(is, path + QLatin1Char('.') + QLatin1String(k), QStringLiteral("required"));
        if (a.contains(QLatin1String("bus")) && !oneOf(str(a, "bus"), {"session", "system"}))
            err(is, path + QStringLiteral(".bus"), QStringLiteral("bus must be session or system"));
    } else if (t == QLatin1String("macro")) {
        const auto steps = a.value(QLatin1String("steps")).toArray();
        if (steps.isEmpty())
            err(is, path + QStringLiteral(".steps"), QStringLiteral("macro has no steps"));
        if (steps.size() > kMaxMacroSteps)
            err(is, path + QStringLiteral(".steps"), QStringLiteral("at most %1 steps").arg(kMaxMacroSteps));
        needNumber(a, "repeat", path, is, 1, kMaxMacroRepeat, false);
        qint64 totalDelay = 0;
        for (int i = 0; i < steps.size(); ++i) {
            const auto s = steps.at(i).toObject();
            const QString sp = path + QStringLiteral(".steps[%1]").arg(i);
            if (s.contains(QLatin1String("delayMs"))) {
                needNumber(s, "delayMs", sp, is, 0, 10000, true);
                totalDelay += s.value(QLatin1String("delayMs")).toInt();
            } else if (s.value(QLatin1String("action")).isObject()) {
                validateAction(s.value(QLatin1String("action")).toObject(), sp + QStringLiteral(".action"), is, depth + 1);
            } else {
                err(is, sp, QStringLiteral("step needs 'action' or 'delayMs'"));
            }
        }
        if (totalDelay * qMax(1, a.value(QLatin1String("repeat")).toInt(1)) > kMaxMacroTotalDelayMs)
            err(is, path, QStringLiteral("total macro delay exceeds %1 ms").arg(kMaxMacroTotalDelayMs));
    } else if (t == QLatin1String("if")) {
        if (!a.value(QLatin1String("condition")).isObject())
            err(is, path + QStringLiteral(".condition"), QStringLiteral("condition object required"));
        else {
            const auto c = a.value(QLatin1String("condition")).toObject();
            if (c.contains(QLatin1String("op")) && !oneOf(str(c, "op"), {"lt", "le", "eq", "ne", "ge", "gt", "<", "<=", "==", "!=", ">=", ">"}))
                err(is, path + QStringLiteral(".condition.op"), QStringLiteral("op must be lt/le/eq/ne/ge/gt"));
        }
        if (!a.value(QLatin1String("then")).isObject())
            err(is, path + QStringLiteral(".then"), QStringLiteral("'then' action required"));
        else
            validateAction(a.value(QLatin1String("then")).toObject(), path + QStringLiteral(".then"), is, depth + 1);
        if (a.value(QLatin1String("else")).isObject())
            validateAction(a.value(QLatin1String("else")).toObject(), path + QStringLiteral(".else"), is, depth + 1);
    } else if (t == QLatin1String("plugin")) {
        if (str(a, "appId").isEmpty() || str(a, "toolId").isEmpty())
            err(is, path, QStringLiteral("plugin action needs appId and toolId"));
    } else if (t == QLatin1String("osd")) {
        if (str(a, "text").size() > 200)
            warn(is, path + QStringLiteral(".text"), QStringLiteral("OSD text will be truncated"));
    }
}

Risk actionRisk(const QJsonObject &a)
{
    const QString t = str(a, "type");
    if (t == QLatin1String("command") || t == QLatin1String("dbus") || t == QLatin1String("launch"))
        return Risk::NeedsApproval;
    if (t == QLatin1String("osc") && !isLoopbackHost(str(a, "host")))
        return Risk::NeedsApproval;
    if (t == QLatin1String("macro")) {
        for (const auto &s : a.value(QLatin1String("steps")).toArray())
            if (actionRisk(s.toObject().value(QLatin1String("action")).toObject()) == Risk::NeedsApproval)
                return Risk::NeedsApproval;
    }
    if (t == QLatin1String("if")) {
        if (actionRisk(a.value(QLatin1String("then")).toObject()) == Risk::NeedsApproval
            || actionRisk(a.value(QLatin1String("else")).toObject()) == Risk::NeedsApproval)
            return Risk::NeedsApproval;
    }
    if (t == QLatin1String("value") && a.value(QLatin1String("output")).isObject())
        return actionRisk(a.value(QLatin1String("output")).toObject());
    return Risk::Safe;
}

QString actionFingerprint(const QJsonObject &a)
{
    // QJsonDocument serialises object keys in sorted order: canonical form.
    const QByteArray canon = QJsonDocument(a).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(canon, QCryptographicHash::Sha256).toHex());
}

QString describeAction(const QJsonObject &a)
{
    const QString t = str(a, "type");
    auto signedNum = [](double v, const QString &unit) {
        return (v >= 0 ? QStringLiteral("+") : QString()) + QString::number(v) + unit;
    };
    if (t.isEmpty() || t == QLatin1String("none")) return QStringLiteral("Nothing");
    if (t == QLatin1String("key")) {
        auto r = parseKeySequence(str(a, "keys"));
        return QStringLiteral("Key %1").arg(r.ok() ? r.sequence.toString() : str(a, "keys"));
    }
    if (t == QLatin1String("text")) return QStringLiteral("Type text");
    if (t == QLatin1String("mouse")) return QStringLiteral("Mouse %1").arg(str(a, "button"));
    if (t == QLatin1String("scroll"))
        return QStringLiteral("Scroll %1 %2").arg(str(a, "axis").isEmpty() ? QStringLiteral("vertical") : str(a, "axis"),
                                                  signedNum(a.value(QLatin1String("amount")).toDouble(1), QString()));
    if (t == QLatin1String("zoom")) return QStringLiteral("Zoom %1").arg(str(a, "direction"));
    if (t == QLatin1String("volume"))
        return QStringLiteral("%1 %2").arg(str(a, "target") == QLatin1String("source") ? QStringLiteral("Mic volume") : QStringLiteral("Volume"),
                                           signedNum(a.value(QLatin1String("delta")).toDouble(), QStringLiteral("%")));
    if (t == QLatin1String("mute")) return str(a, "target") == QLatin1String("source") ? QStringLiteral("Mute mic") : QStringLiteral("Mute");
    if (t == QLatin1String("media")) return QStringLiteral("Media %1").arg(str(a, "command"));
    if (t == QLatin1String("brightness")) return QStringLiteral("Brightness %1").arg(signedNum(a.value(QLatin1String("delta")).toDouble(), QStringLiteral("%")));
    if (t == QLatin1String("workspace")) return QStringLiteral("Workspace %1").arg(str(a, "direction"));
    if (t == QLatin1String("tool")) return QStringLiteral("Tool %1 %2").arg(str(a, "op"), str(a, "id")).trimmed();
    if (t == QLatin1String("profile")) return QStringLiteral("Profile %1 %2").arg(str(a, "op"), str(a, "id")).trimmed();
    if (t == QLatin1String("menu")) return QStringLiteral("Menu %1").arg(str(a, "op"));
    if (t == QLatin1String("haptic")) return QStringLiteral("Haptic %1").arg(str(a, "waveform"));
    if (t == QLatin1String("value")) return QStringLiteral("Value %1 %2").arg(str(a, "variable"), signedNum(a.value(QLatin1String("step")).toDouble(), QString()));
    if (t == QLatin1String("osc")) return QStringLiteral("OSC %1").arg(str(a, "address"));
    if (t == QLatin1String("launch"))
        return QStringLiteral("Open %1").arg(!str(a, "desktopId").isEmpty() ? str(a, "desktopId") : !str(a, "url").isEmpty() ? str(a, "url") : str(a, "path"));
    if (t == QLatin1String("command")) {
        QStringList argv;
        for (const auto &v : a.value(QLatin1String("argv")).toArray())
            argv << v.toString();
        return QStringLiteral("Run %1").arg(argv.join(QLatin1Char(' ')).left(60));
    }
    if (t == QLatin1String("dbus")) return QStringLiteral("D-Bus %1.%2").arg(str(a, "interface"), str(a, "method"));
    if (t == QLatin1String("macro")) return QStringLiteral("Macro (%1 steps)").arg(a.value(QLatin1String("steps")).toArray().size());
    if (t == QLatin1String("if")) return QStringLiteral("Conditional");
    if (t == QLatin1String("plugin")) return QStringLiteral("Plugin %1").arg(str(a, "appId"));
    if (t == QLatin1String("osd")) return QStringLiteral("Show \"%1\"").arg(str(a, "text").left(30));
    return t;
}

void collectRiskyActions(const QJsonValue &tree, const QString &path, QList<QPair<QString, QJsonObject>> &out)
{
    if (tree.isObject()) {
        const auto o = tree.toObject();
        if (o.value(QLatin1String("type")).isString() && actionTypes().contains(o.value(QLatin1String("type")).toString())
            && actionRisk(o) == Risk::NeedsApproval) {
            out.append({path, o});
            return; // the whole (composite) action is approved as a unit
        }
        for (auto it = o.begin(); it != o.end(); ++it)
            collectRiskyActions(it.value(), path + QLatin1Char('.') + it.key(), out);
    } else if (tree.isArray()) {
        const auto a = tree.toArray();
        for (int i = 0; i < a.size(); ++i)
            collectRiskyActions(a.at(i), path + QStringLiteral("[%1]").arg(i), out);
    }
}

} // namespace opendial
