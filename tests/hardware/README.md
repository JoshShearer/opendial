# Physical hardware acceptance tests

These tests need a real Surface Dial and a person. They are what moves a row
in [docs/parity-matrix.md](../../docs/parity-matrix.md) to `verified`.
Automated tests (`ctest`) never count as hardware evidence.

**Record every run** in [VERIFICATION.md](../../VERIFICATION.md):
- date
- distribution + version, desktop + version, session type (Wayland/X11)
- kernel (`uname -r`), Qt, BlueZ (`bluetoothctl --version`)
- the Dial's modalias (`bluetoothctl info <addr> | grep Modalias`)
- pass, fail or blocked per step, with notes
- `opendialctl diagnose --out FILE` attached (redacted)

## 0. Preparation

1. Install the package for the distribution under test (not a dev build).
2. Log out and in once (user unit enabled, udev ACL applied).
3. `opendialctl status` → service running. `opendialctl --json devices` → `evdevReadable: true`, `hidrawWritable: true`.
4. In a second terminal, keep `opendialctl watch` running. It prints every gesture as JSON.

## 1. Protocol capture (once per hardware revision)

| # | Step | Expected |
|---|---|---|
| 1.1 | `cat /sys/bus/hid/devices/0005:045E:091B.*/report_descriptor \| xxd > descriptor.txt` | Descriptor saved; compare with [hardware-protocol.md](../../docs/hardware-protocol.md) |
| 1.2 | `opendialctl record dial-cw.jsonl --seconds 15`, turning exactly 10 detents clockwise (haptics on, 10° tool) | 10 rotate gestures of +10° in `watch`; file contains `REL_DIAL` positive values |
| 1.3 | Same counter-clockwise → `dial-ccw.jsonl` | Negative values |
| 1.4 | Record 5 clicks, one long press, one press-and-turn | `BTN_0` 1/0 pairs |
| 1.5 | Scrub (no identifying data is recorded, but check), add to `tests/replay-fixtures/` with a README line, and run `ctest -R replay` | Replays pass |

## 2. Windows parity (WP rows)

| # | WP | Step | Expected |
|---|---|---|---|
| 2.1 | 01 | Pair from the app's Device page (Forget first). Turn off (remove a battery), wait 30 s, put it back, press | Connected within ~5 s, `opendialctl watch` shows events, no duplicate actions |
| 2.2 | 01 | Suspend the computer 1 min, resume, press | Reconnects; no burst of stale rotation (connect guard) |
| 2.3 | 01 | Walk out of range (~15 m) for 1 min, come back, press | Reconnects |
| 2.4 | 01 | Reboot, log in, press | Works without opening the app |
| 2.5 | 02 | Volume tool: turn 1, 10, 100 detents each way; then fast back-and-forth | Volume changes match direction and amount; no lost reversal |
| 2.6 | 03 | Click 20 times at normal speed with the Media tool | Exactly 20 play/pause toggles, no long-press menu |
| 2.7 | 04 | Hold 0.5 s | Menu appears in ≤150 ms after threshold (eyeball; use screen recording to measure) |
| 2.8 | 04 | Turn while the menu is open; click | Highlight follows each detent with a haptic tick; click selects; menu closes; selected tool active; no action leaked to the app |
| 2.9 | 05 | Bind turn-while-pressed to Ctrl+Tab on a tool; press and turn | Only the pressed binding fires, never the normal rotation |
| 2.10 | 06 | Each built-in tool: Volume, Scroll (browser page), Zoom (browser), Undo (text editor), Brightness (laptop panel), Media (music player) | Each works; brightness shows "not available" on desktops without a controllable panel |
| 2.11 | 07 | Custom tool: CW = `]`, CCW = `[`, click = `Ctrl+Shift+N` in a text editor; also a non-US layout | Correct characters on both layouts |
| 2.12 | 08 | Add, remove, reorder, disable a tool, change icon; save | Menu reflects it immediately |
| 2.13 | 09 | Haptics off globally → turn | No clicks felt; status shows off |
| 2.14 | 10 | Tool detent 10° vs 36° vs 1° | Felt clicks every 10°, 36°; none at 1° (clamped, UI says so) |
| 2.15 | 11 | Sensitivity 2×, invert, continuous mode | Behaviour changes as configured |
| 2.16 | 12 | Focus a browser, Krita, a terminal in turn | Profile switches within ~200 ms; Dashboard shows the app ID and reason |
| 2.17 | 13 | Menu shows icons and names legibly at 100 % and 200 % scale | |
| 2.18 | 14 | Change settings, `systemctl --user restart opendial`, log out/in, reboot | Settings and selected tool persist |
| 2.19 | 15 | Placement: pointer, centre, saved; second monitor; 150 % fractional scaling | Correct position on the right monitor |
| 2.20 | 16 | Left-handed + "beside pointer" | Menu opens to the right of the pointer |
| 2.21 | 19 | Tool with haptics off and one with them on; switch between them | Haptics follow the tool |
| 2.22 | 21 | Start/stop a music player with a `requires: mpris` tool configured | Tool appears/disappears (menu closed) |
| 2.23 | — | Lock the screen, turn the Dial with a key-sending tool | Nothing typed into the lock screen; media/volume still work if allowed |

## 3. Reliability (smaller practical set on hardware, SRS §14.7)

| # | Step | Expected |
|---|---|---|
| 3.1 | 50 connect/disconnect cycles (battery out/in, or `bluetoothctl disconnect/connect`) | Every cycle reconnects; RSS of `opendiald` stable (`ps -o rss`) |
| 3.2 | 200 menu open/select cycles | No stuck overlay, no stuck keys |
| 3.3 | 8 h idle with the Dial connected | CPU < 1 % (`pidstat -p $(pidof opendiald) 60`), no RSS growth |
| 3.4 | Kill `opendiald` with SIGKILL while holding a modifier-sending tool | No stuck modifier after restart (systemd restarts it) |

## 4. OS matrix to cover

- Ubuntu 24.04 GNOME Wayland
- Ubuntu 24.04 GNOME on Xorg
- Pop!_OS 24.04 COSMIC
- Arch KDE Plasma 6 Wayland
- Arch KDE Plasma X11
- Arch GNOME Wayland (optional)
- Hyprland/Sway (reference, optional)

## Reliability logger (physical Dial)

`tests/hardware/reliability-log.sh [OUT.csv] [INTERVAL]` samples the installed
service every few seconds: Dial connected/grabbed, opendiald PID, RSS and CPU
time. Run it during the reconnect, out-of-range, suspend/resume and 8 h idle
checks; on Ctrl+C it prints connect/disconnect counts, suspend gaps, daemon
restarts and the RSS delta. It only reads, so it is safe against the real
session. Only a run with the physical Dial counts as evidence.
