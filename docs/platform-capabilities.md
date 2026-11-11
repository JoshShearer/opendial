# Platform capabilities

**Last updated:** 2026-11-10. This page describes what each desktop allows
an unprivileged per-user service to do, and which OpenDial backend uses it.
"Tested" means run on that desktop. Everything else is a design expectation,
taken from the protocol or the compositor's documentation. The daemon reports
its live state as `supported`, `permission-needed`, `not-supported` or
`fallback-available` on the Integrations page and in `opendialctl status`
(SRS §7).

## Summary

| Capability | X11 (any) | GNOME Wayland | KDE Plasma Wayland | COSMIC | wlroots / Hyprland / Sway |
|---|---|---|---|---|---|
| Read the Dial (evdev) | udev uaccess | same | same | same | same |
| Haptics (hidraw) | udev uaccess | same | same | same | same |
| Keyboard injection | XTest | RemoteDesktop portal (one-time permission; restore token kept) | `zwp_virtual_keyboard_v1` if offered, else the portal | portal if xdg-desktop-portal-cosmic offers RemoteDesktop (untested) | `zwp_virtual_keyboard_v1` (**tested** on Hyprland) |
| Pointer / scroll injection | XTest | RemoteDesktop portal | `zwlr_virtual_pointer_v1` if offered, else the portal | portal (untested) | `zwlr_virtual_pointer_v1` (**tested** on Hyprland) |
| Focused app detection | `_NET_ACTIVE_WINDOW` + `WM_CLASS` | Bundled GNOME Shell extension → `ReportFocus` | KWin script loaded over D-Bus (`org.kde.kwin.Scripting`) | no protocol known; manual profile lock | Hyprland IPC, Sway IPC, or `zwlr_foreign_toplevel_manager_v1` (Hyprland **tested**) |
| Pointer position (menu at pointer) | `XQueryPointer` | GNOME extension `GetPointer` | KWin script `workspace.cursorPos` | none → screen centre or saved position | Hyprland IPC (**tested**) |
| Radial overlay | frameless always-on-top window | frameless window (no layer-shell in Mutter) | `zwlr_layer_shell_v1` via LayerShellQt | layer-shell | layer-shell (**tested**) |
| Volume | wpctl → pactl | same | same | same | same |
| Media | MPRIS (D-Bus) | same | same | same | same |
| Brightness | logind `SetBrightness` on `/sys/class/backlight`; brightnessctl; ddcutil (opt-in) for external monitors | same | same | same | same |
| Lock-screen suppression | logind `LockedHint` | same + GNOME ScreenSaver | same | same | same |

Last-resort injection fallback on every desktop: `/dev/uinput`. It is used
only after the user opts in **and** an admin installs
`71-opendial-uinput-optin.rules.example` (see the threat model, T7). It is
never on by default.

## Backend selection

The keyboard and the pointer pick a backend separately, in this order:
1. The backend set in `settings.injection.backend` (default `auto`).
2. Otherwise: native Wayland virtual input, then the RemoteDesktop portal, then XTest, then uinput (only if opted in).

When no backend can inject, key and scroll bindings show
`permission-needed` or `not-supported` in the editor. When run, they are
logged as skipped, not as done. Volume, media, brightness, OSC, D-Bus and
plugin actions do not need injection.

## Focus detection details

- **App identity:** the Wayland `app_id` or the X11 `WM_CLASS` class. Flatpak IDs are matched as-is (`org.mozilla.firefox`) and also by their last component in lower case. Title matching is an extra filter (`titleRegex`), never the only rule (SRS §8).
- **Secure input:** the profile switcher ignores password prompts and the polkit, keyring and portal dialogs (a glob list in `ContextManager`). Injection is suppressed while one has focus or the session is locked.
- **No tracker:** the Dashboard says why, and offers profile lock, `opendialctl profile lock ID`, or a key binding to switch.

## Known upstream limits

- GNOME Wayland (Mutter) has no virtual-keyboard or layer-shell protocol for clients. Injection works only through the portal, so GNOME shows a permission prompt once. Focus and pointer need the Shell extension.
- No Wayland desktop gives an ordinary client the global pointer position. The pointer placement on Wayland comes from compositor IPC or a bridge.
- COSMIC: no focus-reporting protocol for clients is known to us at this date. Status `blocked-upstream` until tested otherwise.
- Surface Studio on-screen placement needs a digitizer that reports the Dial's contact. Not available here (WP-20).
