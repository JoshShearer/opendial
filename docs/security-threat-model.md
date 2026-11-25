# Security threat model

**Last updated:** 2026-11-10 (0.9.0). Scope: `opendiald`, `opendial`,
`opendialctl`, the packaged udev/systemd/D-Bus files, the plugin protocols
and the HTTP API.

## Assets

1. The user's keyboard/pointer identity. Injected input acts as the user in every app.
2. The user's files and command execution. Macros can launch programs.
3. Privacy: focused window titles and app IDs, typed key sequences, Bluetooth addresses.
4. Device access: hidraw/evdev nodes.

## Trust boundaries

| Boundary | Trusted side | Untrusted side |
|---|---|---|
| Kernel device nodes | kernel | Dial firmware / radio input (malformed reports) |
| Session D-Bus | the user's own processes | — (same uid: see T2) |
| Loopback HTTP | token holders | browsers and web pages, other local users |
| Profile files | the user's own edits | imported/shared profiles |
| Plugins | approved apps | unapproved apps on the session bus |

**Out of scope:** malware already running as the same user. It can read
`~/.config/opendial`, ptrace the daemon and inject input by other means.
OpenDial does not try to defend the user from themselves. It only avoids
*widening* what such code can do. The exception is uinput, which widens it
(see T7).

## Threats and mitigations

| ID | Threat | Mitigation | Test |
|---|---|---|---|
| T1 | **Imported profile runs commands** (shared profile with `command`/`launch`/`dbus` actions) | Risky actions are never run unapproved. Approval is per **action fingerprint**: a hash of the exact action JSON, so a changed argv needs a new approval. Approving a `macro`, `if` or `value` action covers the actions inside it, but only there: the same command elsewhere needs its own approval. The request lists every risky action inside, with command lines in full and the range `{value}` can take. Arguments are quoted, control and invisible characters (such as bidi overrides) are written as `\uXXXX`, and neither the window nor notifications render markup in it. Import lists the risky actions and leaves them unapproved | `riskyActionNeedsApproval`, `profileImportExport`, `describeApproval`, `describeApprovalIsComplete`, `test_commands` |
| T2 | **Another same-user process drives the D-Bus API** (simulates gestures, changes config) | Session bus is per-user. Calls cannot approve risky actions silently: `ApproveAction` exists for the GUI/CLI, which the user drives. Same-uid code is out of scope (see above) | — |
| T3 | **ReportFocus spoofing**: any session process can claim "Firefox is focused" | It can only switch which **profile** is active; profiles contain the user's own actions, and risky ones still need approval. Secure-input detection uses the tracker's own data first. A spoofed "not secure" report cannot unlock injection while logind reports the session locked | `appProfileSwitching` |
| T4 | **Over-broad device permissions** | udev rule matches only `KERNELS=="0005:045E:091B.*"` (hidraw) and bus 0005 + vendor 045e + product 091b (evdev), with `TAG+="uaccess"`: only the active seat's user, revoked on logout/switch. No group, no `MODE="0666"`, never all HID devices | rule reviewed; `udevadm test` in the hardware checklist |
| T5 | **Malformed or oversized input** from the device or fixtures | Decoder bounds-checks reports, handles `SYN_DROPPED`, counts malformed frames. Simulation JSON is range-checked (rotation ±3600°, click counts, sequence length) | `test_decoder`, `simulationRejectsGarbage`, `test_replay` malformed fixture |
| T6 | **Config file attacks** (symlink to another file, partial writes, corrupt JSON) | Atomic `QSaveFile` writes with mode 0600; refuses to write through a symlink; corrupt file kept as `.corrupt` with fallback to backup; invalid or newer-version configs are loaded read-only and never overwritten | `refusesSymlinkTarget`, `corruptFallsBackToBackup`, `invalidKeepsUserFileReadOnly`, `newerVersionNotTouched` |
| T7 | **`/dev/uinput` fallback**: whoever can open uinput can create a virtual keyboard that types into *any* app, terminals included, and on some desktops the lock screen | Off by default. It needs **both** the user opt-in (`injection.allowUinput`) **and** an admin installing `71-opendial-uinput-optin.rules.example`. That rule gives `uaccess` to the active seat only. Not shipped active by any package. Injection is suppressed while locked or a secure prompt is focused. The UI explains the trade-off before enabling | manual |
| T8 | **Rogue plugin** registers tools that look like system tools | Plugin tools are namespaced `plugin:<appId>:<toolId>`, hidden until the user approves the app, and cannot replace built-in tools. Events go only to the registering bus name. Tools vanish when that name disconnects. `PluginFeedback` is accepted only from the registering name | `pluginToolsNeedConsent`, `test_plugins_http` |
| T9 | **Local web page or DNS-rebinding attack on the HTTP API** | Off by default. Binds 127.0.0.1 only. Bearer token (random, `$XDG_RUNTIME_DIR/opendial/http-token`, 0600). Requests with a foreign `Host` or any `Origin` header are rejected (browsers always send Origin on cross-site fetches). Body size limit (413), 5 s request deadline, at most 8 event streams (429). The API cannot approve actions, change config or run commands: state, tools, select tool, menu, simulate, profile lock | `test_plugins_http` auth checks |
| T10 | **Privacy leak through diagnostics** | Exports are redacted by default: BT address, window titles, variables and action details (only the action *type* remains). Files are written 0600, only under `$HOME`, `/tmp` or `$XDG_RUNTIME_DIR` after canonicalising the parent (no `..` or symlink escape). The daemon never records keystrokes; it only emits configured ones | diagnostics checks in `test_dbus_cli` |
| T11 | **Shell injection through command actions** | Commands are an argv array run directly (`QProcess`, no shell). There is no string-to-shell path. Timeout with terminate → kill. A command waiting for an earlier run to finish is dropped on lock, secure focus, pause or disconnect, and its approval is checked again before it starts. Each step of a macro checks the macro's approval again | `test_actions`, `test_commands` |
| T12 | **Input on lock screen / password prompts** | logind `LockedHint`, ScreenSaver `ActiveChanged`, and a secure-window list (pinentry, polkit, keyring, portal dialogs) suppress injection and close the menu. Only media/volume may run while locked (setting) | `pauseSuppressesActions` |
| T13 | **Stuck modifier keys** after crash/stop | Every injector releases all held keys on suppression, backend switch and shutdown (SIGTERM via signalfd → clean exit) | "clean shutdown on SIGTERM" |
| T14 | **OSC to remote hosts** used for exfiltration | Loopback OSC targets run directly; any other host needs approval like a command | `oscRemoteNeedsApproval` |
| T15 | **Privilege escalation via the package** | No setuid, no root daemon, no polkit helper. Maintainer scripts only reload udev rules. The service unit is a user unit with `NoNewPrivileges`, `RestrictSUIDSGID`, `LockPersonality`, `RestrictRealtime`, `ProtectKernelTunables/Modules`, `ProtectControlGroups`. Filesystem protection is not used, because the service must read the user's files for launch actions | package lint (namcap, lintian) |
| T16 | **Event floods / resource exhaustion** | Event log is a bounded ring. Rotation coalescing preserves total delta without unbounded queues. Haptic writes run on a separate thread with coalescing. HTTP limits as in T9. At most one process per configured command (whatever `{value}` is), with at most four requests waiting | `test_rotation`, reconnect flood fixture, `test_commands` |

## Residual risks

- Wayland portals remember the RemoteDesktop grant per app ID. Anything that can impersonate `io.github.joshshearer.opendial` to the portal gets the same grant. That is the portal's model.
- The GNOME Shell extension and the KWin script run inside the compositor with its privileges. They are small and only report the focus app ID, title and pointer.
- The Krita plugin and the plugin examples run inside the host apps with those apps' privileges.

## Reporting

See [SECURITY.md](../SECURITY.md).
