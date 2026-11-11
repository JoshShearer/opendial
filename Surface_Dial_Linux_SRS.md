# Surface Dial Linux — Production Driver, Radial UI & Automation Suite

**Software Requirements Specification (SRS) and Claude Code execution brief**  
**Version:** 1.0 — 2026-11-10  
**Target systems:** Ubuntu LTS, Pop!_OS (including COSMIC), Arch Linux; GNOME, KDE Plasma, COSMIC; Wayland and X11  
**Quality bar:** At least Microsoft Surface Dial's Windows configuration features where underlying Linux hardware and compositor capabilities permit; superior profile programmability, automation, portability, and diagnostics.  
**Project working name:** OpenDial (provisional; check trademark/name collisions before publication).

---

## 0. Mission and non-negotiable outcomes

Build an open-source, installable, fully configurable, reliable Linux control suite for Microsoft's physical **Surface Dial** Bluetooth Low Energy (BLE) rotary controller. The product must feel native on Ubuntu, Pop!_OS, and Arch, have a polished accessible GUI, and reproduce **all feasible Windows-level features**, including default system wheel tools, configurable custom tools, directional actions, press actions, radial menu, haptic options, configurable sensitivity, and context-aware application integration. It must extend Windows capability with unlimited nested tools, macro sequences, per-app/per-window profiles, portable configuration, and an optional plugin API.

**Not acceptable:** a proof-of-concept Python listener; mapping left/right to volume only; hard-coded modes; using `sudo` to run the graphical app; X11-only shortcuts while claiming Wayland support; background daemon requiring a terminal; silent loss of haptics; unsupported claims about Surface Studio-specific placement; one distribution working while the others fail; passing unit tests without testing real hardware.

**Success definition:** a user pairs the Dial, installs the package, opens the graphical application, tests live rotation/click/haptics, configures tools and shortcuts, uses radial menus and automatic per-app profiles, restarts/logs out/reconnects, and has settings persist without elevated privileges or manual filesystem edits.

## 1. Hardware and Windows parity inventory

### 1.1 Ground-truth Windows baseline

Microsoft's official documentation describes these capabilities:

1. BLE connection; 2 AAA batteries; bidirectional continuous rotation; mechanical click; press-and-hold; rotation while pressed; dynamically programmable tactile feedback.
2. Press-and-hold opens a wheel/radial tool menu. Rotate to select; click to activate. When menu is closed, rotation and click act on the selected tool.
3. Built-in tools: volume/mute, scrolling, zoom, undo/redo, brightness, media controls; and application-specific controls when applications expose integrations.
4. Windows Wheel settings allow users to add **custom tools**, individually assigning arbitrary keyboard shortcuts for clockwise rotation, counter-clockwise rotation, and click.
5. Programmable rotation event resolution/sensitivity; automatic haptic feedback enable/disable and interval behavior; context-specific tools and haptics. Windows documentation recommends avoiding excessive haptic feedback at fine resolution.
6. User can customize wheel menu tools and preference settings (including vibration); Windows also describes dominant-hand preference.
7. Apps can customize tools, tool availability, command icons, rotation and click behavior; stylus/inking workflows include brush selection, thickness, and ruler manipulation.
8. Specialized **Surface Studio** on-screen behavior requires a compatible capacitive touch digitizer to report the Dial's contact position. This is **not** a generic BLE feature; treat as a separately investigated compatibility capability, not a promise for ordinary Linux displays.

**Sources (revalidate during implementation):**
- https://support.microsoft.com/en-us/surface/accessories/dial/meet-surface-dial
- https://support.microsoft.com/en-us/surface/accessories/dial/change-surface-dial-settings
- https://learn.microsoft.com/en-us/windows/apps/develop/input/windows-wheel-interactions
- https://learn.microsoft.com/en-us/windows/apps/design/input/radialcontroller-walkthrough
- https://www.microsoft.com/en-us/surface/accessories/surface-dial

### 1.2 Feature-parity acceptance matrix

Every row MUST be tracked in `docs/parity-matrix.md` with **Windows baseline**, **Linux implementation**, **X11 status**, **GNOME Wayland**, **KDE Wayland**, **COSMIC Wayland**, **physical hardware evidence**, **limitations**, and **release gate**.

| ID | Feature | Priority | Observable acceptance |
|---|---|---|---|
| WP-01 | BLE pairing and reconnect | P0 | Pair through BlueZ/system Bluetooth; reconnect after reboot/sleep/out-of-range; no duplicate bindings |
| WP-02 | Clockwise and counter-clockwise rotation | P0 | Direction and relative delta correct at multiple speeds, including rapid direction reversal |
| WP-03 | Short-click | P0 | Click recognized once; no false long-press or double events |
| WP-04 | Press-and-hold opens radial menu | P0 | Configurable long-press threshold; visible menu; turn highlights; click selects |
| WP-05 | Rotate while pressed | P0 | Independently remappable gesture; does not accidentally fire ordinary rotation |
| WP-06 | Standard tools | P0 | Volume/mute, scroll, zoom, undo/redo, brightness where available, previous/next/play/pause |
| WP-07 | Custom directional keyboard shortcuts | P0 | Separate clockwise/counterclockwise/click assignments, modifiers, key sequences |
| WP-08 | Add/remove/reorder wheel tools | P0 | GUI editor with icons, names, order, enable/disable, preview and instant apply |
| WP-09 | Haptic enable/disable | P0 | Device haptics really switch; status/limitations visible |
| WP-10 | Haptic step interval | P0 | Haptic ticks correlate to configurable angular increments where protocol permits |
| WP-11 | Rotation sensitivity and resolution | P0 | Smooth/stepped modes, degrees/step, event aggregation, fine/coarse and acceleration |
| WP-12 | Context-sensitive app tools | P0 | Auto-switch correct app profiles on focus changes for supported desktop sessions |
| WP-13 | Menu and tool icons | P0 | Assign bundled icons/custom user icon; clear selected state; tool names legible |
| WP-14 | Persist and restore configuration | P0 | All settings survive app close, daemon restart, logout, system reboot |
| WP-15 | Wheel positioning | P1 | Center/last pointer/custom display/position; multi-monitor; HiDPI |
| WP-16 | Visual settings and handedness | P1 | Right/left hand UI defaults, clockwise inversion override, accessibility prefs |
| WP-17 | Inking/brush tool modes | P1 | Explicit adapters for Krita, Blender sculpting, GIMP (where possible), Inkscape, etc. |
| WP-18 | Native app customization API | P1 | Registered applications can provide tool list and respond to rotation/click |
| WP-19 | Contextual haptics | P1 | Profile/tool-specific intervals or off; changes apply on tool switch |
| WP-20 | On-screen digitizer localization | Research gate | Only claim if supported digitizer emits actionable position; otherwise clear unsupported status |
| WP-21 | Media-aware contextual tools | P1 | Show media mode where MPRIS player available; optional auto-suggest without surprise activation |
| WP-22 | Custom macro workflows | P0 extension | Multi-step shortcuts, delays, commands under explicit permissions, profile-scoped |
| WP-23 | Profile import/export | P0 extension | Versioned interoperable JSON/TOML, validation and rollback |
| WP-24 | Multiple profile layers | P1 extension | Global, workspace, app, window, manual override; deterministic precedence |
| WP-25 | Reliability + install | P0 | .deb and Arch package work without manual privileged intervention |

**No equivalence claim without evidence.** Feature statuses: `verified`, `implemented-unverified`, `partial`, `blocked-upstream`, `unsupported-hardware`, `not-started`. A compositor restriction must never be presented as a working feature.

## 2. Target environment, compatibility, dependencies

- **Ubuntu:** current supported LTS versions, with minimum versions determined in discovery; validate GNOME Wayland and GNOME on Xorg if available. Use Debian packaging and official dependencies.
- **Pop!_OS:** validate the currently supported COSMIC desktop and any supported legacy GNOME session as separate test targets; do not assume COSMIC shares GNOME Shell extension protocols. Use `.deb` packaging and distro-specific smoke tests.
- **Arch Linux:** rolling releases; build `PKGBUILD`, `.SRCINFO`, packaging tests and `pacman` install/update/uninstall. Optional AUR publication after review (do not auto-publish).
- **KDE Plasma:** test Wayland and X11; document portal/compositor capabilities separately.
- **Bluetooth:** BlueZ/system pairing and trusted reconnection. Use D-Bus and kernel HID where available; do not implement parallel Bluetooth pairing stacks needlessly.
- **Audio:** PipeWire/WirePlumber via stable OS interfaces or user-facing integration, plus PulseAudio compatibility where relevant.
- **Media:** MPRIS D-Bus. Brightness: supported display/backlight interfaces; report unsupported external monitors or permission restrictions accurately. DDC/CI is optional with careful capability validation.
- **Input:** Linux `evdev`, `hidraw`, `libudev`, optional `/dev/uinput` for authorized fallback. Verify kernel HID behavior and event codes on actual hardware before deciding ownership of input nodes.
- **Desktop:** XDG desktop portal(s) for supported shortcuts/foreground information, then narrowly scoped GNOME/KDE/COSMIC adapters; X11 via XCB/XInput when allowed. Wayland is intentionally restrictive: avoid assuming global injection or focused-window identity is universally available.
- **Dependencies:** avoid unstable private D-Bus APIs unless version-gated and clearly disclosed. Do not ship privileged executable shells as a shortcut for key injection.

## 3. Research-first architecture decision (mandatory Phase A)

Audit existing implementations before committing to a stack:

1. `https://github.com/daniel5151/surface-dial-linux` — Rust daemon reads kernel evdev, monitors hotplug via udev, configures sensitivity/haptics via hidapi, and emits virtual input through uinput. Check license, current health, open issues, event decoding, disconnect behavior and API quality. **Do not blindly fork.**
2. Linux kernel `hid-microsoft`/relevant wheel-device code — supported IDs, HID reports, event mappings, haptic feature reports, regressions by kernel.
3. BlueZ and D-Bus device properties — disconnect, charging/battery reporting if exposed, pairing permissions.
4. Similar open-source rotary controller applications / radial menu tools and current portal/compositor support. Reuse components only if licensing and maintenance risk are acceptable.
5. Microsoft `RadialController` UX and configuration baseline above — enumerate every Windows behavior, including click/hold, context changes, tool customization, sensitivity, haptics, on-screen digitizer features.
6. Test compatibility on target desktops and kernel versions instead of extrapolating from one machine.

Deliver `docs/research-report.md`, `docs/hardware-protocol.md`, `docs/license-audit.md`, `docs/platform-capabilities.md`, `docs/architecture-decisions/ADR-*.md`, and initial parity matrix. Select implementation based on evidence and maintainability.

## 4. Proposed component architecture

**Recommended, subject to Phase A:** Rust workspace for hardware service, engine and IPC; Qt 6 + QML desktop GUI for polished multi-distro support. Alternative GTK 4/libadwaita acceptable only if clearly justified and parity across COSMIC/KDE tested. Keep frontend swappable.

```text
                GUI (Qt 6/QML)       Optional command-line tool
                       |                      |
                       +---- Versioned D-Bus/IPC API ---+
                                                        |
              Per-user OpenDial service (systemd --user)
                | profiles / context / event router |
                | gestures / radial state / macros  |
                | haptics / permissions / telemetry  |
                   |             |                |
          Device adapter   Desktop adapters   Action integrations
        BlueZ+evdev+HID    GNOME/KDE/COSMIC    MPRIS/audio/brightness
              |                |                app plugins/shortcuts
        Surface Dial HW     session APIs
```

Modules/crates: `opendial-device`, `opendial-gesture`, `opendial-haptics`, `opendial-profile`, `opendial-context`, `opendial-actions`, `opendial-ipc`, `opendial-daemon`, `opendial-cli`, `opendial-testkit`, `opendial-ui`, `opendial-packaging`. Keep direct HID writes behind an audited abstraction; avoid two processes competing to consume the same device events.

Service lifecycle: launch as user via `systemd --user`; use event-driven wakeups (not busy polling); restore state after reconnect; refuse duplicate instances; notify GUI of device changes; work without GUI running; expose clear health info; graceful shutdown without stuck synthetic keys; recover from malformed packets.

## 5. Input and gesture engine

### 5.1 Signals
- Signed rotation deltas with monotonic timestamp and preserved input ordering; optional raw degrees if hardware mapping validated.
- Button down/up, click, long-press, click count, hold-and-turn; distinguish physical down/up from higher-level synthetic gestures.
- Device connection and disconnection; data errors; optional battery level **only when reliably supplied by hardware/kernel**.
- Explicit active tool, radial menu open/close, application profile, suppression of events consumed by menu.

### 5.2 Configurable interaction behavior
- Relative vs absolute control (software integrates relative rotation into bounded values).
- Step-based vs continuous input; configurable angular detents, sensitivity, smoothing, acceleration, reverse direction, per-mode speed curves.
- One-step-per-event, multi-step burst, fast rotation acceleration; coalescing that preserves total delta.
- Long press threshold, double click window, click-to-toggle mode, double click or triple click actions, hold-and-rotate mode, release behavior. Additional gestures must not compromise normal Windows parity.
- Per-action repeat/debounce, anti-ghosting and hysteresis; unlimited mode layers via optional nested menu. Configurable slow fine control while pressed.
- Safe overflow limits and high-speed stress handling; haptics must not block input pipeline.

## 6. Radial menu UX (core flagship feature)

- A slick floating radial wheel invoked by long-press; rotation navigates, press activates; selected tool becomes active and remains active with menu hidden.
- Standard Windows-equivalent initial tool set (volume, scroll, zoom, undo/redo, brightness, media), with app-sensitive options where capabilities permit.
- Clear highlighted sector, readable icon/name, clockwise/counterclockwise action help, visible currently active tool, optional live value, haptic tick on selection.
- Keyboard/mouse/touch can configure and test same menu; accessibility labels and keyboard navigation.
- Configurable menu placement (pointer, screen center, saved position, display); handle rotated displays, fractional scale, multiple monitors, window focus and panel positioning.
- More than seven tools through pages/submenus/overflow; user-selected favorites and deterministic ordering; maintain <=7 default visible per ring as Windows UX reference.
- Dynamic applications can register custom menu items with stable IDs, icons, tooltips, supported actions and priorities. Do not allow untrusted apps to overwrite system tools without consent.
- Menu closes on escape, outside click, focus changes according to settings, timeout, disconnect; no stuck overlay blocking input.
- Custom theme/accent, translucent background where supported, contrast, motion preferences, animations that degrade gracefully; no unnecessary always-on-top window persistence.
- UI editor features WYSIWYG preview, drag/drop reorder, duplicate tool, search actions, reset to preset, inline collision/error warnings, undo/redo editor changes.

## 7. Full Windows-style tool programmability, plus extensions

Each tool MUST support **independent bindings** for `rotate_cw`, `rotate_ccw`, `click`, `long_press` (when not reserved for menu), `double_click`, `rotate_while_pressed_cw`, `rotate_while_pressed_ccw`, and optionally `release`. Default bindings must honor Windows parity; extension gestures user opt-in.

Supported actions:
1. Keyboard shortcuts (modifiers, letters, special keys, international layouts) and configurable key chords/sequences; UI key recorder with accurate display and validation.
2. Scroll (vertical/horizontal), system volume/mute, MPRIS play/pause/next/previous, browser back/forward, zoom, undo/redo, brightness, workspace switching when supported.
3. Application-specific tools; map to hotkeys, accessibility actions, app D-Bus / plugin APIs, HTTP local endpoint optional, MIDI and OSC optional extensions.
4. Launch app/file/URL, run **explicitly user-approved** command, macro with timed actions, conditionals bounded in complexity, variables and input mapping. Do not execute arbitrary strings from imported profiles automatically.
5. Continuous controls mapped to value ranges, min/max, snapping, reset-to-default, coarse/fine stepped adjustment, velocity-based scaling; automation use cases (brush size/opacity, CAD zoom, timeline scrubbing, rotation, playback speed, temperature setpoint).
6. Script adapter / plugin SDK: secure documented permission gates; sandboxed extension pathway or narrowly trusted local plugin model, no remote execution by default.
7. Device feedback for selected tool and action: haptic detents; OSD confirmation; configurable notification frequency and quiet mode.

**Important:** On Wayland, emitting arbitrary synthetic keys globally can be restricted. Backend must expose `supported`, `permission-needed`, `not-supported`, `fallback-available` per action/desktop. Prefer compositor-supported portals, native integrations, app-specific APIs, and user-approved shortcuts. `/dev/uinput` via secure access is a documented fallback only after informed opt-in and without bypassing session security assumptions. Never claim universal Wayland keyboard injection without testing.

## 8. Per-app and per-window profile system

- Global default, app-specific, window-specific (where supported), workspace-specific (where supported), temporarily locked/manual override, named custom modes.
- Clearly defined precedence: temporary manual lock > window > app > workspace > global; exact ambiguity resolution and fallback rules.
- Detect foreground app using desktop adapter (X11, GNOME Shell, KWin, COSMIC). Where compositor blocks detection, provide manual switching, authorized user-configured shortcut, and integration-specific supported methods.
- Match stable desktop app ID or process identity; account for Flatpak IDs and applications started via launchers; never rely solely on window title substring.
- Profile transitions smooth, preserve or reset selected tool per preference; avoid repeating destructive action on focus changes; disable automation on lock screen and secure input dialogs.
- Per-app import/export and bundled starter profiles: Blender (sculpting/zoom), Krita (brush size/opacity), GIMP, Inkscape, FreeCAD, browsers, terminals, media editors. Validate actual shortcut correctness in target app version and keyboard layout before shipping a profile as verified.
- User can preview current matched app ID and profile reason, simulate focus changes, pin a profile, troubleshoot misrouting.

## 9. Haptics and device management

- Independent global and per-tool haptic enable/disable. Hardware report-based control where validated; change interval if supported; visible feedback of applied vs requested state.
- Device sensitivity/resolution where available; avoid conflating software scaling with hardware settings.
- GUI pairing guidance (use official Bluetooth stack); list known nearby or paired devices only through system permissions; connection/reconnection status with actionable errors.
- Device identification by verified attributes and model; multiple physical devices only if behavior and address stability tested. Do not assume BLE MAC addresses remain static.
- Optional battery widget if genuine battery level data exposed; otherwise display `not reported`, not estimated percent.
- Settings reset to factory/default, device re-pair guidance, safe force reconnect and diagnostic report export. Never update firmware unless a separately verified firmware mechanism exists.

## 10. GUI / interaction design

Pages:
- **Dashboard:** connection status, current profile/tool, live encoder dial visualization, click/hold visual feedback, test haptics button, current backend capability status.
- **Radial Menu Editor:** true preview, drag/drop sectors, icons, pages/submenus, label and selection behavior, menu placement, themes.
- **Tool Editor:** mapping grid with clockwise, counterclockwise, click, hold+rotate and custom gestures; key recorder, macro builder, action catalog.
- **Profiles:** add/clone/delete/import/export, app matcher, automatic-switch simulation, precedence and overrides.
- **Rotation & Haptics:** response graph, sensitivity, detents, stepping, smoothing, haptic interval, per-tool overrides, fine tuning with live output.
- **Integrations:** compositor-specific status, needed permissions, tested app adapters and install instructions; portal availability.
- **Bluetooth & Device:** pairing hints, live connectivity, reconnect, version, input event diagnostics and validation.
- **Diagnostics:** logs, service state, HID/evdev detection, permissions check, safe self-test, packet diagnostics with opt-in redaction.
- **Settings:** launch at login, UI startup, minimize to tray where supported (AppIndicator/StatusNotifier), notifications, localization, update policy and privacy.

Visual design: modern, restrained, fast, keyboard accessible, dark/light theme, HiDPI/large-font support, English initially with localization-ready resources; device-relevant illustration and live previews; no Windows 95-style widgets; do not compromise functionality for cosmetic polish.

## 11. External APIs and developer extensibility

- Versioned D-Bus API or equivalent user-session IPC: get state, list devices, profiles/tools, preview raw events with access control, set active tool/profile, apply validated settings, subscribe to state changes.
- CLI: `opendialctl status|diagnose|profiles|devices|config-validate|test` with machine-readable JSON.
- Optional language-agnostic localhost API disabled by default; authenticated and bound to loopback only if enabled.
- Plugin architecture: app sends declarative tool descriptors and gets typed rotation/click events; feature negotiate and versioned schema; never presume Windows `RadialController` binaries work on Linux.
- Examples for Python, Rust, JS/TypeScript; minimal Blender/Krita integrations if officially supported extension APIs permit.
- Config schemas with migrations, strict validation, explicit version, atomic writing, backup, rollback; no silent destructive migrations.

## 12. Security and permissions

- Run UI and main engine as unprivileged user. Do not give users wholesale write access to `/dev/input/*` or `/dev/hidraw*`.
- Prefer narrow `udev` `uaccess` rules for verified Surface Dial device IDs, logind seat checks, and group/ACL model where unavoidable; compare access patterns across GNOME, KDE, COSMIC, and Arch; rules must never match all HID devices.
- If virtual device creation requires privileges, use minimal install-time permission strategy, polkit-gated helper, or opt-in mechanism. Define threat model for `/dev/uinput` access.
- Explicit user consent for macros launching commands or opening URLs; no profile-driven shell execution without authorization. Show imported profile dangerous actions and default them disabled until user approves.
- Never log raw password entry, key sequences or personally identifying window content; redact diagnostic exports; disable injections on lock screen.
- No cloud account, no telemetry by default, no network requirement, reproducible builds and dependency/license auditing.
- Security test invalid config, oversized events, malformed HID payloads, privilege escalation, plugin injection, symlink attacks, command escaping and D-Bus authorization.

## 13. Distribution, installation, upgrades, rollback

**Ubuntu / Pop!_OS**
- Signed release `.deb`, packaging with correct runtime dependencies, `udev` rules, desktop entry, icons, manpages, systemd --user service, MIME/config schema as needed; `apt install ./...deb` smoke test.
- Avoid hard-coded kernel/desktop assumptions; do not replace stock HID kernel modules.
- Post-install should show Bluetooth pairing and permission onboarding, with automatic service startup at next login or immediately if allowed.

**Arch**
- `PKGBUILD`, `.SRCINFO`, namcap validation, clean-chroot `makepkg`, pacman install/update/remove tests. Optional source package for AUR reviewed separately.
- Handle Qt, Rust, shared libraries through native package dependencies and correct version ranges.

**For all**
- Preserve existing user profiles across upgrades; migrate configs safely; rollback if migration fails; do not delete user config on uninstall without explicit purge.
- Uninstaller cleans installed service/rules/shortcuts and ensures no untracked privileged helper remains.
- Diagnostic installer checks BlueZ, adapter, device permissions, user session D-Bus, portals and supported compositor.
- Release artifacts: `.deb`, Arch package recipe, optional portable binary/Flatpak **UI-only where sandboxed device APIs are insufficient**; do not promise a self-contained Flatpak driver without proven permission model.
- Provide offline install docs, first-run wizard, FAQ for conflict with other daemons, troubleshooting; no need to disable native BlueZ services.

## 14. Testing and quality gates

Testing hierarchy:
1. **Unit:** direction, delta aggregation, click/hold classification, gesture conflicts, tool mapping, profile precedence, config migration, haptics policy, invalid inputs.
2. **Protocol fixtures:** recorded known-safe HID/evdev samples from real hardware, synthetic replay, randomized motion; scrub sensitive environmental data.
3. **Integration:** BlueZ reconnect, evdev + hidapi, IPC, udev/logind ACL, action providers, MPRIS, brightness, portal permissions.
4. **UI:** menu navigation from physical Dial, high DPI, dark/light, translations, key recording, radial keyboard navigation, large tool count.
5. **E2E physical:** rotate forward/back 1/10/100 increments, rapid turns, click, long press, rotate pressed, haptics, sleep/resume, power cycle, Bluetooth disconnect, reboot, app focus transitions, multiple monitors.
6. **OS matrix:** Ubuntu GNOME Wayland, Pop!_OS COSMIC Wayland, Arch KDE Plasma Wayland, GNOME X11 wherever still available, KDE X11 where applicable. Pin tested distro/kernel/desktop versions; rolling Arch is CI-tested periodically.
7. **Reliability:** 24-hour soak plus repeated 1000 hotplug/reconnect cycles using simulated tests and a smaller practical set on hardware; 1000 repeated menu/action transitions; concurrency and crash recovery.
8. **Performance targets (adjust through measured baseline):** median event-to-action <=20 ms for local action, p95 <=50 ms on reference hardware; radial overlay visible <=150 ms after long-press threshold; idle CPU typically <1% of one core; bounded idle memory and no growth/leaks in soak. Report hardware and compositor specifics.
9. **Install:** fresh installation, upgrades, downgraded incompatible config rejection, rollback, clean uninstall with preserved user config, no root background daemon, no manual reboot requirement unless ACL/seat policies demand it and explicitly communicated.

Every P0 feature must pass physical hardware acceptance on at least the designated reference Dial plus all three distribution families, with documented matrix exceptions if upstream compositor prevents an action. A blocked feature means **the build is not allowed to advertise full parity**. Define which blockers are release-stopping and which require an explicit public limitation statement.

## 15. Deliverables and repository structure

```text
README.md
LICENSE
CONTRIBUTING.md
SECURITY.md
CLAUDE.md
Cargo.toml
crates/
ui/
plugins/
profiles/examples/
packaging/debian/
packaging/arch/
packaging/udev/
packaging/systemd/
docs/SRS.md
docs/parity-matrix.md
docs/research-report.md
docs/hardware-protocol.md
docs/platform-capabilities.md
docs/security-threat-model.md
docs/architecture-decisions/
docs/installation/
docs/troubleshooting/
tests/unit/
tests/integration/
tests/hardware/
tests/replay-fixtures/
.github/workflows/
```

All release documents must have accurate verification date, OS versions, hardware IDs, tested feature coverage, limits and reproduction steps. Provide install video or annotated screenshots and a complete user manual.

## 16. Goal → Delegate → Verify implementation roadmap

### Phase A — Discover and design (release gate)
**Goal:** Ground the design in verified hardware/protocol and Windows baseline; map capabilities for Ubuntu/Pop!_OS/Arch and Wayland/X11.
**Delegate:** separate parallel agents for (a) Windows parity research, (b) existing Rust and kernel code, (c) compositor/input security, (d) distro packaging, (e) UI architecture.
**Verify:** evidence links and dated notes; working HID sample reads; complete parity matrix and ADRs; no engineering decisions based on unverified guesses.

### Phase B — Minimal functioning vertical slice
**Goal:** installable user service; detect paired Dial, read rotation/click, show raw events in GUI/CLI; one reliable action (e.g. volume), restart-safe and haptics toggle if supported.
**Verify:** physical Dial, supported distro packaging, permission-safe startup, reconnect, no stuck input, clean uninstall.

### Phase C — Full Windows parity interactions
**Goal:** radial menu, all standard tools, custom clockwise/counterclockwise/click keyboard mappings, haptic intervals, sensitivity, tool configuration GUI.
**Verify:** each WP-01 through WP-16 condition by physical interaction and visual screenshots, audit target desktop limitations.

### Phase D — Contextual app profiles and plugins
**Goal:** robust per-app switching, macro builder, application integration, inking/creative profiles, reliable fallbacks on restricted Wayland sessions.
**Verify:** Blender/Krita/FreeCAD plus browser/media app, focus transfer, unsupported compositor states visibly surfaced, secure plugin operations.

### Phase E — Production packaging and hardening
**Goal:** signed distro-native installers, documentation, diagnostics, performance, hardware soak, security review, release artifacts.
**Verify:** fresh VM/package tests and real desktop hardware acceptance on Ubuntu, Pop!_OS, Arch; independent clean-room install and rollback; traceable evidence for all release claims.

## 17. Execution rules for Claude Code

1. Inspect repository state first. If a repository exists, do not overwrite unrelated files, remove commits, or rewrite architecture without explaining decision.
2. Use a research-first task breakdown, then maintain `PLAN.md`, `TASKS.md`, `VERIFICATION.md`, `docs/parity-matrix.md`, and `DECISIONS.md` with current evidence.
3. Delegate bounded parallel work when helpful, but avoid concurrent edits to same files; merge and run integration checks after each batch.
4. Separate architecture/API from GUI early; do not build GUI mockup without functioning device layer, except as isolated design prototype.
5. Implement deterministic tests and fixtures before high-risk refactors. Keep CI fast and portable, with hardware-specific jobs separately labeled.
6. If physical hardware is unavailable, implement fixtures and virtual tests, mark hardware verification as **blocked**, and provide exact manual test instructions. Do not claim success from simulation.
7. Use `cargo fmt`, `cargo clippy`, test suites, security checks, package lint and platform checks where runnable. Save command output and pass/fail summaries.
8. Never claim Windows parity unless matrix rows are physically verified; clearly identify platform exceptions and research-only features.
9. Build production packages only when installation scripts are reviewed, least privilege is established, and rollback is tested.
10. Keep all services and installed files documented; aim for one-command install and first-run GUI.

## 18. Copy-paste Claude Code master prompt

> **GOAL**
>
> Build the Microsoft Surface Dial Linux driver and graphical configuration suite defined by `docs/SRS.md`. The user requires **at least Windows Surface Dial feature equivalence and programmability**, not merely scroll and volume mappings. Target Ubuntu, Pop!_OS (including COSMIC), and Arch Linux, across actual supported GNOME/KDE/COSMIC Wayland and X11 environments. Deliver a polished, maintainable, secure product with production packaging, contextual radial menu, individual clockwise/counter-clockwise/click custom tool actions, haptics, sensitivity, app profiles, macros, and diagnostics.
>
> **RESEARCH / DELEGATE**
>
> Read the full SRS and Microsoft documentation cited in it. Audit `daniel5151/surface-dial-linux`, the Linux kernel HID support, BlueZ, and Linux session input/portal limitations. Produce an evidence-based Windows-to-Linux parity table, protocol notes and architecture decisions before implementation. Delegate independent audits of hardware, desktop sessions, GUI, packaging, testing. Respect licenses and least-privilege requirements.
>
> **IMPLEMENT**
>
> Start with an actually working physical device and user-mode service, then a complete radial menu and customizable tool editor, followed by per-app profiles/plugins and advanced programmability. Ensure `.deb` packaging for Ubuntu/Pop!_OS and Arch `PKGBUILD`. Preserve settings across reconnect/logout/reboot and install/upgrade; run without root. Implement secure Wayland-capable strategies with documented alternatives and limitations rather than claiming unrestricted global input access.
>
> **VERIFY**
>
> Every deliverable must have testable acceptance criteria and real artifacts. Validate long press -> radial menu -> rotate highlight -> click select; raw rotation; rotation-while-pressed; individual directional/click shortcuts; haptics and resolution; per-app switching; secure command/macro handling; reconnect; and installation on all target distributions. Report pass/fail/blocked per Windows parity feature and compositor. Do not claim test execution that did not occur. Any hardware capability not available on the tested hardware (e.g. Surface Studio digitizer position) must be explicitly documented as conditional.
>
> **WORKFLOW**
>
> Use Goal → Delegate → Verify with task tracking, small verified vertical slices, code review and evidence files. Complete as much as possible autonomously, commit cohesive changes, and provide an honest final compatibility matrix, install instructions, remaining gaps, and next priorities.

## 19. Explicit out-of-scope / conditional feature disclosure

- The original Surface Dial is a BLE peripheral, **not a 3D SpaceMouse**; it cannot inherently provide 6-DOF spatial axes. Do not make that claim.
- Hardware-on-screen location and menu wrap-around depend on a Surface Studio-class compatible digitizer and Linux-exposed contact data. General Linux laptop/display support must not promise on-screen detection.
- Battery state may be absent from publicly visible HID/GATT interfaces; show `unavailable` if not verifiable.
- Windows UWP/WinRT app-specific integrations do not run natively as-is on Linux; reproduce *capabilities* with native adapters instead.
- Global shortcuts and synthetic input are compositor-specific under Wayland; no API can simply assume all desktop security boundaries are bypassable.
- Any advertised future support for other rotary dials requires their own hardware test matrix; do not conflate them with the Surface Dial.

## 20. Primary authoritative references

1. Microsoft Support, Meet Surface Dial: https://support.microsoft.com/en-us/surface/accessories/dial/meet-surface-dial
2. Microsoft Support, Change Surface Dial settings: https://support.microsoft.com/en-us/surface/accessories/dial/change-surface-dial-settings
3. Microsoft Learn, Surface Dial interactions: https://learn.microsoft.com/en-us/windows/apps/develop/input/windows-wheel-interactions
4. Microsoft Learn, RadialController walkthrough: https://learn.microsoft.com/en-us/windows/apps/design/input/radialcontroller-walkthrough
5. Microsoft, Surface Dial technical specifications: https://www.microsoft.com/en-us/surface/accessories/surface-dial
6. Community Rust Linux daemon: https://github.com/daniel5151/surface-dial-linux

**Final product standard:** a genuinely usable and safe Linux Surface Dial equivalent to the Windows experience, explicitly validated where supported, with stronger customization and no fictional device/platform capabilities.
