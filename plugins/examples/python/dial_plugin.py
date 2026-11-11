#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Minimal OpenDial plugin in Python (PyGObject / Gio, no other dependencies).

It registers one Dial tool, "Counter". When you select it in the radial menu
and turn the Dial, OpenDial sends PluginEvent signals here; the plugin keeps a
number and shows it in OpenDial's on-screen display.

The first time, approve the plugin in OpenDial → Integrations → Plugins (or
`opendialctl plugin allow org.example.counter`). Tools disappear when this
process exits.

Run:  python3 dial_plugin.py
"""
import json
import signal
import sys

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

BUS = "io.github.opendial"
PATH = "/io/github/opendial/Daemon"
IFACE = "io.github.opendial.Daemon1"
APP_ID = "org.example.counter"

conn = Gio.bus_get_sync(Gio.BusType.SESSION, None)
count = 0


def call(method, *args, sig="s"):
    reply = conn.call_sync(BUS, PATH, IFACE, method, GLib.Variant(f"({sig})", args), None,
                           Gio.DBusCallFlags.NONE, 5000, None)
    return reply.unpack()[0]


def on_plugin_event(_conn, _sender, _path, _iface, _signal, params):
    global count
    app_id, payload = params.unpack()
    if app_id != APP_ID:
        return
    ev = json.loads(payload)
    # ev = {"toolId": "counter", "slot": "rotate_cw"|"rotate_ccw"|"click"|..., "steps": +/-n, "event": ...}
    if ev["slot"] in ("rotate_cw", "rotate_ccw"):
        count += ev["steps"]
    elif ev["slot"] == "click":
        count = 0
    print(f"{ev['slot']:<12} steps={ev['steps']:+d} count={count}", flush=True)
    call("PluginFeedback", APP_ID, json.dumps({"osd": {"title": "Counter", "text": str(count), "icon": "value"}}), sig="ss")


def main():
    conn.signal_subscribe(BUS, IFACE, "PluginEvent", PATH, None, Gio.DBusSignalFlags.NONE, on_plugin_event)
    reg = {
        "appId": APP_ID,
        "name": "Counter example",
        "tools": [{"id": "counter", "name": "Counter", "icon": "value",
                   "description": "Turn to count, click to reset"}],
        # "matchAppIds": ["org.kde.krita"],  # only show the tool while this app is focused
    }
    result = json.loads(call("RegisterApp", json.dumps(reg)))
    if not result.get("ok"):
        sys.exit(f"registration failed: {result}")
    print("registered:", json.dumps(result), flush=True)
    if not result.get("approved"):
        print(f"approve it: opendialctl plugin allow {APP_ID}", flush=True)
    loop = GLib.MainLoop()
    try:  # GLib >= 2.80
        gi.require_version("GLibUnix", "2.0")
        from gi.repository import GLibUnix
        add_signal = GLibUnix.signal_add
    except (ImportError, ValueError):
        add_signal = GLib.unix_signal_add
    for sig in (signal.SIGINT, signal.SIGTERM):
        add_signal(GLib.PRIORITY_DEFAULT, sig, loop.quit)
    loop.run()
    call("UnregisterApp", APP_ID)


if __name__ == "__main__":
    main()
