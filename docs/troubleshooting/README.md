# Troubleshooting

Start here:
```sh
opendialctl status                 # device, profile, tool, backends at a glance
opendialctl diagnose --out ~/opendial-diagnostics.json   # redacted report to attach to issues
journalctl --user -u opendial -b   # service log
```
The app's **Diagnostics** page shows the same, plus a live event log and a
permissions check.

## The Dial does nothing

| `opendialctl status` says | Fix |
|---|---|
| `opendiald is not running` | `systemctl --user enable --now opendial`. If it exits at once, see the journal |
| Device `not found`, BlueZ `connected=false` | The Dial sleeps after a few minutes. Press it or turn it to wake it. Still nothing: on the Device page, Connect. If it never connects: Forget, then pair again (hold the pairing button 5 s) |
| Device found, `evdev not readable` | The udev rule isn't applied: reconnect the Dial, or `sudo udevadm trigger --action=change`. Was the package installed? (`ls /usr/lib/udev/rules.d/70-opendial*`) |
| Events arrive but nothing happens | Is OpenDial paused (`opendialctl resume`)? Is the session locked? Look at `suppressed` in status. Is the bound action `permission-needed`? See "Keys don't work" below |
| A burst of rotation after waking | Expected: buffered turns. The connect guard drops them; raise `device.connectGuardMs` if some still get through |

## Keys or scrolling don't work (Wayland)

- **GNOME:** keys go through the RemoteDesktop portal. If you dismissed the prompt, open *Integrations → Injection → Allow*, or run `opendialctl portal`.
- **KDE:** the same portal prompt appears if KWin does not offer the virtual-keyboard protocol.
- **Wrong characters:** set `settings.injection.keyboardLayout` to your layout (e.g. `de`). `auto` reads the session's layout.
- Which backend is in use: `opendialctl status`, or *Integrations → Injection*.
- **Last resort:** uinput. See the threat model (T7) before enabling it.

## A command action does nothing

- `opendialctl status` shows the last outcome under `lastAction`: `failed to start` (program not found or not executable), `finished with exit code N`, `was killed by a signal` or `timed out` (default 10 s, `timeoutMs`). Failures are also notified and logged to the journal.
- `opendialctl diagnose --no-redact` lists every step in `eventLog` (`kind: command`). An action's `commandId` matches the `id` of its outcomes.
- Only one process per command action runs at a time. `overlap` decides what happens to requests that arrive meanwhile: `coalesce` (merged into one further run, the default for turning), `queue` (up to four wait, the default for presses) or `reject`.

## Per-app profiles don't switch

- Dashboard → *Profile* shows the focused app ID and why a profile matched. If the app ID is `(unknown)`, your desktop gives OpenDial no focus information:
  - **GNOME:** install and enable the extension ([installation](../installation/README.md)).
  - **COSMIC:** not supported yet. Lock a profile manually.
- Test a profile without switching windows: `opendialctl context org.kde.krita`, and `opendialctl context clear` to stop.
- Match on the app ID shown in the Dashboard. For Flatpaks that is the reverse-DNS ID (`org.mozilla.firefox`).

## Haptics don't work

- `opendialctl test haptics`. "unavailable": hidraw isn't writable. The udev rule is missing or not yet applied (see above).
- Haptics are off below 2° per step, on purpose (Microsoft's guidance). Coarser steps on the *Rotation & Haptics* page turn them back on.

## The radial menu doesn't appear or appears in the wrong place

- Hold for longer than the threshold (*Settings → Gestures → Long press*, default 500 ms).
- **GNOME and X11:** the menu is a normal window, so another always-on-top window can cover it.
- **"At the pointer" opens it in the centre:** the desktop doesn't share the pointer position. See [platform capabilities](../platform-capabilities.md). Choose *Saved position* or a display instead.
- A menu that never closes: press Escape, or wait for the timeout. `opendialctl menu close` always works.

## Volume / brightness / media

- **Volume:** needs `wpctl` (WirePlumber) or `pactl`.
- **Brightness:** laptop panels work through logind. External monitors need `ddcutil`, the `i2c-dev` module and DDC/CI enabled on the monitor. Then enable it under *Integrations → System*.
- **Media:** needs an MPRIS player. Browsers expose one per playing tab.

## Conflicts with other Surface Dial daemons

Stop other tools that read the Dial, such as `surface-dial-linux`,
input-remapper presets for it, or a custom `evdev` script. With
`device.grabDevice` on, OpenDial takes the device exclusively, so the other tool
gets nothing; with it off, both act on every turn. You do not need to disable
BlueZ or the kernel HID driver.

## Config problems

- **"configuration is read-only":** the file is invalid or from a newer version. OpenDial never overwrites it. Fix it, or `opendialctl config rollback`, or `opendialctl config reset` (keeps a backup).
- **Undo a bad change:** `opendialctl config rollback`. Repeat to go further back (5 versions are kept).
- **Validate a file before using it:** `opendialctl config-validate FILE`.

## Reporting a bug

Attach `opendialctl diagnose --out FILE` (redacted by default) and, for input
problems, a short recording: `opendialctl record dial.jsonl --seconds 10`. Say
which distribution, desktop and session type you use.
