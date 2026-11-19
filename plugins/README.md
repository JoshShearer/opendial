# Plugins and integrations

| Path | What | Tested |
|---|---|---|
| `gnome-shell/opendial@joshshearer.github.io` | GNOME Shell 45–49 extension: reports the focused app and the pointer position to OpenDial | Syntax-checked as an ES module (`node --check`); **not yet run inside GNOME Shell** |
| `krita/` | Krita Python plugin: Dial tools for brush size, opacity, canvas zoom and rotation | **Not run inside Krita** (Krita was not available) |
| `examples/python/dial_plugin.py` | Minimal D-Bus plugin (PyGObject) | Yes: `tests/integration/test_plugins_http.py` |
| `examples/js/dial-events.mjs` | Node 18+ client of the loopback HTTP API and SSE event stream | Yes: same test |
| `examples/rust/` | zbus 4 plugin | Compiles (`cargo build` in the `rust:1-slim` container, 2026-11-10); not yet run against the daemon |

Protocol: [docs/dbus-api.md](../docs/dbus-api.md) (RegisterApp / PluginEvent /
PluginFeedback) and ADR-0008. A plugin's tools appear in the menu only after
the user allows the app (*Integrations → Plugins*, or
`opendialctl plugin allow APPID`).
