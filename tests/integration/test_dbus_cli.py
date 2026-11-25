#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""End to end over a private session bus: start opendiald (dry-run, no
hardware), drive it with opendialctl and check JSON output and exit codes.
Run under dbus-run-session (ctest does this)."""
import json, os, signal, subprocess, sys, tempfile, time

daemon, ctl = sys.argv[1], sys.argv[2]
tmp = tempfile.TemporaryDirectory()
env = dict(os.environ, XDG_CONFIG_HOME=tmp.name + "/cfg", XDG_STATE_HOME=tmp.name + "/state", QT_QPA_PLATFORM="offscreen",
           QT_FORCE_STDERR_LOGGING="1")
failures = []


def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + (f"  ({detail})" if detail and not cond else ""))
    if not cond:
        failures.append(name)


def octl(*args, expect=0):
    p = subprocess.run([ctl, *args], env=env, capture_output=True, text=True, timeout=30)
    if p.returncode != expect:
        print(f"  opendialctl {' '.join(args)} -> {p.returncode}\n{p.stdout}\n{p.stderr}")
    return p


def jctl(*args):
    p = octl(*args, "--json")
    try:
        return json.loads(p.stdout)
    except json.JSONDecodeError:
        return None


# no daemon: exit code 3
check("no daemon -> exit 3", octl("status", expect=3).returncode == 3)

d = subprocess.Popen([daemon, "--dry-run", "--no-device", "--no-context", "--no-overlay", "--no-tray"], env=env,
                     stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
try:
    for _ in range(50):
        if octl("status", expect=None).returncode == 0:
            break
        time.sleep(0.1)
    st = jctl("status")
    check("status --json", st is not None and st.get("activeTool") == "volume", str(st)[:200])
    check("dry-run reported", st and st.get("dryRun") is True)

    # second instance refuses (single instance) and exits 0
    p2 = subprocess.run([daemon, "--dry-run", "--no-device", "--no-context", "--no-overlay", "--no-tray"], env=env,
                        capture_output=True, text=True, timeout=10)
    check("single instance", p2.returncode == 0 and "another" in p2.stderr, p2.stderr[-200:])

    tools = jctl("tools")
    check("tools list", isinstance(tools, list) and any(t.get("id") == "zoom" for t in tools))

    check("set tool", octl("tool", "scroll").returncode == 0 and jctl("status")["activeTool"] == "scroll")
    check("unknown tool fails", octl("tool", "nope", expect=1).returncode == 1)

    octl("simulate", "longpress")
    time.sleep(0.8)
    check("long press opens menu", jctl("status")["menuOpen"] is True)
    octl("simulate", "rotate", "20")
    octl("simulate", "click")
    time.sleep(0.6)
    s = jctl("status")
    check("menu select", s["menuOpen"] is False and s["activeTool"] == "zoom", f"{s['menuOpen']} {s['activeTool']}")

    octl("context", "org.kde.krita")
    time.sleep(0.2)
    check("app profile via context", jctl("status")["profile"]["profileId"] == "krita")
    octl("context", "clear")

    check("profile lock", octl("profile", "lock", "krita").returncode == 0 and jctl("status")["lockedProfile"] == "krita")
    octl("profile", "unlock")

    cfg_path = os.path.join(tmp.name, "cfg", "opendial", "config.json")
    check("config file written", os.path.exists(cfg_path))
    check("config validate (file)", octl("config", "validate", cfg_path).returncode == 0)
    bad = os.path.join(tmp.name, "bad.json")
    with open(bad, "w") as f:
        json.dump({"schema": "opendial.config", "version": 1, "profiles": [{"id": "x", "tools": [{"id": "t", "bindings": {"click": {"type": "rm"}}}]}]}, f)
    check("config validate rejects bad", octl("config", "validate", bad, expect=1).returncode == 1)

    exp = os.path.join(tmp.name, "krita.json")
    check("export profile", octl("export", "krita", exp).returncode == 0 and os.path.exists(exp))
    check("import profile", octl("import", exp).returncode == 0)
    ids = [p["id"] for p in jctl("profiles")]
    check("imported copy renamed", any(i.startswith("krita-") for i in ids), str(ids))

    octl("tool", "volume")
    octl("simulate", "rotate", "30")
    time.sleep(0.3)
    cfg = jctl("config", "get")
    cfg["profiles"][0]["tools"] += [{"id": "secret", "name": "Secret", "bindings": {
        "click": {"type": "command", "argv": ["true", "secret-pending"]},
        "double_click": {"type": "command", "argv": ["true", "secret-approved"]},
        "long_press": {"type": "launch", "url": "https://example.org/secret-url"}}}]
    new_cfg = os.path.join(tmp.name, "secret.json")
    with open(new_cfg, "w") as f:
        json.dump(cfg, f)
    check("config with commands accepted", octl("config", "set", new_cfg).returncode == 0)
    for a in jctl("approvals"):
        if "secret-approved" in json.dumps(a["action"]):
            octl("approve", a["fingerprint"])
    octl("tool", "secret")
    octl("simulate", "click")
    time.sleep(0.6)
    diag = os.path.join(tmp.name, "diag.json")
    check("diagnostics export", octl("diagnose", "--out", diag).returncode == 0 and os.path.exists(diag))
    if os.path.exists(diag):
        check("diagnostics file is private", (os.stat(diag).st_mode & 0o077) == 0)
        with open(diag) as f:
            report = json.load(f)
        acts = [e for e in report.get("eventLog", []) if "action" in e]
        check("redacted log keeps only action types", bool(acts) and all(e["action"] == e.get("actionType") for e in acts), str(acts[:3]))
        ap = report.get("approvals", [])
        check("redacted approvals keep the action type and state",
              sorted((str(e.get("action")), str(e.get("approved"))) for e in ap) == [("command", "False"), ("command", "True"), ("launch", "False")], str(ap))
        text = json.dumps(report)
        fps = [e["fingerprint"] for e in jctl("approvals")]
        check("redacted report hides commands and URLs", "secret-" not in text, text[max(0, text.find("secret-") - 200):][:300])
        check("redacted report hides action fingerprints", len(fps) == 3 and not any(fp in text for fp in fps))
        full = json.dumps(jctl("diagnose", "--no-redact"))
        check("full report keeps approval details", all(x in full for x in ("secret-pending", "secret-approved", "secret-url")))
    check("diagnostics outside home refused", octl("diagnose", "--out", "/etc/opendial-diag.json", expect=1).returncode == 1)
    check("diagnostics '..' escape refused", octl("diagnose", "--out", "/tmp/../etc/opendial-diag.json", expect=1).returncode == 1)

    check("pause", octl("pause").returncode == 0 and jctl("status")["paused"] is True)
    check("resume", octl("resume").returncode == 0 and jctl("status")["paused"] is False)

    check("garbage simulate rejected", octl("simulate", "json", '{"rotate": 1e12}', expect=1).returncode == 1)
finally:
    d.send_signal(signal.SIGTERM)
    try:
        rc = d.wait(timeout=10)
    except subprocess.TimeoutExpired:
        d.kill()
        rc = -9
    check("clean shutdown on SIGTERM", rc == 0, f"rc={rc}")

sys.exit(1 if failures else 0)
