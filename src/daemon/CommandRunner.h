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
    QString key;         // same for every invocation of one configured command

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
// configured command (as written in the config, before {value} is filled in, and
// per approval that covers it); requests that arrive meanwhile follow the action's
// "overlap" policy:
//   coalesce  merge into a single further run of the latest request (default for rotation)
//   reject    refuse
//   queue     wait in line, at most kMaxWaiting (default for everything else)
class CommandRunner : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxWaiting = 4;

    struct Request {
        QJsonObject action;
        QJsonObject source; // the action before {value} was filled in
        QString approval;   // fingerprint of the approved action this belongs to
        QString slot;
        QString toolId;
        bool rotation = false;
    };
    struct Reply {
        bool ok = false;
        quint64 id = 0;     // invocation the request belongs to, 0 if refused
        QString message;
    };

    // isApproved is asked again before a waiting command starts: its approval
    // may have been withdrawn in the meantime.
    explicit CommandRunner(std::function<bool(const QString &approval)> isApproved, QObject *parent = nullptr);

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
    struct Lane { // per key
        Invocation running;
        QList<Invocation> waiting;
        bool timedOut = false;
    };

    void start(const QString &key, const Invocation &inv);
    void finish(const QString &key, CommandEvent::State s, int exitCode, const QString &detail);
    void report(const Invocation &inv, CommandEvent::State s, const QString &what, int exitCode = 0);

    std::function<bool(const QString &)> m_isApproved;
    QHash<QString, Lane> m_lanes; // a lane exists while its command runs
    quint64 m_lastId = 0;
};

} // namespace opendial
