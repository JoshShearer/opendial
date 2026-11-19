# D-Bus API (`io.github.joshshearer.opendial.Daemon1`)

| | |
|---|---|
| Bus | session |
| Name | `io.github.joshshearer.opendial` (D-Bus-activatable; starts `opendial.service`) |
| Object | `/io/github/opendial/Daemon` |
| Interface | `io.github.joshshearer.opendial.Daemon1` |

**Conventions:**
- Payloads are **JSON strings** (D-Bus type `s`), so the schema can grow without breaking D-Bus signatures (ADR-0005).
- Methods returning JSON report failure as `{"ok":false,"error":"…"}`.
- A breaking change will add `Daemon2` next to `Daemon1`.

Introspect it live:
```sh
busctl --user introspect io.github.joshshearer.opendial /io/github/opendial/Daemon
```

## Methods

### State and configuration
| Method | In → Out | Notes |
|---|---|---|
| `GetState` | → `s` | Device, battery, active profile and why it matched, active tool, menu, capabilities (injection backends, focus trackers, overlay, audio, media, brightness), suppression, dry-run flag |
| `GetConfig` | → `s` | Full config document (`opendial.config` v1) |
| `SetConfig` | `s` → `s` | Validated; `{ok, errors[], warnings[]}`. Written atomically with a backup. Rejected configs change nothing |
| `ValidateConfig` | `s` → `s` | Same validation, no write |
| `ResetToDefaults` | → `s` | The previous config stays as a backup |
| `RollbackConfig` | → `s` | Restores the newest backup. Repeated calls walk further back |
| `ImportProfile` | `s` → `s` | `opendial.profile` envelope. The result lists risky actions, which stay unapproved; an ID clash gives a renamed copy |
| `ExportProfile` | `s` id → `s` | Envelope |
| `ListTools` / `ListProfiles` | → `s` | Current menu tools (after availability filtering) / profiles |

### Control
| Method | Signature | Notes |
|---|---|---|
| `SetActiveTool` | `s` → `b` | Tool ID from the current menu |
| `LockProfile` / `UnlockProfile` | `s` → `b` / → | Manual profile override (highest precedence) |
| `OpenMenu` / `CloseMenu` | → | |
| `SetPaused` | `b` → | Suppresses all actions |
| `CompleteFirstRun` | → | Clears the first-run flag (wizard done) |
| `TestHaptics` | `s` (`click`\|`buzz`\|`rumble`) → `b` | false if hidraw is unavailable |
| `Reconnect` | → | Reopen the device |
| `RequestPortalPermission` | → | Ask for RemoteDesktop again |
| `Bluetooth` | `s` action, `s` path → `s` | `status`, `discover`, `stop-discovery`, `pair`, `trust`, `connect`, `disconnect`, `remove` (BlueZ) |

### Simulation and diagnostics
| Method | Signature | Notes |
|---|---|---|
| `Simulate` | `s` → `s` | `{"button":"down"\|"up"}`, `{"rotate":deg}`, `{"click":n}`, `{"longPress":true}`, `{"sequence":[…]}`. Range-checked |
| `SimulateContext` | `s` → `b` | `{"appId":"…","title":"…"}` pretends focus; `{}` stops |
| `ReportFocus` | `s` → | For compositor bridges (GNOME extension, KWin script): `{"appId","title","pid","workspace","pointer":{x,y}}` |
| `SetRawStreaming` | `b` → | Enables `RawEvent` signals |
| `GetDiagnostics` | `b` redact → `s` | |
| `ExportDiagnostics` | `s` path, `b` redact → `s` | Writes 0600, only under `$HOME`, `/tmp`, `$XDG_RUNTIME_DIR` |

### Approvals
| Method | Signature |
|---|---|
| `ListApprovals` | → `s` (pending risky actions: fingerprint, description, tool, profile) |
| `ApproveAction` / `RevokeApproval` | `s` fingerprint → `b` |

### Plugins
| Method | Signature | Notes |
|---|---|---|
| `RegisterApp` | `s` → `s` | `{"appId","name","matchAppIds":[…],"tools":[{"id","name","icon","description"}]}` → `{ok, approved, tools, errors}`. Bound to the caller's unique bus name |
| `UnregisterApp` | `s` appId → `b` | Only from the registering name |
| `ApprovePlugin` | `s` appId, `b` → `b` | Consent (GUI/CLI) |
| `PluginFeedback` | `s` appId, `s` → `b` | `{"osd":{"title","text","icon"},"haptic":"click"}`. Only from the registering name |

## Signals

| Signal | Args | Payload |
|---|---|---|
| `StateChanged` | `s` | Same as `GetState` (coalesced) |
| `ConfigChanged` | `s` | Config document |
| `GestureEvent` | `s` | `{"kind":"click"\|"long_press"\|"release"\|"rotate"\|"physical_down"\|"physical_up"\|"latch_changed","t":ms,"count":n,"degrees":d,"pressed":b}` |
| `RawEvent` | `s` | `{"t":us,"type":n,"code":n,"value":n}` (only while raw streaming is on) |
| `PluginEvent` | `s` appId, `s` | `{"toolId","slot":"rotate_cw"\|"rotate_ccw"\|"click"\|…,"steps":±n,"event":…}` |
| `MenuChanged` | `s` | `{"open":true, …menu model (items, highlight, page), "settings", "profileName", "handedness", "pointer"}` or `{"open":false}` |

## Loopback HTTP API (optional)

Enable it with `settings.integrations.httpApi.enabled`; the port defaults to
47311. Every request needs
`Authorization: Bearer $(cat $XDG_RUNTIME_DIR/opendial/http-token)`.
Requests that carry an `Origin` header or a foreign `Host` header are refused.

| Endpoint | |
|---|---|
| `GET /v1/state`, `GET /v1/tools` | as above |
| `POST /v1/tool` `{"id":…}` | select tool |
| `POST /v1/menu` `{"op":"open"\|"close"\|"toggle"}` | default `toggle` |
| `POST /v1/simulate` | same body as `Simulate` |
| `POST /v1/profile/lock` `{"id":…}`, `POST /v1/profile/unlock` | |
| `GET /v1/events` | Server-Sent Events: `event: gesture`, `event: tool`; 15 s keep-alive; at most 8 streams |

Examples: [plugins/examples/](../plugins/examples/).
