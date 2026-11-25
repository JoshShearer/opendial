// SPDX-License-Identifier: MIT
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <functional>

class QProcess;

namespace opendial {

// What happened to one command invocation. Every invocation ends in exactly one
// of FailedToStart, Finished, Crashed, TimedOut or Cancelled.
struct CommandEvent {
    enum class State { Queued, Started, FailedToStart, Finished, Crashed, TimedOut, Cancelled };
    quint64 id = 0;
    State state = State::Queued;
    int exitCode = 0;    // Finished only
    QString message;     // may contain the program path: redact like an action
    QJsonObject action;
    QString slot;
    QString toolId;

    // The command ran into trouble (as opposed to being dropped on purpose).
    bool failed() const
    {
        return state == State::FailedToStart || state == State::Crashed || state == State::TimedOut
            || (state == State::Finished && exitCode != 0);
    }
    bool ok() const { return !failed() && state != State::Cancelled; }
};
QString commandStateName(CommandEvent::State s);

// Owns the processes of "command" actions. One process runs at a time per
// action fingerprint; requests that arrive meanwhile follow the action's
// "overlap" policy:
//   coalesce  merge into a single further run (default for rotation)
//   reject    refuse
//   queue     wait in line, at most kMaxWaiting (default for everything else)
class CommandRunner : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxWaiting = 4;

    struct Request {
        QJsonObject action;
        QString slot;
        QString toolId;
        bool rotation = false;
    };
    struct Reply {
        bool ok = false;
        quint64 id = 0;     // invocation the request belongs to, 0 if refused
        QString message;
    };

    // mayStart is asked again before a waiting command starts: its approval may
    // have been withdrawn in the meantime.
    explicit CommandRunner(std::function<bool(const QJsonObject &action)> mayStart, QObject *parent = nullptr);

    Reply submit(const Request &r);
    // Drop waiting commands. A running one is left alone.
    void cancelWaiting();

Q_SIGNALS:
    void commandEvent(const opendial::CommandEvent &e);

private:
    struct Invocation {
        quint64 id = 0;
        Request req;
    };
    struct Lane { // per action fingerprint
        Invocation running;
        QList<Invocation> waiting;
        bool timedOut = false;
    };

    void start(const QString &key, const Invocation &inv);
    void finish(const QString &key, CommandEvent::State s, int exitCode, const QString &detail);
    void report(const Invocation &inv, CommandEvent::State s, const QString &what, int exitCode = 0);

    std::function<bool(const QJsonObject &)> m_mayStart;
    QHash<QString, Lane> m_lanes; // a lane exists while its command runs
    quint64 m_lastId = 0;
};

} // namespace opendial
