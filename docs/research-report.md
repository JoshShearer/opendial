# Research report (Phase A)

**Date:** 2026-11-10.

**How this was researched:** what was checked locally on the reference machine
is marked **local**. Everything else comes from the cited public sources as
known when this was written. Those web sources were **not** re-fetched during
this implementation session. Recheck them before a public release (SRS §1.1:
"revalidate during implementation").

## 1. Windows baseline

Sources: Microsoft Support, "Meet Surface Dial" and "Change Surface Dial
settings"; Microsoft Learn, "Surface Dial interactions" and the RadialController
walkthrough (links in [SRS.md](SRS.md) §20).

| Behaviour | Windows | OpenDial decision |
|---|---|---|
| Hold → menu | Press and hold (~1 s) opens the wheel menu, a ring of up to 6–7 tools; turning highlights, click selects; the selected tool stays active | Same. Threshold configurable (default 500 ms, so it feels responsive on Linux); `maxPerRing` 7 with overflow pages and folders |
| Default tools | Volume, Scroll, Zoom, Undo, plus Brightness and Media where available | Same six, same order |
| Custom tools | Settings → Devices → Wheel: name, icon, and a keyboard shortcut each for *turn left*, *turn right*, *click*; app-specific or for all apps | Same, plus more gestures and action types |
| Haptics | Wheel settings toggle vibration. Apps set `RadialController.UseAutomaticHapticFeedback` and rotation resolution; guidance: avoid haptics at fine resolution | Global and per-tool toggle; detent → resolution; clamp below 2° |
| Handedness | Windows uses the dominant-hand setting for menu placement next to the pen | `handedness` setting + "open beside the pointer" |
| App integration | Apps add `RadialControllerMenuItem`s with icons and get `RotationChanged`/`ButtonClicked` | D-Bus plugin protocol (RegisterApp/PluginEvent) with user consent |
| On-screen (Studio) | Dial on screen: menu around the puck | Out of scope without a compatible digitizer (WP-20) |

## 2. Existing Linux implementations

**daniel5151/surface-dial-linux** (Rust, MIT):
- It is the main prior art. It reads evdev, watches udev, writes the haptics feature report through hidapi, and emits input through uinput. Modes are hard-coded and switched with long press and a notification.
- What we take from it (facts only): the feature/output report layouts, `REL_DIAL` units at the default resolution, and the reconnect behaviour.
- Why we did not fork it:
  - Its output relies on uinput, which conflicts with SRS §12 least privilege.
  - It has no radial UI, profiles or GUI.
  - The architecture would need replacing almost entirely.

**Kernel:**
- The Dial is driven by `hid-generic`/`hid-input`, which maps the Generic Desktop *Dial* usage to `REL_DIAL` and the button to `BTN_0`.
- `hid-microsoft` has no special handling for 091B.
- **local:** the reference kernel (7.2.15) is recent, so no regressions are expected. Event capture on hardware is still open (see [hardware-protocol.md](hardware-protocol.md)).

**BlueZ:**
- **local:** `org.bluez.Device1` exposes Paired/Trusted/Connected, and `org.bluez.Battery1` the GATT battery level. Observed values: paired, trusted, battery 99 %, modalias `usb:v045Ep091Bd0108`.
- Pairing through BlueZ D-Bus works without privileges for the active session (BlueZ's default policy).

**Other tools surveyed (by description):**
- Input remappers: input-remapper, keyd, kanata. They are evdev-to-uinput remappers, which need root or uinput, so they are not suitable as a base.
- Desktop radial menus: Kando (Electron), and pie menus in KDE/GNOME extensions. None is aware of the Dial.

## 3. Desktop security model (Wayland)

- **Injection:**
  - Wayland clients cannot inject input by default.
  - Options: compositor protocols (`zwp_virtual_keyboard_v1`, `zwlr_virtual_pointer_v1`, offered by wlroots-based compositors, Hyprland and sometimes KWin), the xdg-desktop-portal **RemoteDesktop** interface (GNOME, KDE; it asks the user and supports restore tokens), or uinput (kernel-level, desktop-independent, privileged).
  - Decision: the order in [platform-capabilities.md](platform-capabilities.md).
  - **local:** keys delivered on Hyprland through the virtual keyboard; pointer delivery not yet observed.
- **Focus:**
  - No portal exposes the focused app.
  - Each desktop has its own route: GNOME needs a Shell extension; KWin accepts scripts over D-Bus; wlroots has foreign-toplevel; Hyprland and Sway have IPC sockets; X11 has EWMH.
  - COSMIC: no client-facing focus API known.
- **Overlay:** layer-shell where available; elsewhere a frameless top-level window.

## 4. Packaging targets

- **Ubuntu 24.04 LTS:**
  - Qt 6.4.2 sets the minimum Qt version.
  - LayerShellQt for Qt 6 is not in noble (Plasma 5 era), so the overlay falls back to a frameless window there unless built against a newer LayerShellQt.
  - **local:** built, all tests passed and installed in an `ubuntu:24.04` container.
- **Pop!_OS 24.04 (COSMIC):** Ubuntu noble base, so the same .deb.
- **Arch:**
  - Qt 6.12, layer-shell-qt 6.8.
  - **local:** built with makepkg, checked with namcap and installed with pacman in `archlinux:latest`.

## 5. UI framework

Qt 6 + QML (Material style), chosen over GTK 4/libadwaita because:
- one toolkit draws both the overlay (layer-shell via LayerShellQt) and the settings app
- it looks native enough on KDE, COSMIC and GNOME
- C++ shares the core library with the daemon without an FFI layer.

See ADR-0001 and ADR-0003.

## 6. Language

The SRS recommends Rust. We chose C++20 + Qt instead (ADR-0001):
- The GUI is Qt/QML either way.
- Qt's D-Bus, layer-shell and Wayland client integration are first-class in C++.
- One build system and one toolchain package per distro.
- Rust bindings for Qt Quick (cxx-qt) add a second toolchain and lag behind Qt releases.

Cost: no `cargo clippy`. In its place: `-Wall -Wextra` (optionally `-Werror`), Qt Test, sanitizer builds in CI, and fuzz-style garbage tests on the decoder and the simulation input.

## Open research items

1. Hardware: capture rotation and button fixtures, read the report descriptor, write and read back the feature report.
2. COSMIC: check whether cosmic-comp offers a toplevel-info or virtual-keyboard protocol to clients, and whether xdg-desktop-portal-cosmic implements RemoteDesktop.
3. GNOME 49: check the extension on the current Shell.
4. KWin 6: confirm the virtual-keyboard protocol is advertised to unprivileged clients; otherwise the portal.
5. App shortcuts in the starter profiles: check each against current app versions (SRS §8).
