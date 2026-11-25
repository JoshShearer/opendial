// SPDX-License-Identifier: MIT
#include "CommandRunner.h"

#include "Action.h"

#include <QJsonArray>
#include <QLoggingCategory>
#include <QProcess>
#include <QTimer>
#include <utility>

Q_LOGGING_CATEGORY(lcCommand, "opendial.command")

namespace opendial {

namespace {
QStringList argvOf(const QJsonObject &action)
{
    QStringList argv;
    for (const auto &v : action.value(QStringLiteral("argv")).toArray())
        argv << v.toString();
    return argv;
}
QString label(quint64 id, const QJsonObject &action)
{
    return QStringLiteral("command #%1 (%2)").arg(QString::number(id), argvOf(action).value(0));
}
} // namespace

QString commandStateName(CommandEvent::State s)
{
    switch (s) {
    case CommandEvent::State::Queued: return QStringLiteral("queued");
    case CommandEvent::State::Started: return QStringLiteral("started");
    case CommandEvent::State::FailedToStart: return QStringLiteral("failed-to-start");
    case CommandEvent::State::Finished: return QStringLiteral("finished");
    case CommandEvent::State::Crashed: return QStringLiteral("crashed");
    case CommandEvent::State::TimedOut: return QStringLiteral("timed-out");
    case CommandEvent::State::Cancelled: return QStringLiteral("cancelled");
    }
    return {};
}

CommandRunner::CommandRunner(std::function<bool(const QJsonObject &)> mayStart, QObject *parent)
    : QObject(parent), m_mayStart(std::move(mayStart))
{
}

CommandRunner::Reply CommandRunner::submit(const Request &r)
{
    if (argvOf(r.action).value(0).isEmpty())
        return {false, 0, QStringLiteral("empty argv")};
    const QString key = actionFingerprint(r.action);
    auto lane = m_lanes.find(key);
    if (lane == m_lanes.end()) {
        const Invocation inv{++m_lastId, r};
        start(key, inv);
        if (!m_lanes.contains(key)) // QProcess can fail before start() returns
            return {false, inv.id, label(inv.id, r.action) + QStringLiteral(" failed to start")};
        return {true, inv.id, label(inv.id, r.action) + QStringLiteral(" requested")};
    }

    const QString busy = label(lane->running.id, r.action) + QStringLiteral(" is already running");
    const QString policy = r.action.value(QStringLiteral("overlap")).toString(r.rotation ? QStringLiteral("coalesce") : QStringLiteral("queue"));
    if (policy == QLatin1String("reject"))
        return {false, 0, busy};
    if (policy == QLatin1String("coalesce") && !lane->waiting.isEmpty()) {
        const quint64 id = lane->waiting.constLast().id;
        return {true, id, busy + QStringLiteral(", merged into #%1").arg(id)};
    }
    if (lane->waiting.size() >= kMaxWaiting)
        return {false, 0, busy + QStringLiteral(" and its queue is full")};
    const Invocation inv{++m_lastId, r};
    lane->waiting.append(inv);
    report(inv, CommandEvent::State::Queued, QStringLiteral("queued"));
    return {true, inv.id, busy + QStringLiteral(", #%1 waits").arg(inv.id)};
}

void CommandRunner::cancelWaiting()
{
    QList<Invocation> dropped;
    for (auto &lane : m_lanes)
        dropped += std::exchange(lane.waiting, {});
    for (const auto &inv : std::as_const(dropped))
        report(inv, CommandEvent::State::Cancelled, QStringLiteral("cancelled before it started"));
}

void CommandRunner::start(const QString &key, const Invocation &inv)
{
    Lane &lane = m_lanes[key];
    lane.running = inv;
    lane.timedOut = false;

    const QStringList argv = argvOf(inv.req.action);
    auto *p = new QProcess(this);
    p->setProgram(argv.first());
    p->setArguments(argv.mid(1));
    p->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    p->setStandardOutputFile(QProcess::nullDevice());

    connect(p, &QProcess::started, this, [this, inv] { report(inv, CommandEvent::State::Started, QStringLiteral("started")); });
    connect(p, &QProcess::finished, this, [this, key, p](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        if (m_lanes.value(key).timedOut)
            finish(key, CommandEvent::State::TimedOut, 0, QStringLiteral("timed out and was stopped"));
        else if (status == QProcess::CrashExit)
            finish(key, CommandEvent::State::Crashed, 0, QStringLiteral("was killed by a signal"));
        else
            finish(key, CommandEvent::State::Finished, code, QStringLiteral("finished with exit code %1").arg(code));
    });
    connect(p, &QProcess::errorOccurred, this, [this, key, p](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) // everything else is followed by finished()
            return;
        p->deleteLater();
        finish(key, CommandEvent::State::FailedToStart, 0, QStringLiteral("failed to start: ") + p->errorString());
    });

    const int timeout = qBound(100, inv.req.action.value(QStringLiteral("timeoutMs")).toInt(10000), 600000);
    QTimer::singleShot(timeout, p, [this, key, p] {
        if (p->state() == QProcess::NotRunning)
            return;
        m_lanes[key].timedOut = true;
        p->terminate();
        QTimer::singleShot(2000, p, [p] { p->kill(); });
    });
    p->start();
}

void CommandRunner::finish(const QString &key, CommandEvent::State s, int exitCode, const QString &detail)
{
    const Lane lane = m_lanes.take(key);
    report(lane.running, s, detail, exitCode);

    QList<Invocation> waiting = lane.waiting;
    while (!waiting.isEmpty()) {
        const Invocation next = waiting.takeFirst();
        if (!m_mayStart(next.req.action)) {
            report(next, CommandEvent::State::Cancelled, QStringLiteral("cancelled: no longer approved"));
            continue;
        }
        m_lanes[key].waiting = waiting;
        start(key, next); // the rest continues from its finish()
        return;
    }
}

void CommandRunner::report(const Invocation &inv, CommandEvent::State s, const QString &what, int exitCode)
{
    CommandEvent e;
    e.id = inv.id;
    e.state = s;
    e.exitCode = exitCode;
    e.message = label(inv.id, inv.req.action) + QLatin1Char(' ') + what;
    e.action = inv.req.action;
    e.slot = inv.req.slot;
    e.toolId = inv.req.toolId;
    if (e.failed())
        qCWarning(lcCommand).noquote() << e.message;
    else
        qCDebug(lcCommand).noquote() << e.message;
    Q_EMIT commandEvent(e);
}

} // namespace opendial
