# OpenDial

Microsoft Surface Dial support for Linux: a per-user service with a radial
tool menu, the Windows wheel tools, custom tools with separate clockwise,
counter-clockwise and click shortcuts, per-app profiles, haptics, macros, a
Qt 6 configuration app, a CLI, a D-Bus API and plugins.

> **Status: 0.9.0 beta.** All features are implemented and covered by
> automated tests. On real hardware it has so far been verified on **Hyprland
> (Arch/Omarchy) only**. Reports from other desktops are very welcome; see
> [Reporting hardware results](#reporting-hardware-results).

### Tested on

| Desktop | Real Dial | Notes |
|---|---|---|
| Hyprland 0.56 (Arch, Omarchy) | **verified** | Rotation, click, long press, menu, haptics, built-in tools, theme following, per-app profile switching, starts on its own after a reboot, out-of-range reconnect, 17+ suspend/resume cycles, 55 reconnect cycles, 16.5 h awake soak with no daemon restart or memory growth |
| GNOME 46+ (Wayland/X11) | not yet | Adapter tested in a headless GNOME session with simulated input; needs the bundled Shell extension |
| KDE Plasma 6 | not yet | Adapter tested headless with simulated input |
| COSMIC | not yet | Adapter tested headless with simulated input |
| Other wlroots compositors (Sway, river, …) | not yet | Uses the same Wayland protocols as Hyprland |

Krita with the real Dial is still to be checked.
[docs/parity-matrix.md](docs/parity-matrix.md) has the per-feature detail;
nothing in it is marked verified on simulation alone.

## What it does

| Windows wheel feature | OpenDial |
|---|---|
| Press and hold opens the wheel menu; turn highlights, click selects | Layer-shell overlay on Wayland (KDE, wlroots, Hyprland, COSMIC), frameless window on GNOME/X11; placement at pointer, centre, saved position or a chosen display |
| Volume, Scroll, Zoom, Undo, Brightness, Media | Built in, in the same order; plus horizontal scroll, mic, seek, workspaces, window switching, arrows and a "More tools" folder |
| Custom tool: shortcut per clockwise / counter-clockwise / click | Yes, with a key recorder. Also double-click, long press, turn-while-pressed (both directions) and release |
| App-specific tools | Profiles matched by app ID (Flatpak IDs included), window class, title regex and workspace. Starter profiles for browsers, Krita, Blender, GIMP, Inkscape, FreeCAD, media players and video editors |
| Haptics on/off, haptic feedback | Global and per-tool; hardware detents follow the tool's step size |
| Rotation sensitivity | Stepped and continuous modes, degrees per step, sensitivity, acceleration, smoothing, invert, fine control while pressed |
| Dominant hand | Left/right handedness: with "open beside the pointer" the menu opens on the side away from your hand, so it does not cover what you point at |
| Add, remove, reorder tools; icons | Drag to reorder, enable/disable, duplicate, bundled icons or a custom image, undo/redo in the editor |

Beyond Windows:
- unlimited tools via folders and pages
- macros with delays, bounded conditionals and variables
- value tools (min/max/snap/reset)
- launch, URL and command actions that need approval
- OSC output
- versioned profile import/export with validation and multi-level rollback
- a fixture recorder and replayer
- a loopback HTTP API with an event stream
- a plugin protocol with Python, JS and Rust examples, plus Krita and GNOME Shell integrations

## Install

**You need:** a Microsoft Surface Dial, a Bluetooth LE adapter, and a
systemd-based Linux desktop (Wayland or X11). There are no prebuilt packages
yet; the scripts below build a native package from the source in a couple of
minutes.

```sh
git clone https://github.com/JoshShearer/opendial.git
cd opendial
```

**Arch Linux** (build dependencies are in [docs/installation/arch.md](docs/installation/arch.md))
```sh
scripts/build-arch.sh          # builds dist/opendial-*.pkg.tar.zst
sudo pacman -U dist/opendial-0.9.0-1-x86_64.pkg.tar.zst
```

**Ubuntu 24.04+ / Pop!_OS / Debian 13**
```sh
sudo apt install devscripts equivs
sudo mk-build-deps -i packaging/debian/control
scripts/build-deb.sh           # builds dist/opendial_*.deb
sudo apt install ./dist/opendial_0.9.0-1_amd64.deb
```

Then, as your normal user (never with sudo):
```sh
systemctl --user enable --now opendial.service
opendial            # or "OpenDial" from the application menu
```

Pair the Dial: pull off the bottom cover and hold the pairing button next to the
batteries for about 5 s until the LED flashes, then pair it in
your desktop's Bluetooth settings or on the app's **Device** page. The first-run
wizard checks permissions and walks you through a test turn and click.

See [docs/installation/](docs/installation/) for per-distro details, building
from source, and uninstalling. See [docs/troubleshooting/](docs/troubleshooting/)
when something does not respond.

## Components

| Binary | Role |
|---|---|
| `opendiald` | Per-user service (systemd `--user`, D-Bus activated). Reads the Dial, runs gestures, menu, profiles and actions, and draws the overlay and OSD. Works without the GUI. |
| `opendial` | Configuration app (Qt Quick): dashboard, menu editor, tool editor, profiles, rotation & haptics, integrations, device, diagnostics, settings. |
| `opendialctl` | CLI: `status`, `diagnose`, `profiles`, `devices`, `config-validate`, `test`, `simulate`, `record` and more (`opendiald --replay FILE` plays a recording back). Prints JSON with `--json`. |

Architecture: [docs/architecture-decisions/](docs/architecture-decisions/).
D-Bus API: [docs/dbus-api.md](docs/dbus-api.md).
Plugins: [plugins/README.md](plugins/README.md).

## Security model in one paragraph

Everything runs as you. A udev rule grants the active seat access to the
Surface Dial's own nodes (`045e:091b` over Bluetooth) and to no other device.
Key injection uses each desktop's own protocols (virtual keyboard, RemoteDesktop
portal, XTest). `/dev/uinput` is used only if you opt in, and that needs an admin
to install an extra rule. Commands, D-Bus calls, app launches and remote OSC
targets need a one-time approval. Imported profiles never run anything
unapproved. Diagnostics are redacted by default. Details:
[docs/security-threat-model.md](docs/security-threat-model.md).

## Development

```sh
cmake -S . -B build -G Ninja && cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```
See [CONTRIBUTING.md](CONTRIBUTING.md). The project keeps
[PLAN.md](docs/dev/PLAN.md), [TASKS.md](docs/dev/TASKS.md), [VERIFICATION.md](VERIFICATION.md)
and [DECISIONS.md](docs/dev/DECISIONS.md) up to date.

## Reporting hardware results

If you try OpenDial on a desktop or distro that is not marked verified above,
please open a [hardware report](https://github.com/JoshShearer/opendial/issues/new?template=hardware-report.yml),
whether it worked or not. Attach the output of `opendialctl diagnose --json`;
it redacts addresses, window titles and paths.

## License

MIT, see [LICENSE](LICENSE) and [docs/license-audit.md](docs/license-audit.md).
"Surface" and "Surface Dial" are trademarks of Microsoft. This project is not
affiliated with or endorsed by Microsoft.
