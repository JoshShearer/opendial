#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""End to end over a private session bus: command actions really run here.

The daemon is started WITHOUT --dry-run (dry-run never starts a process), but
with no device, no context tracker and no display, and the only actions bound
are the commands below. Covers the outcome of every command (GitHub issue 1),
the overlap policy for repeated events (GitHub issue 2) and commands inside
macro, if and value actions (GitHub issue 4).
Run under dbus-run-session (ctest does this)."""
import json, os, shutil, signal, subprocess, sys, tempfile, time

daemon, ctl = sys.argv[1], sys.argv[2]
tmp = tempfile.TemporaryDirectory()
env = dict(os.environ, XDG_CONFIG_HOME=tmp.name + "/cfg", XDG_STATE_HOME=tmp.name + "/state", QT_QPA_PLATFORM="offscreen",
           QT_FORCE_STDERR_LOGGING="1")
for k in ("WAYLAND_DISPLAY", "DISPLAY", "HYPRLAND_INSTANCE_SIGNATURE"):
    env.pop(k, None)  # never reach the desktop the tests run on
failures = []


def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + (f"  ({detail})" if detail and not cond else ""), flush=True)
    if not cond:
        failures.append(name)


def octl(*args):
    return subprocess.run([ctl, *args], env=env, capture_output=True, text=True, timeout=30)


def jctl(*args):
    p = octl(*args, "--json")
    try:
        return json.loads(p.stdout)
    except json.JSONDecodeError:
        return None


def event_log():
    return (jctl("diagnose", "--no-redact") or {}).get("eventLog", [])


def wait_for(fn, timeout=5.0):
    end = time.monotonic() + timeout
    while True:
        r = fn()
        if r or time.monotonic() > end:
            return r
        time.sleep(0.05)


def children(pid):
    """Live child processes of the daemon (zombies excluded), from /proc."""
    n = 0
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            with open(f"/proc/{d}/stat") as f:
                rest = f.read().rsplit(")", 1)[1].split()
            n += int(rest[1]) == pid and rest[0] != "Z"
        except (OSError, IndexError):
            pass
    return n


mark = os.path.join(tmp.name, "mark")


def touch_cmd(tag, seconds):
    """Appends '<tag>' to the mark file when it starts, then sleeps."""
    return ["sh", "-c", f"echo {tag} >> {mark}; exec sleep {seconds}"]


def runs(tag):
    try:
        with open(mark) as f:
            return f.read().split().count(tag)
    except OSError:
        return 0


def command(argv, **kw):
    return dict({"type": "command", "argv": argv}, **kw)


def macro(*steps, **kw):
    return dict({"type": "macro", "steps": [{"delayMs": x} if isinstance(x, int) else {"action": x} for x in steps]}, **kw)


def value(variable, output, hi=20):
    return {"type": "value", "variable": variable, "min": 0, "max": hi, "step": 1, "default": 0, "output": output}


TOOLS = {
    "missing": {"click": command(["/nonexistent/opendial-test"])},
    "nonzero": {"click": command(["sh", "-c", "exit 7"])},
    "good": {"click": command(["true"])},
    "killed": {"click": command(["sh", "-c", "kill -9 $$"])},
    "timeout": {"click": command(["sleep", "30"], timeoutMs=300)},
    "spin": {"rotate_cw": command(touch_cmd("spin", 5)), "click": command(["true", "responsive"])},
    "pressclick": {"click": command(touch_cmd("queued", 2))},
    "reject": {"rotate_cw": command(touch_cmd("reject", 2), overlap="reject")},
    "queue": {"rotate_cw": command(touch_cmd("queue", 1), overlap="queue")},
    "paused": {"click": command(touch_cmd("paused", 3))},
    "revoked": {"click": command(touch_cmd("revoked", 3))},
    "macro": {"click": macro(command(touch_cmd("m1", 0)), 100, command(touch_cmd("m2", 0)))},
    "if": {"click": {"type": "if", "condition": {"variable": "if.x", "op": "ge", "value": 1},
                     "then": command(touch_cmd("then", 0)), "else": command(touch_cmd("else", 0))}},
    "nested": {"click": macro({"type": "if", "condition": {}, "then": value("nest.x", command(touch_cmd("nested{value}", 0)))})},
    "value": {"rotate_cw": value("val.x", command(["sh", "-c", f"echo arg$0 >> {mark}", "{value}"]))},
    "vslow": {"rotate_cw": value("vslow.x", command(touch_cmd("vslow{value}", 2)))},
    "vfail": {"rotate_cw": value("vfail.x", command(["/nonexistent/value-test", "{value}"]))},
    "inner": {"click": command(touch_cmd("m1", 0))},  # same command as in "macro", not approved itself
    "unapproved": {"click": macro(command(touch_cmd("unapproved", 0)))},
    "mrevoke": {"click": macro(command(touch_cmd("before", 0)), 1500, command(touch_cmd("after", 0)))},
    "mwait": {"click": macro(command(touch_cmd("mwait", 2)), command(touch_cmd("mwait", 2)))},
    "mfull": {"click": macro(command(touch_cmd("mfull", 0.2)), repeat=10)},
    "shutdown": {"click": command(touch_cmd("shutdown", 60), timeoutMs=60000)},
}


NOT_APPROVED = [TOOLS["inner"]["click"], TOOLS["unapproved"]["click"]]


def outcomes(tool):  # each tool is exercised once
    return [e for e in event_log() if e.get("kind") == "command" and e.get("tool") == tool]


def states(tool):
    return [e["state"] for e in outcomes(tool)]


def requests(tool):
    return [e for e in event_log() if e.get("kind") == "action" and e.get("tool") == tool]


def use(tool):
    octl("tool", tool)


def revoke(tool):
    return octl("revoke", fps.get(json.dumps(TOOLS[tool]["click"], sort_keys=True), ""))


def click():
    octl("simulate", "click")
    time.sleep(0.5)  # past the double-click window


def rotate(times):
    seq = [{"rotate": 10}, {"wait": 10}] * times
    octl("simulate", "json", json.dumps({"sequence": seq}))


bus_log = os.path.join(tmp.name, "notifications")
monitor = None
if shutil.which("dbus-monitor"):
    monitor = subprocess.Popen(["dbus-monitor", "--session", "interface='org.freedesktop.Notifications',member='Notify'"],
                               stdout=open(bus_log, "w"), stderr=subprocess.DEVNULL)


def notified(text):
    with open(bus_log) as f:
        return sum(text in line for line in f)


d = subprocess.Popen([daemon, "--no-device", "--no-context", "--no-overlay", "--no-tray"], env=env,
                     stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
try:
    wait_for(lambda: octl("status").returncode == 0)
    cfg = jctl("config", "get")
    cfg["profiles"][0]["tools"] += [{"id": i, "name": i, "bindings": b} for i, b in TOOLS.items()]
    path = os.path.join(tmp.name, "new.json")
    with open(path, "w") as f:
        json.dump(cfg, f)
    p = octl("config", "set", path)
    check("config with commands accepted", p.returncode == 0, p.stdout + p.stderr)
    fps = {}
    for a in jctl("approvals"):
        fps[json.dumps(a["action"], sort_keys=True)] = a["fingerprint"]
        if a["action"] not in NOT_APPROVED:
            octl("approve", a["fingerprint"])

    # ------------------------------------------------ outcomes (issue 1)
    use("missing")
    click()
    ev = wait_for(lambda: outcomes("missing"))
    check("missing executable -> failed-to-start", [e["state"] for e in ev] == ["failed-to-start"], str(ev))
    check("missing executable: not ok, has a reason", bool(ev) and ev[-1]["ok"] is False and bool(ev[-1].get("message")), str(ev))
    last = jctl("status")["lastAction"]
    check("missing executable visible in status", last.startswith("failed") and "failed to start" in last, last)

    if monitor:
        check("failure notification", wait_for(lambda: notified("(/nonexistent/opendial-test) failed to start") == 1))
        click()
        wait_for(lambda: len(outcomes("missing")) == 2)
        time.sleep(0.3)
        check("repeated failure notifies once", len(outcomes("missing")) == 2 and notified(") failed to start") == 1)

    use("nonzero")
    click()
    ev = wait_for(lambda: [e for e in outcomes("nonzero") if e["state"] == "finished"])
    check("nonzero exit -> started, finished(7)", states("nonzero") == ["started", "finished"] and ev[0]["exitCode"] == 7, str(ev))
    last = jctl("status")["lastAction"]
    check("nonzero exit is a visible failure", bool(ev) and ev[0]["ok"] is False and last.startswith("failed") and "7" in last, last)

    use("good")
    click()
    ev = wait_for(lambda: [e for e in outcomes("good") if e["state"] == "finished"])
    check("success -> started, finished(0)", states("good") == ["started", "finished"] and ev[0]["exitCode"] == 0 and ev[0]["ok"], str(ev))
    last = jctl("status")["lastAction"]
    check("success visible in status", last.startswith("ok") and "finished" in last, last)
    req = requests("good")
    ids = {e.get("id") for e in outcomes("good")} | {e.get("commandId") for e in req}
    check("request and outcomes share one id", len(ids) == 1 and None not in ids, str(ids))

    use("killed")
    click()
    ev = wait_for(lambda: [e for e in outcomes("killed") if e["state"] == "crashed"])
    check("killed by a signal -> crashed", bool(ev) and ev[0]["ok"] is False, str(outcomes("killed")))

    use("timeout")
    click()
    ev = wait_for(lambda: [e for e in outcomes("timeout") if e["state"] == "timed-out"])
    check("timeout -> timed-out", bool(ev) and ev[0]["ok"] is False, str(outcomes("timeout")))

    red = jctl("diagnose") or {}
    text = json.dumps([e for e in red.get("eventLog", []) if e.get("kind") == "command"]) + str(red.get("lastAction"))
    check("redacted report hides command lines", "nonexistent" not in text and "sleep" not in text, text[:300])

    # ------------------------------------------------ overlap (issue 2)
    use("spin")
    rotate(50)
    peak = 0
    end = time.monotonic() + 3
    while time.monotonic() < end:
        peak = max(peak, children(d.pid))
        time.sleep(0.02)
    rotations = [e for e in requests("spin") if e.get("slot") == "rotate_cw"]
    check("rotation events delivered", len(rotations) >= 40, str(len(rotations)))
    check("slow command on rotation: one process at a time", peak == 1, f"peak {peak}")
    click()
    ev = wait_for(lambda: [e for e in outcomes("spin") if e["state"] == "finished" and e.get("slot") == "click"], 2)
    check("other inputs respond while it runs", bool(ev) and children(d.pid) == 1, str(outcomes("spin")))
    wait_for(lambda: states("spin").count("finished") >= 3, 12)
    time.sleep(0.5)
    check("merged rotation events run once more", runs("spin") == 2 and children(d.pid) == 0, f"{runs('spin')} runs")

    use("pressclick")
    for _ in range(3):
        click()
    check("clicks wait, not overlap", children(d.pid) == 1 and "queued" in states("pressclick"), str(states("pressclick")))
    wait_for(lambda: states("pressclick").count("finished") == 3, 10)
    check("every click ran", runs("queued") == 3, str(runs("queued")))

    use("reject")
    rotate(10)
    wait_for(lambda: "finished" in states("reject"), 5)
    time.sleep(0.3)
    req = requests("reject")
    check("reject: ran once", runs("reject") == 1, str(runs("reject")))
    check("reject: refusals are visible", sum(not e["ok"] and "already running" in e.get("message", "") for e in req) == len(req) - 1, str(req[-1:]))

    use("queue")
    rotate(20)
    wait_for(lambda: states("queue").count("finished") == 5, 9)
    time.sleep(0.5)
    req = requests("queue")
    check("queue: one running plus four waiting", runs("queue") == 5 and children(d.pid) == 0, str(runs("queue")))
    check("queue: overflow refused", any(not e["ok"] and "queue" in e.get("message", "") for e in req), str(req[-1:]))

    # ---------------------------------- waiting commands lose their licence
    use("paused")
    click()
    click()
    octl("pause")
    wait_for(lambda: "finished" in states("paused"))
    time.sleep(0.4)
    check("pause drops waiting commands", runs("paused") == 1 and "cancelled" in states("paused"), str(states("paused")))
    octl("resume")

    use("revoked")
    click()
    click()
    check("second click waits", states("revoked") == ["started", "queued"], str(states("revoked")))
    p = revoke("revoked")
    check("approval revoked", p.returncode == 0, p.stdout + p.stderr)
    wait_for(lambda: "finished" in states("revoked"))
    time.sleep(0.4)
    check("revoking approval drops waiting commands", runs("revoked") == 1 and "cancelled" in states("revoked"), str(states("revoked")))

    # -------------------------- commands inside macro, if, value (issue 4)
    use("macro")
    click()
    wait_for(lambda: states("macro").count("finished") == 2)
    check("approved macro runs its commands", (runs("m1"), runs("m2")) == (1, 1), str(requests("macro")))

    use("if")
    click()
    wait_for(lambda: "finished" in states("if"))
    check("approved if runs the matching branch only", (runs("then"), runs("else")) == (0, 1), str(requests("if")))

    use("nested")
    click()
    wait_for(lambda: "finished" in states("nested"))
    check("approved nested composites run", runs("nested1") == 1, str(requests("nested")))

    use("value")
    rotate(1)
    wait_for(lambda: "finished" in states("value"))
    check("approved value runs its command with the value as argument", runs("arg1") == 1, str(requests("value")))

    use("vslow")
    rotate(12)
    peak = 0
    end = time.monotonic() + 5.5
    while time.monotonic() < end:
        peak = max(peak, children(d.pid))
        time.sleep(0.02)
    ran = [i for i in range(21) if runs(f"vslow{i}")]
    check("turning a value: one process at a time", peak == 1, f"peak {peak}")
    check("turning a value: merged run uses the latest value", ran == [1, 12], str(ran))

    use("vfail")
    for n in (1, 2):  # one at a time: detents during a run would merge
        rotate(1)
        wait_for(lambda: len(outcomes("vfail")) == n)
    time.sleep(0.3)
    check("failing value command is visible each time", states("vfail") == ["failed-to-start"] * 2, str(states("vfail")))
    if monitor:
        check("failing value command notifies once", notified("(/nonexistent/value-test) failed to start") == 1)

    use("unapproved")
    click()
    time.sleep(0.3)
    req = requests("unapproved")
    check("unapproved macro starts nothing", runs("unapproved") == 0 and bool(req) and "needs approval" in req[-1].get("message", ""), str(req))
    desc = [a["description"] for a in jctl("approvals") if a["action"] == TOOLS["unapproved"]["click"]]
    check("approval request shows the command inside", bool(desc) and "echo unapproved" in desc[0], str(desc))
    if monitor:
        check("approval notification shows the command inside", wait_for(lambda: notified("echo unapproved") >= 1))

    use("inner")
    click()
    time.sleep(0.3)
    check("approving a macro does not approve its commands elsewhere", runs("m1") == 1, str(requests("inner")))

    use("mrevoke")
    click()
    wait_for(lambda: runs("before") == 1)
    revoke("mrevoke")
    time.sleep(2)
    check("revoking a macro stops its later commands", (runs("before"), runs("after")) == (1, 0))
    ev = [e for e in event_log() if e.get("kind") == "macro" and e.get("tool") == "mrevoke"]
    check("a step that fails later is visible", len(ev) == 1 and not ev[0]["ok"] and "no longer approved" in ev[0]["message"], str(ev))
    last = jctl("status")["lastAction"]
    check("a step that fails later is visible in status", last.startswith("failed") and "no longer approved" in last, last)

    use("mwait")
    click()
    check("macro command waits for the same command", wait_for(lambda: sorted(states("mwait")) == ["queued", "started"]), str(states("mwait")))
    revoke("mwait")
    wait_for(lambda: "cancelled" in states("mwait"))
    check("revoking a macro drops its waiting commands", runs("mwait") == 1 and "cancelled" in states("mwait"), str(states("mwait")))

    use("mfull")
    click()
    wait_for(lambda: states("mfull").count("finished") == 5)
    time.sleep(0.4)
    req = requests("mfull")
    check("macro over the queue limit: five run", runs("mfull") == 5, str(runs("mfull")))
    check("macro over the queue limit: refusal is visible", bool(req) and not req[-1]["ok"] and "queue is full" in req[-1].get("message", ""), str(req))

    use("shutdown")
    click()
    check("command running at shutdown", wait_for(lambda: children(d.pid) == 1))
finally:
    d.send_signal(signal.SIGTERM)
    try:
        rc = d.wait(timeout=10)
    except subprocess.TimeoutExpired:
        d.kill()
        rc = -9
    if monitor:
        monitor.terminate()
    check("clean shutdown with a command running", rc == 0, f"rc={rc}")

sys.exit(1 if failures else 0)
