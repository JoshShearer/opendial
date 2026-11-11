// SPDX-License-Identifier: MIT
#include "ActionRunner.h"
#include "Action.h"
#include "Engine.h"
#include "InjectorManager.h"
#include "Integrations.h"
#include "KeyChord.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDesktopServices>
#include <QHostAddress>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtEndian>
#include <cmath>
#include <cstring>

Q_LOGGING_CATEGORY(lcAction, "opendial.action")

namespace opendial {

namespace {
QString str(const QJsonObject &a, const char *k, const QString &def = {}) { return a.value(QLatin1String(k)).toString(def); }
double num(const QJsonObject &a, const char *k, double def = 0) { return a.value(QLatin1String(k)).toDouble(def); }

ActionResult okr(const QString &m = {}) { return ActionResult{true, m, false}; }
ActionResult fail(const QString &m) { return ActionResult{false, m, false}; }

int mouseButton(const QString &name)
{
    if (name == QLatin1String("right")) return btn::Right;
    if (name == QLatin1String("middle")) return btn::Middle;
    if (name == QLatin1String("back")) return btn::Side;
    if (name == QLatin1String("forward")) return btn::Extra;
    return btn::Left;
}

QVariant jsonToDBus(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Bool: return v.toBool();
    case QJsonValue::Double: {
        const double d = v.toDouble();
        if (std::floor(d) == d && std::abs(d) < 2147483647.0)
            return int(d);
        return d;
    }
    case QJsonValue::String: return v.toString();
    default: return QVariant();
    }
}
} // namespace

ActionRunner::ActionRunner(Engine *engine, InjectorManager *inj, AudioController *audio, MediaController *media,
                           BrightnessController *brightness, WorkspaceController *workspace, QObject *parent)
    : QObject(parent), m_engine(engine), m_inj(inj), m_audio(audio), m_media(media), m_brightness(brightness), m_workspace(workspace)
{
}

QJsonValue ActionRunner::substitute(const QJsonValue &v, const QHash<QString, QString> &vars)
{
    if (v.isString()) {
        QString s = v.toString();
        // whole-string numeric placeholders keep their numeric type
        for (auto it = vars.cbegin(); it != vars.cend(); ++it) {
            const QString ph = QLatin1Char('{') + it.key() + QLatin1Char('}');
            if (s == ph) {
                bool isNum = false;
                const double d = it.value().toDouble(&isNum);
                return isNum ? QJsonValue(d) : QJsonValue(it.value());
            }
            s.replace(ph, it.value());
        }
        return s;
    }
    if (v.isArray()) {
        QJsonArray out;
        for (const auto &e : v.toArray())
            out.append(substitute(e, vars));
        return out;
    }
    if (v.isObject()) {
        QJsonObject out;
        const auto o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            out.insert(it.key(), substitute(it.value(), vars));
        return out;
    }
    return v;
}

QByteArray ActionRunner::encodeOsc(const QString &address, const QJsonArray &args)
{
    auto pad = [](QByteArray b) {
        b.append('\0');
        while (b.size() % 4)
            b.append('\0');
        return b;
    };
    QByteArray tags(",");
    QByteArray payload;
    for (const auto &a : args) {
        if (a.isBool()) {
            tags.append(a.toBool() ? 'T' : 'F');
        } else if (a.isDouble()) {
            const double d = a.toDouble();
            if (std::floor(d) == d && std::abs(d) < 2147483647.0) {
                tags.append('i');
                char buf[4];
                qToBigEndian<qint32>(qint32(d), buf);
                payload.append(buf, 4);
            } else {
                tags.append('f');
                float f = float(d);
                quint32 bits;
                std::memcpy(&bits, &f, 4);
                char buf[4];
                qToBigEndian<quint32>(bits, buf);
                payload.append(buf, 4);
            }
        } else {
            tags.append('s');
            payload.append(pad(a.toString().toUtf8()));
        }
    }
    return pad(address.toUtf8()) + pad(tags) + payload;
}

bool ActionRunner::evaluate(const QJsonObject &cond, const ActionContext &ctx) const
{
    if (cond.contains(QStringLiteral("appId"))) {
        const auto cands = appIdCandidates(ctx.focus);
        bool any = false;
        for (const auto &v : cond.value(QStringLiteral("appId")).toArray())
            if (cands.contains(normalizeAppId(v.toString())))
                any = true;
        if (!any)
            return false;
    }
    if (cond.contains(QStringLiteral("variable"))) {
        const double cur = m_engine->variable(cond.value(QStringLiteral("variable")).toString()).toDouble();
        const double val = cond.value(QStringLiteral("value")).toDouble();
        const QString op = cond.value(QStringLiteral("op")).toString(QStringLiteral("=="));
        if (op == QLatin1String("==")) return qFuzzyCompare(cur + 1, val + 1);
        if (op == QLatin1String("!=")) return !qFuzzyCompare(cur + 1, val + 1);
        if (op == QLatin1String("<")) return cur < val;
        if (op == QLatin1String("<=")) return cur <= val;
        if (op == QLatin1String(">")) return cur > val;
        if (op == QLatin1String(">=")) return cur >= val;
        return false;
    }
    if (cond.contains(QStringLiteral("direction")))
        return (cond.value(QStringLiteral("direction")).toString() == QLatin1String("cw")) == (ctx.direction > 0);
    return true;
}

ActionResult ActionRunner::run(const QJsonObject &a, const ActionContext &ctx)
{
    if (ctx.depth > kMaxActionDepth)
        return fail(QStringLiteral("nesting limit reached"));
    const QString type = str(a, "type", QStringLiteral("none"));
    const int steps = qMax(1, ctx.steps);

    if (actionRisk(a) == Risk::NeedsApproval)
        return runRisky(a, ctx);

    if (type == QLatin1String("none"))
        return okr();

    if (type == QLatin1String("key")) {
        auto parsed = parseKeySequence(str(a, "keys"));
        if (!parsed.ok())
            return fail(parsed.error);
        bool ok;
        if (a.value(QStringLiteral("holdModifiers")).toBool() && parsed.sequence.chords.size() == 1)
            ok = m_inj->tapHeld(parsed.sequence.chords.first(), steps, a.value(QStringLiteral("releaseAfterMs")).toInt(800));
        else
            ok = m_inj->tapSequence(parsed.sequence, steps);
        return ok ? okr() : fail(QStringLiteral("no keyboard injection backend (") + m_inj->keyboardCapability() + QLatin1Char(')'));
    }
    if (type == QLatin1String("text"))
        return m_inj->typeText(str(a, "text")) ? okr() : fail(QStringLiteral("text injection unavailable"));
    if (type == QLatin1String("mouse")) {
        bool ok = true;
        for (int i = 0; i < qMin(steps, 10); ++i)
            ok &= m_inj->click(mouseButton(str(a, "button", QStringLiteral("left"))));
        return ok ? okr() : fail(QStringLiteral("pointer injection unavailable"));
    }
    if (type == QLatin1String("scroll")) {
        const auto o = str(a, "axis") == QLatin1String("horizontal") ? Qt::Horizontal : Qt::Vertical;
        double amount = num(a, "amount", 1) * steps;
        if (ctx.fine)
            amount *= 0.5;
        return m_inj->scroll(o, amount, a.value(QStringLiteral("smooth")).toBool(ctx.fine)) ? okr() : fail(QStringLiteral("scroll injection unavailable"));
    }
    if (type == QLatin1String("zoom")) {
        const QString dir = str(a, "direction", QStringLiteral("in"));
        const QString method = str(a, "method", QStringLiteral("keys"));
        if (dir == QLatin1String("reset")) {
            auto p = parseKeySequence(QStringLiteral("Ctrl+0"));
            return m_inj->tapSequence(p.sequence) ? okr() : fail(QStringLiteral("keyboard injection unavailable"));
        }
        const int sign = dir == QLatin1String("out") ? -1 : 1;
        if (method == QLatin1String("ctrlScroll"))
            return m_inj->ctrlScroll(sign * steps) ? okr() : fail(QStringLiteral("pointer injection unavailable"));
        // Ctrl+plus is "Ctrl+equal" on most layouts; apps accept both.
        auto p = parseKeySequence(sign > 0 ? QStringLiteral("Ctrl+plus") : QStringLiteral("Ctrl+minus"));
        return m_inj->tapSequence(p.sequence, steps) ? okr() : fail(QStringLiteral("keyboard injection unavailable"));
    }
    if (type == QLatin1String("volume")) {
        if (m_audio->state() != CapState::Supported) {
            // XF86 keys work wherever the compositor handles them
            auto p = parseKeySequence(num(a, "delta", 2) > 0 ? QStringLiteral("XF86AudioRaiseVolume") : QStringLiteral("XF86AudioLowerVolume"));
            return m_inj->tapSequence(p.sequence, steps) ? okr(QStringLiteral("media keys")) : fail(QStringLiteral("no audio backend (install wireplumber or pulseaudio-utils)"));
        }
        const bool source = str(a, "target") == QLatin1String("source");
        m_audio->adjust(source, num(a, "delta", 2) * steps, num(a, "max", 100));
        return okr();
    }
    if (type == QLatin1String("mute")) {
        const bool source = str(a, "target") == QLatin1String("source");
        if (m_audio->state() != CapState::Supported) {
            auto p = parseKeySequence(source ? QStringLiteral("XF86AudioMicMute") : QStringLiteral("XF86AudioMute"));
            return m_inj->tapSequence(p.sequence) ? okr(QStringLiteral("media keys")) : fail(QStringLiteral("no audio backend"));
        }
        m_audio->toggleMute(source);
        return okr();
    }
    if (type == QLatin1String("media")) {
        const QString cmd = str(a, "command", QStringLiteral("playpause"));
        bool ok = false;
        if (cmd == QLatin1String("seek"))
            ok = m_media->command(cmd, qint64(num(a, "offsetMs", 5000)) * steps);
        else if (cmd == QLatin1String("rate") || cmd == QLatin1String("volume"))
            ok = m_media->command(cmd, 0, num(a, "rateDelta", 0.1) * steps);
        else
            ok = m_media->command(cmd); // one track change per detent batch
        if (ok)
            return okr();
        // No MPRIS player: fall back to media keys handled by the compositor.
        static const QHash<QString, QString> keys{{QStringLiteral("playpause"), QStringLiteral("XF86AudioPlay")},
                                                  {QStringLiteral("play"), QStringLiteral("XF86AudioPlay")},
                                                  {QStringLiteral("pause"), QStringLiteral("XF86AudioPause")},
                                                  {QStringLiteral("stop"), QStringLiteral("XF86AudioStop")},
                                                  {QStringLiteral("next"), QStringLiteral("XF86AudioNext")},
                                                  {QStringLiteral("previous"), QStringLiteral("XF86AudioPrev")}};
        if (keys.contains(cmd)) {
            auto p = parseKeySequence(keys.value(cmd));
            if (m_inj->tapSequence(p.sequence))
                return okr(QStringLiteral("media keys"));
        }
        return fail(QStringLiteral("no MPRIS player"));
    }
    if (type == QLatin1String("brightness")) {
        if (m_brightness->state() == CapState::NotSupported) {
            auto p = parseKeySequence(num(a, "delta", 5) > 0 ? QStringLiteral("XF86MonBrightnessUp") : QStringLiteral("XF86MonBrightnessDown"));
            return m_inj->tapSequence(p.sequence, steps) ? okr(QStringLiteral("brightness keys")) : fail(QStringLiteral("no backlight control"));
        }
        if (!m_brightness->adjust(num(a, "delta", 5) * steps))
            return fail(QStringLiteral("brightness change failed"));
        if (auto pc = m_brightness->percent())
            m_engine->showOsd(QJsonObject{{QStringLiteral("icon"), QStringLiteral("brightness")},
                                          {QStringLiteral("title"), tr("Brightness")},
                                          {QStringLiteral("value"), qBound(0.0, *pc + num(a, "delta", 5) * steps, 100.0) / 100.0}});
        return okr();
    }
    if (type == QLatin1String("workspace")) {
        const int dir = str(a, "direction", QStringLiteral("next")) == QLatin1String("previous") ? -1 : 1;
        bool ok = true;
        for (int i = 0; i < qMin(steps, 10); ++i)
            ok &= m_workspace->step(dir);
        return ok ? okr(m_workspace->method()) : fail(QStringLiteral("workspace switching unavailable"));
    }
    if (type == QLatin1String("tool")) {
        m_engine->toolOp(str(a, "op", QStringLiteral("next")), str(a, "id"), ctx.direction ? steps : 1);
        return okr();
    }
    if (type == QLatin1String("profile")) {
        m_engine->profileOp(str(a, "op", QStringLiteral("cycle")), str(a, "id"));
        return okr();
    }
    if (type == QLatin1String("menu")) {
        m_engine->menuOp(str(a, "op", QStringLiteral("toggle")));
        return okr();
    }
    if (type == QLatin1String("haptic")) {
        const QString w = str(a, "waveform", QStringLiteral("click"));
        m_engine->haptic(w == QLatin1String("buzz") ? hid::Waveform::BuzzContinuous
                         : w == QLatin1String("rumble") ? hid::Waveform::RumbleContinuous
                                                         : hid::Waveform::Click,
                         a.value(QStringLiteral("repeat")).toInt(1));
        return okr();
    }
    if (type == QLatin1String("osd")) {
        m_engine->showOsd(QJsonObject{{QStringLiteral("title"), str(a, "text")}, {QStringLiteral("icon"), str(a, "icon", QStringLiteral("info"))}});
        return okr();
    }
    if (type == QLatin1String("value"))
        return runValue(a, ctx);
    if (type == QLatin1String("macro"))
        return runMacro(a, ctx);
    if (type == QLatin1String("if")) {
        const bool c = evaluate(a.value(QStringLiteral("condition")).toObject(), ctx);
        const QJsonObject branch = a.value(c ? QStringLiteral("then") : QStringLiteral("else")).toObject();
        if (branch.isEmpty())
            return okr();
        ActionContext sub = ctx;
        sub.depth++;
        return run(branch, sub);
    }
    if (type == QLatin1String("plugin")) {
        QJsonObject ev{{QStringLiteral("toolId"), str(a, "toolId", ctx.toolId)},
                       {QStringLiteral("slot"), ctx.slot},
                       {QStringLiteral("steps"), ctx.direction * steps},
                       {QStringLiteral("event"), str(a, "event", ctx.slot)}};
        if (a.contains(QStringLiteral("data")))
            ev.insert(QStringLiteral("data"), a.value(QStringLiteral("data")));
        return m_engine->emitPluginEvent(str(a, "appId"), ev) ? okr() : fail(QStringLiteral("plugin not registered or not approved"));
    }
    return fail(QStringLiteral("unknown action type ") + type);
}

ActionResult ActionRunner::runValue(const QJsonObject &a, const ActionContext &ctx)
{
    const QString var = str(a, "variable");
    if (var.isEmpty())
        return fail(QStringLiteral("value: variable missing"));
    const double mn = num(a, "min", 0), mx = num(a, "max", 100), stepSize = num(a, "step", 1);
    double cur = m_engine->variable(var).toDouble(num(a, "default", mn));
    if (ctx.slot == slot::Click && a.contains(QStringLiteral("resetOnClick")))
        cur = num(a, "default", mn);
    else
        cur += stepSize * ctx.steps * (ctx.direction == 0 ? 1 : ctx.direction);
    const double range = mx - mn;
    if (a.value(QStringLiteral("wrap")).toBool() && range > 0) {
        while (cur > mx) cur -= range;
        while (cur < mn) cur += range;
    } else {
        cur = qBound(mn, cur, mx);
    }
    if (a.contains(QStringLiteral("snap"))) {
        const double s = num(a, "snap", 0);
        if (s > 0)
            cur = mn + std::round((cur - mn) / s) * s;
    }
    m_engine->setVariable(var, cur);
    m_engine->showOsd(QJsonObject{{QStringLiteral("icon"), str(a, "icon", QStringLiteral("value"))},
                                  {QStringLiteral("title"), str(a, "label", var)},
                                  {QStringLiteral("text"), QString::number(cur, 'g', 6)},
                                  {QStringLiteral("value"), range > 0 ? (cur - mn) / range : 0.0}});
    const QJsonObject out = a.value(QStringLiteral("output")).toObject();
    if (out.isEmpty())
        return okr(QString::number(cur));
    const QJsonObject resolved = substitute(out, {{QStringLiteral("value"), QString::number(cur, 'g', 10)},
                                                  {QStringLiteral("normalized"), QString::number(range > 0 ? (cur - mn) / range : 0.0, 'g', 6)}})
                                     .toObject();
    ActionContext sub = ctx;
    sub.depth++;
    sub.steps = 1;
    return run(resolved, sub);
}

ActionResult ActionRunner::runMacro(const QJsonObject &a, const ActionContext &ctx)
{
    const QJsonArray steps = a.value(QStringLiteral("steps")).toArray();
    if (steps.size() > kMaxMacroSteps)
        return fail(QStringLiteral("macro too long"));
    const int repeat = qBound(1, a.value(QStringLiteral("repeat")).toInt(1), kMaxMacroRepeat);
    ActionContext sub = ctx;
    sub.depth++;
    sub.steps = 1;
    scheduleMacro(steps, 0, repeat, sub, m_generation);
    return okr(QStringLiteral("macro started"));
}

void ActionRunner::scheduleMacro(QJsonArray steps, int index, int repeatLeft, ActionContext ctx, quint64 gen)
{
    // Runs synchronously until the next delay, then continues from a timer.
    while (true) {
        if (gen != m_generation)
            return; // cancelled
        if (index >= steps.size()) {
            if (--repeatLeft <= 0)
                return;
            index = 0;
        }
        const QJsonObject step = steps.at(index++).toObject();
        if (step.contains(QStringLiteral("delayMs"))) {
            const int ms = qBound(0, step.value(QStringLiteral("delayMs")).toInt(), 10000);
            QPointer<ActionRunner> self(this);
            QTimer::singleShot(ms, this, [self, steps, index, repeatLeft, ctx, gen] {
                if (self)
                    self->scheduleMacro(steps, index, repeatLeft, ctx, gen);
            });
            return;
        }
        const ActionResult r = run(step.value(QStringLiteral("action")).toObject(), ctx);
        if (!r.ok)
            qCInfo(lcAction) << "macro step failed:" << r.message;
    }
}

ActionResult ActionRunner::runRisky(const QJsonObject &a, const ActionContext &ctx)
{
    const QString fp = actionFingerprint(a);
    if (!m_engine->isApproved(fp)) {
        m_engine->requestApproval(a, ctx.toolId, ctx.profileId);
        return ActionResult{false, QStringLiteral("needs approval: ") + describeAction(a), true};
    }
    const QString type = str(a, "type");
    if (type == QLatin1String("command")) {
        QStringList argv;
        for (const auto &v : a.value(QStringLiteral("argv")).toArray())
            argv << v.toString();
        if (argv.isEmpty())
            return fail(QStringLiteral("empty argv"));
        auto *p = new QProcess(this);
        p->setProgram(argv.first());
        p->setArguments(argv.mid(1));
        p->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        p->setStandardOutputFile(QProcess::nullDevice());
        const int timeout = qBound(100, a.value(QStringLiteral("timeoutMs")).toInt(10000), 600000);
        QTimer::singleShot(timeout, p, [p] {
            if (p->state() != QProcess::NotRunning) {
                qCWarning(lcAction) << "command timed out, terminating" << p->program();
                p->terminate();
                QTimer::singleShot(2000, p, [p] { p->kill(); });
            }
        });
        connect(p, &QProcess::finished, p, &QObject::deleteLater);
        connect(p, &QProcess::errorOccurred, p, [p](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart)
                p->deleteLater();
        });
        p->start();
        return okr(QStringLiteral("started ") + argv.first());
    }
    if (type == QLatin1String("launch")) {
        if (a.contains(QStringLiteral("desktopId"))) {
            const QString id = str(a, "desktopId").remove(QStringLiteral(".desktop"));
            for (const char *tool : {"gtk-launch", "gio", "kioclient"}) {
                const QString exe = QStandardPaths::findExecutable(QString::fromLatin1(tool));
                if (exe.isEmpty())
                    continue;
                if (QLatin1String(tool) == QLatin1String("gtk-launch"))
                    return QProcess::startDetached(exe, {id}) ? okr() : fail(QStringLiteral("launch failed"));
                if (QLatin1String(tool) == QLatin1String("gio")) {
                    const QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, id + QStringLiteral(".desktop"));
                    if (!path.isEmpty())
                        return QProcess::startDetached(exe, {QStringLiteral("launch"), path}) ? okr() : fail(QStringLiteral("launch failed"));
                }
                if (QLatin1String(tool) == QLatin1String("kioclient"))
                    return QProcess::startDetached(exe, {QStringLiteral("exec"), id + QStringLiteral(".desktop")}) ? okr() : fail(QStringLiteral("launch failed"));
            }
            return fail(QStringLiteral("no launcher (gtk-launch/gio) available"));
        }
        const QString target = a.contains(QStringLiteral("url")) ? str(a, "url") : str(a, "path");
        const QUrl url = a.contains(QStringLiteral("url")) ? QUrl(target) : QUrl::fromLocalFile(target);
        return QDesktopServices::openUrl(url) ? okr() : fail(QStringLiteral("open failed"));
    }
    if (type == QLatin1String("dbus")) {
        QDBusMessage m = QDBusMessage::createMethodCall(str(a, "service"), str(a, "path"), str(a, "interface"), str(a, "method"));
        QVariantList args;
        for (const auto &v : a.value(QStringLiteral("args")).toArray())
            args << jsonToDBus(v);
        m.setArguments(args);
        auto bus = str(a, "bus") == QLatin1String("system") ? QDBusConnection::systemBus() : QDBusConnection::sessionBus();
        bus.asyncCall(m);
        return okr();
    }
    if (type == QLatin1String("osc")) {
        const QHostAddress host(str(a, "host", QStringLiteral("127.0.0.1")));
        if (host.isNull())
            return fail(QStringLiteral("osc: host must be a numeric address"));
        const QByteArray pkt = encodeOsc(str(a, "address"), a.value(QStringLiteral("args")).toArray());
        const qint64 n = m_udp.writeDatagram(pkt, host, quint16(a.value(QStringLiteral("port")).toInt(9000)));
        return n == pkt.size() ? okr() : fail(QStringLiteral("osc send failed"));
    }
    return fail(QStringLiteral("unhandled risky action ") + type);
}

} // namespace opendial
