// SPDX-License-Identifier: MIT
// opendialctl: command-line control and diagnostics for OpenDial.
#include "Config.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnectionInterface>
#include <QFile>
#include <QFileInfo>
#include <functional>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>
#include <QTextStream>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <sys/signalfd.h>
#include <unistd.h>

using namespace opendial;

namespace {

enum Exit { Ok = 0, Failed = 1, Usage = 2, NoDaemon = 3 };

QTextStream out(stdout);
QTextStream err(stderr);
bool g_json = false;
std::nullptr_t g_none;

const char *kUsage = R"(Usage: opendialctl [--json] <command> [args]

Status
  status                       device, profile, tool, capabilities
  diagnose [--export FILE] [--no-redact]
                               full diagnostic report (redacted by default)
  devices                      Surface Dial input/hidraw nodes and permissions
  watch [--raw] [--seconds N]  stream gesture (and raw evdev) events as JSON lines
  record FILE [--seconds N]    record raw events to a replay fixture

Tools and profiles
  tools                        tools in the current menu
  tool ID                      make ID the active tool
  profiles                     list profiles
  profile lock [ID] | unlock   pin a profile regardless of the focused app
  menu open|close              show or hide the radial menu

Configuration
  config path | get | validate [FILE] | set FILE | reset | rollback
  import FILE                  import a profile (.json), risky actions stay unapproved
  export ID [FILE]             export a profile
  approvals                    list actions that need approval
  approve FINGERPRINT | revoke FINGERPRINT
  plugins                      registered integrations
  plugin allow|deny APPID

Device
  test haptics [click|buzz|rumble]
  bluetooth status|discover|stop-discovery|pair|connect|disconnect|trust|remove [PATH]
  reconnect                    reopen the device
  portal                       ask for the RemoteDesktop input permission again
  pause | resume

Simulation (no hardware needed)
  simulate click [N] | press | release | longpress | rotate DEGREES | json '{...}'
  context APPID [TITLE]        pretend APPID is focused (profile testing)
  context clear
)";

QDBusInterface *iface()
{
    static QDBusInterface *i = new QDBusInterface(QStringLiteral("io.github.opendial"), QStringLiteral("/io/github/opendial/Daemon"),
                                                  QStringLiteral("io.github.opendial.Daemon1"), QDBusConnection::sessionBus());
    return i;
}

bool daemonRunning()
{
    auto *bus = QDBusConnection::sessionBus().interface();
    return bus && bus->isServiceRegistered(QStringLiteral("io.github.opendial")).value();
}

QJsonValue parse(const QString &s)
{
    const auto d = QJsonDocument::fromJson(s.toUtf8());
    if (d.isObject())
        return d.object();
    if (d.isArray())
        return d.array();
    return QJsonValue(s);
}

void printJson(const QJsonValue &v)
{
    if (v.isObject())
        out << QJsonDocument(v.toObject()).toJson(QJsonDocument::Indented);
    else if (v.isArray())
        out << QJsonDocument(v.toArray()).toJson(QJsonDocument::Indented);
    else
        out << v.toVariant().toString() << '\n';
    out.flush();
}

template <typename T>
bool call(T &result, const char *method, const QVariantList &args = {})
{
    QDBusMessage m = iface()->callWithArgumentList(QDBus::Block, QString::fromLatin1(method), args);
    if (m.type() == QDBusMessage::ErrorMessage) {
        err << "opendialctl: " << m.errorMessage() << '\n';
        return false;
    }
    if constexpr (!std::is_same_v<T, std::nullptr_t>) {
        if (m.arguments().isEmpty())
            return false;
        result = qvariant_cast<T>(m.arguments().first());
    }
    return true;
}

int simpleJson(const char *method, const QVariantList &args = {}, const char *okKey = "ok")
{
    QString s;
    if (!call(s, method, args))
        return Failed;
    const QJsonValue v = parse(s);
    printJson(v);
    if (v.isObject() && v.toObject().contains(QLatin1String(okKey)))
        return v.toObject().value(QLatin1String(okKey)).toBool() ? Ok : Failed;
    return Ok;
}

int boolCall(const char *method, const QVariantList &args = {})
{
    bool b = false;
    if (!call(b, method, args))
        return Failed;
    if (g_json)
        printJson(QJsonObject{{QStringLiteral("ok"), b}});
    else
        out << (b ? "ok" : "failed") << '\n';
    return b ? Ok : Failed;
}

QString readFile(const QString &path, bool *ok)
{
    QFile f(path);
    *ok = (path == QLatin1String("-")) ? f.open(stdin, QIODevice::ReadOnly) : f.open(QIODevice::ReadOnly);
    return *ok ? QString::fromUtf8(f.readAll()) : QString();
}

QString capLine(const QJsonObject &c)
{
    return c.value(QStringLiteral("state")).toString();
}

int status()
{
    QString s;
    if (!call(s, "GetState"))
        return Failed;
    const QJsonObject st = parse(s).toObject();
    if (g_json) {
        printJson(st);
        return Ok;
    }
    const QJsonObject dev = st.value(QStringLiteral("device")).toObject();
    const QJsonObject caps = st.value(QStringLiteral("capabilities")).toObject();
    const QJsonObject inj = caps.value(QStringLiteral("injection")).toObject();
    const QJsonObject hap = st.value(QStringLiteral("haptics")).toObject();
    const QJsonObject prof = st.value(QStringLiteral("profile")).toObject();
    out << "OpenDial " << st.value(QStringLiteral("version")).toString() << '\n';
    out << "  Dial:        " << (dev.value(QStringLiteral("connected")).toBool() ? "connected" : "not connected");
    if (dev.value(QStringLiteral("battery")).isDouble())
        out << ", battery " << dev.value(QStringLiteral("battery")).toInt() << '%';
    out << '\n';
    if (!dev.value(QStringLiteral("connected")).toBool()) {
        const QJsonObject bt = st.value(QStringLiteral("bluetooth")).toObject();
        for (const auto &d : bt.value(QStringLiteral("devices")).toArray()) {
            const auto o = d.toObject();
            out << "               BlueZ: " << o.value(QStringLiteral("name")).toString() << " paired=" << o.value(QStringLiteral("paired")).toBool()
                << " connected=" << o.value(QStringLiteral("connected")).toBool() << " (press the Dial to wake it)\n";
        }
    }
    out << "  Haptics:     " << (hap.value(QStringLiteral("available")).toBool() ? QStringLiteral("available") : QStringLiteral("unavailable (") + hap.value(QStringLiteral("error")).toString() + QLatin1Char(')'))
        << ", resolution " << hap.value(QStringLiteral("appliedResolution")).toInt() << (hap.value(QStringLiteral("appliedAuto")).toBool() ? " (detent clicks)" : "") << '\n';
    out << "  Profile:     " << st.value(QStringLiteral("profileName")).toString() << " [" << prof.value(QStringLiteral("profileId")).toString() << "] "
        << prof.value(QStringLiteral("reason")).toString() << '\n';
    out << "  Tool:        " << st.value(QStringLiteral("activeToolName")).toString() << " [" << st.value(QStringLiteral("activeTool")).toString() << "]\n";
    const QJsonObject focus = st.value(QStringLiteral("focus")).toObject();
    out << "  Focus:       " << (focus.value(QStringLiteral("appId")).toString().isEmpty() ? QStringLiteral("(unknown)") : focus.value(QStringLiteral("appId")).toString())
        << " via " << caps.value(QStringLiteral("context")).toObject().value(QStringLiteral("active")).toString(QStringLiteral("none")) << '\n';
    out << "  Injection:   keyboard " << inj.value(QStringLiteral("keyboard")).toString() << " (" << inj.value(QStringLiteral("activeKeyboard")).toString()
        << "), pointer " << inj.value(QStringLiteral("pointer")).toString() << " (" << inj.value(QStringLiteral("activePointer")).toString() << ")\n";
    out << "  Audio:       " << caps.value(QStringLiteral("audio")).toObject().value(QStringLiteral("backend")).toString() << ' ' << capLine(caps.value(QStringLiteral("audio")).toObject()) << '\n';
    out << "  Media:       " << capLine(caps.value(QStringLiteral("media")).toObject()) << '\n';
    out << "  Brightness:  " << caps.value(QStringLiteral("brightness")).toObject().value(QStringLiteral("detail")).toString() << '\n';
    out << "  Workspaces:  " << caps.value(QStringLiteral("workspace")).toObject().value(QStringLiteral("method")).toString() << '\n';
    if (st.value(QStringLiteral("dryRun")).toBool())
        out << "  DRY RUN:     actions are logged, never injected\n";
    if (st.value(QStringLiteral("suppressed")).toBool())
        out << "  SUPPRESSED:  " << st.value(QStringLiteral("suppressReason")).toString() << '\n';
    if (st.value(QStringLiteral("pendingApprovals")).toInt() > 0)
        out << "  Approvals:   " << st.value(QStringLiteral("pendingApprovals")).toInt() << " action(s) waiting (opendialctl approvals)\n";
    out << "  Config:      " << st.value(QStringLiteral("configPath")).toString() << (st.value(QStringLiteral("configReadOnly")).toBool() ? " (read-only: newer version)" : "") << '\n';
    out.flush();
    return Ok;
}

int listTools()
{
    QString s, st;
    if (!call(s, "ListTools") || !call(st, "GetState"))
        return Failed;
    const QJsonArray tools = parse(s).toArray();
    if (g_json) {
        printJson(tools);
        return Ok;
    }
    const QString active = parse(st).toObject().value(QStringLiteral("activeTool")).toString();
    std::function<void(const QJsonArray &, int)> walk = [&](const QJsonArray &arr, int depth) {
        for (const auto &v : arr) {
            const auto t = v.toObject();
            const QString id = t.value(QStringLiteral("id")).toString();
            out << (id == active ? "* " : "  ") << QString(depth * 2, QLatin1Char(' ')) << id << "  " << t.value(QStringLiteral("name")).toString() << '\n';
            if (t.value(QStringLiteral("kind")).toString() == QLatin1String("folder"))
                walk(t.value(QStringLiteral("children")).toArray(), depth + 1);
        }
    };
    walk(tools, 0);
    out.flush();
    return Ok;
}

} // namespace

// Receives daemon signals for `watch` / `record`.
class Watcher : public QObject {
    Q_OBJECT
public:
    QFile *file = nullptr;
    bool showRaw = false;
public Q_SLOTS:
    void onRaw(const QString &j)
    {
        const auto o = QJsonDocument::fromJson(j.toUtf8()).object();
        if (file) {
            if (o.value(QStringLiteral("node")).toString() != QLatin1String("multiaxis"))
                return;
            QJsonObject line{{QStringLiteral("t"), o.value(QStringLiteral("t"))}, {QStringLiteral("type"), o.value(QStringLiteral("type"))},
                             {QStringLiteral("code"), o.value(QStringLiteral("code"))}, {QStringLiteral("value"), o.value(QStringLiteral("value"))}};
            file->write(QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n');
            file->flush();
            ++count;
            return;
        }
        if (showRaw) {
            out << "{\"raw\":" << j << "}\n";
            out.flush();
        }
    }
    void onGesture(const QString &j)
    {
        if (file)
            return;
        out << "{\"gesture\":" << j << "}\n";
        out.flush();
    }
    void onConnectionChange(const QString &j)
    {
        // StateChanged: report device connect/disconnect in recordings
        const auto st = QJsonDocument::fromJson(j.toUtf8()).object();
        const bool c = st.value(QStringLiteral("device")).toObject().value(QStringLiteral("connected")).toBool();
        if (c != connected) {
            connected = c;
            if (file) {
                file->write(c ? "{\"connect\":true}\n" : "{\"disconnect\":true}\n");
                file->flush();
            } else {
                out << "{\"device\":\"" << (c ? "connected" : "disconnected") << "\"}\n";
                out.flush();
            }
        }
    }
public:
    int count = 0;
    bool connected = false;
};

namespace {

int watch(bool raw, int seconds, const QString &recordPath)
{
    auto bus = QDBusConnection::sessionBus();
    QFile file;
    Watcher w;
    w.showRaw = raw;
    if (!recordPath.isEmpty()) {
        file.setFileName(recordPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            err << "cannot write " << recordPath << '\n';
            return Failed;
        }
        file.write("{\"comment\":\"recorded with opendialctl record; not synthetic\"}\n");
        w.file = &file;
    }
    const QString svc = QStringLiteral("io.github.opendial"), path = QStringLiteral("/io/github/opendial/Daemon"),
                  ifc = QStringLiteral("io.github.opendial.Daemon1");
    bus.connect(svc, path, ifc, QStringLiteral("RawEvent"), &w, SLOT(onRaw(QString)));
    bus.connect(svc, path, ifc, QStringLiteral("GestureEvent"), &w, SLOT(onGesture(QString)));
    bus.connect(svc, path, ifc, QStringLiteral("StateChanged"), &w, SLOT(onConnectionChange(QString)));
    QString st;
    if (call(st, "GetState"))
        w.connected = parse(st).toObject().value(QStringLiteral("device")).toObject().value(QStringLiteral("connected")).toBool();
    const bool needRaw = raw || !recordPath.isEmpty();
    if (needRaw)
        call(g_none, "SetRawStreaming", {true});

    // stop on Ctrl+C / SIGTERM or after N seconds
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    const int fd = signalfd(-1, &mask, SFD_CLOEXEC);
    QSocketNotifier n(fd, QSocketNotifier::Read);
    QObject::connect(&n, &QSocketNotifier::activated, qApp, [] { QCoreApplication::quit(); });
    if (seconds > 0)
        QTimer::singleShot(seconds * 1000, qApp, [] { QCoreApplication::quit(); });
    if (!recordPath.isEmpty())
        err << "recording raw Dial events to " << recordPath << " (Ctrl+C to stop)\n";
    QCoreApplication::exec();
    if (needRaw)
        call(g_none, "SetRawStreaming", {false});
    if (!recordPath.isEmpty())
        err << w.count << " events written\n";
    ::close(fd);
    return Ok;
}

int simulate(const QStringList &a)
{
    QJsonObject cmd;
    const QString what = a.value(0);
    if (what == QLatin1String("click"))
        cmd = {{QStringLiteral("click"), a.value(1, QStringLiteral("1")).toInt()}};
    else if (what == QLatin1String("press"))
        cmd = {{QStringLiteral("button"), QStringLiteral("down")}};
    else if (what == QLatin1String("release"))
        cmd = {{QStringLiteral("button"), QStringLiteral("up")}};
    else if (what == QLatin1String("longpress"))
        cmd = {{QStringLiteral("longPress"), true}};
    else if (what == QLatin1String("rotate")) {
        bool ok = false;
        const double d = a.value(1).toDouble(&ok);
        if (!ok) {
            err << "simulate rotate DEGREES (positive = clockwise)\n";
            return Usage;
        }
        cmd = {{QStringLiteral("rotate"), d}};
    } else if (what == QLatin1String("json")) {
        cmd = parse(a.value(1)).toObject();
    } else {
        err << "simulate click [N] | press | release | longpress | rotate DEGREES | json '{...}'\n";
        return Usage;
    }
    return simpleJson("Simulate", {QString::fromUtf8(QJsonDocument(cmd).toJson(QJsonDocument::Compact))});
}

int configCmd(const QStringList &a)
{
    const QString sub = a.value(0);
    if (sub == QLatin1String("path")) {
        out << configDir() << "/config.json\n";
        return Ok;
    }
    if (sub == QLatin1String("validate")) {
        // works without a running daemon
        QString text;
        if (a.size() > 1) {
            bool ok;
            text = readFile(a.at(1), &ok);
            if (!ok) {
                err << "cannot read " << a.at(1) << '\n';
                return Failed;
            }
        } else {
            QFile f(configDir() + QStringLiteral("/config.json"));
            if (!f.open(QIODevice::ReadOnly)) {
                out << "no configuration file yet (defaults in use)\n";
                return Ok;
            }
            text = QString::fromUtf8(f.readAll());
        }
        QJsonParseError pe;
        const auto doc = QJsonDocument::fromJson(text.toUtf8(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            err << "invalid JSON: " << pe.errorString() << " at offset " << pe.offset << '\n';
            return Failed;
        }
        auto mig = migrateConfig(doc.object());
        if (!mig.ok) {
            err << mig.log.join(QStringLiteral("; ")) << '\n';
            return Failed;
        }
        const auto v = validateConfig(mig.config);
        if (g_json) {
            printJson(QJsonObject{{QStringLiteral("ok"), v.ok()}, {QStringLiteral("issues"), v.toJson()}, {QStringLiteral("migrated"), mig.changed}});
        } else {
            for (const auto &e : v.errors())
                out << "error: " << e << '\n';
            for (const auto &w : v.warnings())
                out << "warning: " << w << '\n';
            if (mig.changed)
                out << "note: file uses an older format and will be migrated on load\n";
            out << (v.ok() ? "configuration is valid\n" : "configuration is INVALID\n");
        }
        return v.ok() ? Ok : Failed;
    }
    if (!daemonRunning()) {
        err << "opendiald is not running (systemctl --user start opendial)\n";
        return NoDaemon;
    }
    if (sub == QLatin1String("get")) {
        QString s;
        if (!call(s, "GetConfig"))
            return Failed;
        printJson(parse(s));
        return Ok;
    }
    if (sub == QLatin1String("set") && a.size() > 1) {
        bool ok;
        const QString text = readFile(a.at(1), &ok);
        if (!ok) {
            err << "cannot read " << a.at(1) << '\n';
            return Failed;
        }
        return simpleJson("SetConfig", {text});
    }
    if (sub == QLatin1String("reset"))
        return simpleJson("ResetToDefaults");
    if (sub == QLatin1String("rollback"))
        return simpleJson("RollbackConfig");
    err << "config path | get | validate [FILE] | set FILE | reset | rollback\n";
    return Usage;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("opendialctl"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OPENDIAL_VERSION));
    QStringList args = app.arguments().mid(1);
    if (args.removeAll(QStringLiteral("--json")) > 0)
        g_json = true;
    if (args.isEmpty() || args.first() == QLatin1String("-h") || args.first() == QLatin1String("--help") || args.first() == QLatin1String("help")) {
        out << kUsage;
        return args.isEmpty() ? Usage : Ok;
    }
    if (args.first() == QLatin1String("--version") || args.first() == QLatin1String("version")) {
        out << "opendialctl " << OPENDIAL_VERSION << '\n';
        return Ok;
    }
    const QString cmd = args.takeFirst();
    if (cmd == QLatin1String("config"))
        return configCmd(args);
    if (!daemonRunning()) {
        err << "opendiald is not running. Start it with: systemctl --user start opendial\n";
        return NoDaemon;
    }
    if (cmd == QLatin1String("status"))
        return status();
    if (cmd == QLatin1String("diagnose")) {
        const bool noRedact = args.contains(QStringLiteral("--no-redact"));
        int e = args.indexOf(QStringLiteral("--export"));
        if (e < 0)
            e = args.indexOf(QStringLiteral("--out")); // same flag name as `record`
        if (e >= 0) {
            const QString path = QFileInfo(args.value(e + 1, QStringLiteral("opendial-diagnostics.json"))).absoluteFilePath();
            return simpleJson("ExportDiagnostics", {path, !noRedact});
        }
        QString s;
        if (!call(s, "GetDiagnostics", {!noRedact}))
            return Failed;
        printJson(parse(s));
        return Ok;
    }
    if (cmd == QLatin1String("devices")) {
        QString s;
        if (!call(s, "GetDiagnostics", {true}))
            return Failed;
        const QJsonObject d = parse(s).toObject();
        printJson(QJsonObject{{QStringLiteral("nodes"), d.value(QStringLiteral("nodes"))}, {QStringLiteral("device"), d.value(QStringLiteral("device"))},
                              {QStringLiteral("bluetooth"), d.value(QStringLiteral("bluetooth"))}});
        return Ok;
    }
    if (cmd == QLatin1String("watch")) {
        const int i = args.indexOf(QStringLiteral("--seconds"));
        return watch(args.contains(QStringLiteral("--raw")), i >= 0 ? args.value(i + 1).toInt() : 0, {});
    }
    if (cmd == QLatin1String("record")) {
        if (args.isEmpty() || args.first().startsWith(QLatin1String("--"))) {
            err << "record FILE [--seconds N]\n";
            return Usage;
        }
        const int i = args.indexOf(QStringLiteral("--seconds"));
        return watch(true, i >= 0 ? args.value(i + 1).toInt() : 0, args.first());
    }
    if (cmd == QLatin1String("tools"))
        return listTools();
    if (cmd == QLatin1String("tool") && !args.isEmpty())
        return boolCall("SetActiveTool", {args.first()});
    if (cmd == QLatin1String("profiles"))
        return simpleJson("ListProfiles");
    if (cmd == QLatin1String("profile")) {
        if (args.value(0) == QLatin1String("lock")) {
            QString id = args.value(1);
            if (id.isEmpty()) {
                QString s;
                call(s, "GetState");
                id = parse(s).toObject().value(QStringLiteral("profile")).toObject().value(QStringLiteral("profileId")).toString();
            }
            return boolCall("LockProfile", {id});
        }
        if (args.value(0) == QLatin1String("unlock")) {
            std::nullptr_t n;
            return call(n, "UnlockProfile") ? Ok : Failed;
        }
        err << "profile lock [ID] | unlock\n";
        return Usage;
    }
    if (cmd == QLatin1String("menu")) {
        std::nullptr_t n;
        if (args.value(0) == QLatin1String("open"))
            return call(n, "OpenMenu") ? Ok : Failed;
        if (args.value(0) == QLatin1String("close"))
            return call(n, "CloseMenu") ? Ok : Failed;
        err << "menu open|close\n";
        return Usage;
    }
    if (cmd == QLatin1String("import") && !args.isEmpty()) {
        bool ok;
        const QString text = readFile(args.first(), &ok);
        if (!ok) {
            err << "cannot read " << args.first() << '\n';
            return Failed;
        }
        return simpleJson("ImportProfile", {text});
    }
    if (cmd == QLatin1String("export") && !args.isEmpty()) {
        QString s;
        if (!call(s, "ExportProfile", {args.first()}))
            return Failed;
        const QJsonObject o = parse(s).toObject();
        if (o.isEmpty()) {
            err << "no profile " << args.first() << '\n';
            return Failed;
        }
        if (args.size() > 1) {
            QFile f(args.at(1));
            if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(o).toJson()) < 0) {
                err << "cannot write " << args.at(1) << '\n';
                return Failed;
            }
            return Ok;
        }
        printJson(o);
        return Ok;
    }
    if (cmd == QLatin1String("approvals")) {
        QString s;
        if (!call(s, "ListApprovals"))
            return Failed;
        const QJsonArray arr = parse(s).toArray();
        if (g_json) {
            printJson(arr);
            return Ok;
        }
        if (arr.isEmpty())
            out << "no actions need approval\n";
        for (const auto &v : arr) {
            const auto o = v.toObject();
            out << (o.value(QStringLiteral("approved")).toBool() ? "[approved] " : "[pending]  ") << o.value(QStringLiteral("fingerprint")).toString().left(16)
                << "  " << o.value(QStringLiteral("description")).toString() << "  (" << o.value(QStringLiteral("path")).toString(o.value(QStringLiteral("toolId")).toString()) << ")\n";
        }
        out << "approve with: opendialctl approve <fingerprint prefix>\n";
        out.flush();
        return Ok;
    }
    if ((cmd == QLatin1String("approve") || cmd == QLatin1String("revoke")) && !args.isEmpty()) {
        // accept an unambiguous prefix
        QString s;
        if (!call(s, "ListApprovals"))
            return Failed;
        QStringList matches;
        for (const auto &v : parse(s).toArray())
            if (v.toObject().value(QStringLiteral("fingerprint")).toString().startsWith(args.first()))
                matches << v.toObject().value(QStringLiteral("fingerprint")).toString();
        const QString fp = matches.size() == 1 ? matches.first() : args.first();
        if (matches.size() > 1) {
            err << "ambiguous fingerprint prefix\n";
            return Failed;
        }
        return boolCall(cmd == QLatin1String("approve") ? "ApproveAction" : "RevokeApproval", {fp});
    }
    if (cmd == QLatin1String("plugins")) {
        QString s;
        if (!call(s, "GetState"))
            return Failed;
        printJson(parse(s).toObject().value(QStringLiteral("plugins")));
        return Ok;
    }
    if (cmd == QLatin1String("plugin") && args.size() == 2 && (args.first() == QLatin1String("allow") || args.first() == QLatin1String("deny")))
        return boolCall("ApprovePlugin", {args.at(1), args.first() == QLatin1String("allow")});
    if (cmd == QLatin1String("test") && args.value(0) == QLatin1String("haptics")) {
        const int r = boolCall("TestHaptics", {args.value(1, QStringLiteral("click"))});
        if (r != Ok)
            err << "haptics unavailable: is the Dial connected and is /dev/hidraw* writable? (see opendialctl devices)\n";
        return r;
    }
    if (cmd == QLatin1String("bluetooth"))
        return simpleJson("Bluetooth", {args.value(0, QStringLiteral("status")), args.value(1)});
    if (cmd == QLatin1String("reconnect") || cmd == QLatin1String("portal") || cmd == QLatin1String("pause") || cmd == QLatin1String("resume")) {
        std::nullptr_t n;
        bool ok = cmd == QLatin1String("reconnect")  ? call(n, "Reconnect")
                  : cmd == QLatin1String("portal")   ? call(n, "RequestPortalPermission")
                  : cmd == QLatin1String("pause")    ? call(n, "SetPaused", {true})
                                                     : call(n, "SetPaused", {false});
        return ok ? Ok : Failed;
    }
    if (cmd == QLatin1String("simulate"))
        return simulate(args);
    if (cmd == QLatin1String("context")) {
        if (args.value(0) == QLatin1String("clear") || args.isEmpty())
            return boolCall("SimulateContext", {QString()});
        const QJsonObject ctx{{QStringLiteral("appId"), args.first()}, {QStringLiteral("title"), args.value(1)}};
        return boolCall("SimulateContext", {QString::fromUtf8(QJsonDocument(ctx).toJson(QJsonDocument::Compact))});
    }
    err << "unknown command: " << cmd << "\n\n" << kUsage;
    return Usage;
}

#include "main.moc"
