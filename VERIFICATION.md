# Verification log

Only what was actually run is listed here, with where it ran. Simulated and
replayed input is never reported as hardware success (SRS §17.6).

## 2026-11-10: release candidate 0.9.0

### Automated tests (reference machine)

Environment: Omarchy (Arch-based), kernel 7.2.15, GCC 16.2.1, CMake 4.4.4,
Qt 6.12. Ran `QT_QPA_PLATFORM=offscreen ctest --test-dir build`.

**Result: 13/13 suites pass.**

Unit suites (86 test functions):

| Suite | Tests | Covers |
|---|---|---|
| test_actions | 7 | action validation, fingerprints, risk classes, macro limits |
| test_config | 16 | defaults, validation, migration, atomic writes, backups, multi-level rollback, symlink refusal, corrupt/invalid/newer handling, import/export |
| test_decoder | 7 | evdev framing, SYN_DROPPED, connect guard, HID report decode, fixture parsing |
| test_gesture | 17 | click / double / long press / hold-rotate / latch / debounce |
| test_keychord | 7 | chord and sequence parsing, layouts |
| test_profiles | 13 | matching, precedence, Flatpak IDs, folders, menu composition |
| test_radial | 10 | ring layout, paging, wrap, highlight |
| test_rotation | 11 | stepping, coalescing, acceleration, haptics policy, report encoding |
| test_theme | 7 | Omarchy colors.toml parsing, palette resolution (system/forced light/dark, accent), WCAG text on accent, live theme switch through a replaced symlink (watcher re-armed) |

Integration suites (each runs on a private bus, `dbus-run-session`):

| Suite | Tests | What runs |
|---|---|---|
| test_engine | 22 | Full engine with simulated device input: long press → menu → rotate → click select, wrap, no leaks while open, built-in tools, custom CW/CCW/click tool, app profile switching, remember/lock, pause, invalid config, rollback, approvals, plugin consent, restart persistence, double-click, `requires: mpris`, example profiles, `if` operators, OSC approval, garbage simulation |
| test_dbus_cli | many checks | Real `opendiald --dry-run --no-device` + `opendialctl`. Covers status, tools, profiles, context, lock, config get/validate/set, import/export, diagnostics export (privacy, mode 0600, refused outside allowed roots and on a `..` escape, redacted log), pause/resume, single instance, SIGTERM shutdown |
| test_plugins_http | many checks | Python D-Bus plugin example end to end; HTTP API auth (token, Host, Origin); Node SSE example |
| test_replay | 4 | Fixture replay at real speed, reconnect flood guard, evtest format, malformed fixture → exit code 2 |

The three script-based suites print 83 `PASS` lines in total.

### Sanitizers

Debug build with `-fsanitize=address,undefined`. All 12 suites pass with no
ASan or UBSan reports (leak detection off: Qt's global caches). The CI job
`sanitizers` repeats this.

### Packages (clean containers, nothing installed on the host)

| Target | Command | Result |
|---|---|---|
| Ubuntu 24.04 (Qt 6.4.2) | `packaging/docker/deb.sh` | Build OK. `dh_auto_test`: **12/12 pass**. lintian: 2 warnings (`initial-upload-closes-no-bugs`, a non-executable example script; the second is fixed since). `apt install ./opendial_0.9.0-1_amd64.deb` OK. Files in place; `opendialctl --version` OK |
| Arch Linux (latest, Qt 6.12) | `packaging/docker/arch.sh` | `makepkg` with `check()`: **12/12 pass**. namcap PKGBUILD: clean. namcap package: only the expected warnings (see docs/installation/arch.md). `pacman -U` OK; `opendialctl --version` OK |

The Ubuntu run found a real bug, fixed in b5b80db. As root with
`XDG_RUNTIME_DIR` unset, the diagnostics export allowed any path.

### Real Wayland session (nested Hyprland on the reference machine, simulated input)

- The layer-shell radial menu appears at the pointer. The highlight follows the simulated rotation, click selects, the menu closes and the tool becomes active. OSD shown. Placement was checked against the nested compositor's cursor position.
- The configuration app renders all pages. It was operated in the nested session.
- **Key injection E2E:**
  - Setup: a real (not dry-run) daemon with a custom tool (CW = `a`, CCW = `Shift+b`, click = `Return`), and a foot terminal running `cat > file`.
  - Simulated input: +30°, −20°, click. The file contained `aaaBB\n` (`zwp_virtual_keyboard_v1`, layout read from Hyprland).
  - This run found and fixed a bug: Hyprland reports unset keyboard options as `[[EMPTY]]`, which broke the keymap, so all key injection on Hyprland failed.
  - Pointer/scroll delivery (`zwlr_virtual_pointer_v1`) is advertised but not yet observed.

### System theme (nested Hyprland on the reference machine)

- With the Omarchy theme active, the radial menu, the OSD and the settings app took the theme's background, foreground and accent (`#b59790`); the highlighted icon is drawn in the theme background, which has more contrast on that accent. Forcing `settings.menu.theme = light` switched the menu to the light palette. Screenshots were checked by eye.
- The GNOME/KDE path (portal `org.freedesktop.appearance`) is implemented but has not been run on those desktops.

### Performance (reference machine, dry run, overlay on)

- Idle RSS: 65 MB.
- Idle CPU: 0 scheduler ticks over 10 s.
- Event-to-action latency is recorded per action (`latencyUs` in the event log). It has not yet been measured with hardware input.

### Physical hardware (reference machine, Hyprland 0.56.2, 2026-11-10)

Surface Dial `usb:v045Ep091Bd0108`, kernel 7.2.15, Qt 6.12, development build
(`build/`), throwaway config. udev rule installed by the user (hidraw writable).

| Check | Result |
|---|---|
| Pair and trust through BlueZ | **pass**: Paired: yes, Trusted: yes |
| Rotation capture | **pass**: `REL_DIAL` ±1 per detent, one action per detent |
| Click / long press | **pass**: `BTN_0` down/up; hold opens the menu |
| Menu: turn to highlight, click to select | **pass**: haptic tick per item, the selected tool stays active, no action leaked to the app |
| Press-and-turn | **pass** when turning starts before the long-press threshold |
| Haptics | **pass**: ticks felt; feature report read back (resolution 12 requested = 12 applied) |
| Reconnect after a battery pull | **pass**, no duplicate actions |
| Built-in tools, real mode | **pass** (tester): Volume, Scroll, Zoom, Undo, Brightness, Media |
| Direction | **pass** (tester): volume and workspaces move the expected way for each direction |
| Workspaces | **fail, then pass**: Hyprland 0.56 rejects `dispatch workspace e+1` (its dispatch is Lua now) and the error was counted as success. Fixed in cdaec8c; the tester confirmed switching works |
| System theme | **pass** (tester): the menu followed the Omarchy theme, including live theme switches |
| Suspend/resume, out of range, reboot, soak (§3) | **not run** |

**Hardware verdict: core Dial behaviour verified on Hyprland.** The SRS target desktops (GNOME, KDE, COSMIC, X11) are still untested.

### Not run anywhere yet

- GNOME (extension, portal), KDE (KWin script, portal), COSMIC, X11 sessions.
- Krita plugin inside Krita.
- Rust example against a live daemon. It does compile (`cargo build`, rust:1-slim container).
