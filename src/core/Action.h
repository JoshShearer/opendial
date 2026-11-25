// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace opendial {

// Gesture slots every tool can bind independently (SRS section 7).
namespace slot {
inline const QString RotateCw = QStringLiteral("rotate_cw");
inline const QString RotateCcw = QStringLiteral("rotate_ccw");
inline const QString Click = QStringLiteral("click");
inline const QString DoubleClick = QStringLiteral("double_click");
inline const QString TripleClick = QStringLiteral("triple_click");
inline const QString LongPress = QStringLiteral("long_press");
inline const QString PressedCw = QStringLiteral("rotate_while_pressed_cw");
inline const QString PressedCcw = QStringLiteral("rotate_while_pressed_ccw");
inline const QString Release = QStringLiteral("release");
QStringList all();
} // namespace slot

// Action kinds. Each is a JSON object {"type": "<kind>", ...params}.
//   none
//   key         {keys:"Ctrl+Z" | "Ctrl+K Ctrl+C", holdModifiers?:bool, releaseAfterMs?:int}
//   text        {text:"..."}
//   mouse       {button:"left|right|middle|back|forward"}
//   scroll      {axis:"vertical|horizontal", amount:number(clicks per step), smooth?:bool}
//   zoom        {direction:"in|out", method?:"ctrlScroll|keys"}
//   volume      {delta:number(% per step), target?:"sink|source", max?:number}
//   mute        {target?:"sink|source"}
//   media       {command:"playpause|play|pause|stop|next|previous|seek|rate", offsetMs?:int, rateDelta?:number}
//   brightness  {delta:number(% per step)}
//   workspace   {direction:"next|previous"}
//   tool        {op:"next|previous|select", id?}
//   profile     {op:"cycle|lock|unlock", id?}
//   menu        {op:"open|close|toggle"}
//   haptic      {waveform:"click|buzz|rumble", repeat?:int}
//   value       {variable, step, min, max, default, snap?, wrap?, output?:action with {value} placeholders}
//   osc         {host, port, address, args:[...]}  (dangerous unless loopback)
//   launch      {desktopId?|url?|path?}             (dangerous)
//   command     {argv:[...], timeoutMs?, overlap?:"coalesce|reject|queue"} (dangerous)
//               overlap = what to do while it still runs; default: coalesce on rotation, else queue
//   dbus        {bus?, service, path, interface, method, args?} (dangerous)
//   macro       {steps:[{action:{...}}|{delayMs:n}], repeat?:int}
//   if          {condition:{appId?:[..], variable?, op?, value?}, then:{..}, else?:{..}}
//   plugin      {appId, toolId}                      (routed to registered plugin)
//   osd         {text}

struct ValidationIssue {
    QString path;
    QString message;
    bool warning = false;
};
using ValidationIssues = QList<ValidationIssue>;

QStringList actionTypes();
void validateAction(const QJsonObject &action, const QString &path, ValidationIssues &issues, int depth = 0);

enum class Risk { Safe, NeedsApproval };
// Actions that run programs, open things, talk to other D-Bus services or the
// network require explicit user approval (SRS section 12).
Risk actionRisk(const QJsonObject &action);
// Stable SHA-256 fingerprint of an action's canonical JSON (approval key).
QString actionFingerprint(const QJsonObject &action);
// argv of a "command" action. An entry that is just "{value}" is a number once
// substituted.
QStringList commandArgv(const QJsonObject &action);
// Human summary for UI, e.g. "Key Ctrl+Z", "Volume +2%".
QString describeAction(const QJsonObject &action);
// What an approval covers: every risky action inside, command lines in full.
QString describeForApproval(const QJsonObject &action);
// Collect every action needing approval within a tool/profile/config tree.
void collectRiskyActions(const QJsonValue &tree, const QString &path, QList<QPair<QString, QJsonObject>> &out);

constexpr int kMaxActionDepth = 4;
constexpr int kMaxMacroSteps = 64;
constexpr int kMaxMacroRepeat = 20;
constexpr int kMaxMacroTotalDelayMs = 30000;

} // namespace opendial
