// SPDX-License-Identifier: MIT
#pragma once

#include "Profiles.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QUdpSocket>

namespace opendial {

class Engine;
class InjectorManager;
class AudioController;
class MediaController;
class BrightnessController;
class WorkspaceController;

struct ActionContext {
    int steps = 1;          // magnitude of a rotation batch (>= 1); 1 for buttons
    int direction = 0;      // +1 cw, -1 ccw, 0 button
    QString slot;
    QString toolId;
    QString profileId;
    FocusContext focus;
    int depth = 0;
    bool fine = false;
};

struct ActionResult {
    bool ok = false;
    QString message;      // shown in diagnostics; never contains window titles
    bool needsApproval = false;
};

// Executes action JSON objects (see Action.h for the schema). Rotation steps
// are folded into a single backend call where the action allows it (scroll,
// volume, brightness, seek) so a fast spin is one injection, not forty.
class ActionRunner : public QObject {
    Q_OBJECT
public:
    ActionRunner(Engine *engine, InjectorManager *inj, AudioController *audio, MediaController *media,
                 BrightnessController *brightness, WorkspaceController *workspace, QObject *parent = nullptr);

    ActionResult run(const QJsonObject &action, const ActionContext &ctx);
    // Cancel pending macro steps (lock screen, secure focus, disconnect).
    void cancelPending() { ++m_generation; }

    // Replace {value} / {steps} / {direction} placeholders in an action tree.
    static QJsonValue substitute(const QJsonValue &v, const QHash<QString, QString> &vars);
    // OSC 1.0 message encoding (int32 / float32 / string / bool args).
    static QByteArray encodeOsc(const QString &address, const QJsonArray &args);
    // Evaluate an "if" condition against focus/variables.
    bool evaluate(const QJsonObject &cond, const ActionContext &ctx) const;

private:
    ActionResult runValue(const QJsonObject &a, const ActionContext &ctx);
    ActionResult runMacro(const QJsonObject &a, const ActionContext &ctx);
    ActionResult runRisky(const QJsonObject &a, const ActionContext &ctx);
    void scheduleMacro(QJsonArray steps, int index, int repeatLeft, ActionContext ctx, quint64 gen);

    Engine *m_engine;
    InjectorManager *m_inj;
    AudioController *m_audio;
    MediaController *m_media;
    BrightnessController *m_brightness;
    WorkspaceController *m_workspace;
    QUdpSocket m_udp;
    quint64 m_generation = 0;
};

} // namespace opendial
