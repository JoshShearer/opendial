# Tasks

## Open (need hardware, other desktops or a maintainer)

- [ ] **Physical Dial:** run the full checklist in `tests/hardware/README.md`. The Dial is paired and trusted on the reference machine but was asleep. Haptics need the udev rule installed (sudo).
- [ ] Add captured fixtures from the real Dial to `tests/replay-fixtures/`.
- [ ] Read the report descriptor; confirm the feature-report read-back on hardware.
- [ ] GNOME Wayland (Ubuntu 24.04): extension, portal injection, frameless overlay.
- [ ] GNOME on Xorg: XTest, `_NET_ACTIVE_WINDOW`.
- [ ] KDE Plasma 6 Wayland and X11: KWin script bridge, virtual keyboard or portal.
- [ ] Pop!_OS COSMIC: check for a focus protocol and RemoteDesktop portal; otherwise publish the limitation.
- [ ] Observe scroll delivery through `zwlr_virtual_pointer_v1`.
- [ ] Run the Krita plugin inside Krita 5.2.
- [ ] Compile the Rust example (`cargo build` in `plugins/examples/rust`).
- [ ] Check the starter profiles' shortcuts against current Blender, GIMP, Inkscape, FreeCAD and Kdenlive versions.
- [ ] 8 h soak and 50-cycle reconnect on hardware (SRS §14.7).
- [ ] Latency on hardware: median ≤ 20 ms, p95 ≤ 50 ms (`latencyUs` in the event log).
- [ ] Trademark check; signed releases; AUR publication (manual, reviewed).

## Done

- [x] Core: decoder, gestures, rotation, haptics policy, radial model, profiles, actions, config with migration/rollback.
- [x] Service: device + hotplug, injection backends, focus trackers, overlay + OSD, tray, BlueZ, audio, MPRIS, brightness, D-Bus API, HTTP/SSE API, plugins, approvals.
- [x] GUI: dashboard, menu editor, tool editor, profiles, rotation & haptics, integrations, device, diagnostics, settings, first-run wizard.
- [x] CLI with JSON output, record, simulate; replay in `opendiald`.
- [x] Tests: 8 unit suites + 4 integration suites; nested-Hyprland key-injection E2E.
- [x] .deb and Arch packaging, verified in containers.
- [x] Docs and CI.
