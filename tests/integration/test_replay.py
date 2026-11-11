#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Replay fixtures through the real decoder + engine (opendiald --replay) and
assert on the engine's event log. Synthetic fixtures only prove the software
path; hardware evidence comes from tests/hardware."""
import json, os, subprocess, sys, tempfile

daemon, fixtures = sys.argv[1], sys.argv[2]
failures = []


def run(fixture, speed=4):
    with tempfile.TemporaryDirectory() as d:
        env = dict(os.environ, XDG_CONFIG_HOME=d + "/cfg", XDG_STATE_HOME=d + "/state",
                   XDG_RUNTIME_DIR=d, DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent")
        p = subprocess.run([daemon, "--replay", os.path.join(fixtures, fixture), "--replay-speed", str(speed),
                            "--exit-after-replay", "--dry-run", "--no-context", "--no-overlay", "--no-tray"],
                           env=env, capture_output=True, text=True, timeout=60)
        events = []
        for line in p.stdout.splitlines():
            line = line.strip()
            if line.startswith("{"):
                events.append(json.loads(line))
        return p.returncode, events, p.stderr


def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + (f"  ({detail})" if detail and not cond else ""))
    if not cond:
        failures.append(name)


# click, 3 CW + 2 CCW detents, long press, hold-and-turn
rc, ev, err = run("synthetic-click-rotate.jsonl", speed=1)  # gestures are timed: real speed
check("click-rotate exits cleanly", rc == 0, err[-400:])
kinds = [e.get("kind") for e in ev]
acts = [e for e in ev if e.get("kind") == "action"]
cw = sum(abs(e["steps"]) for e in acts if e["slot"] == "rotate_cw")
ccw = sum(abs(e["steps"]) for e in acts if e["slot"] == "rotate_ccw")
check("click fires the click slot", any(e["slot"] == "click" for e in acts), str(acts))
check("clockwise detents", cw >= 3, f"cw={cw}")
check("counter-clockwise detents", ccw >= 2, f"ccw={ccw}")
check("long press opens the menu", "menu-open" in kinds, str(kinds))

# buffered rotation right after connect must be dropped (no runaway volume)
rc, ev, err = run("synthetic-reconnect-flood.jsonl")
check("reconnect flood exits cleanly", rc == 0, err[-400:])
flood = [e for e in ev if e.get("kind") == "action" and e.get("slot", "").startswith("rotate")]
check("connect guard drops the flood", len(flood) <= 2, f"{len(flood)} rotate actions")

# evtest text format with SYN_DROPPED
rc, ev, err = run("synthetic-evtest.txt")
check("evtest fixture parses and replays", rc == 0, err[-400:])

# malformed fixture is rejected with a non-zero exit, never crashes
with tempfile.NamedTemporaryFile("w", suffix=".jsonl", delete=False) as f:
    f.write('{"t":1,"type":2,"code":7,"value":\n')
    bad = f.name
p = subprocess.run([daemon, "--replay", bad, "--exit-after-replay", "--dry-run", "--no-context", "--no-overlay", "--no-tray"],
                   capture_output=True, text=True, timeout=30,
                   env=dict(os.environ, DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent"))
os.unlink(bad)
check("malformed fixture rejected", p.returncode == 2, f"rc={p.returncode}")

sys.exit(1 if failures else 0)
