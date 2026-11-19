First public beta of OpenDial: Microsoft Surface Dial support for Linux.

## What you get

- Press and hold for the radial tool menu; turn to highlight, click to select.
- The Windows wheel tools (Volume, Scroll, Zoom, Undo, Brightness, Media),
  plus horizontal scroll, mic, seek, workspaces, window switching and more.
- Custom tools with separate clockwise, counter-clockwise, click, double-click,
  long-press and press-and-turn actions, a key recorder, and macros.
- Per-app profiles matched by app ID, window class, title and workspace, with
  starter profiles for browsers, Krita, Blender, GIMP, Inkscape, FreeCAD, media
  players and video editors.
- Haptic feedback, rotation sensitivity and acceleration, left/right handed.
- Menu and settings app follow the desktop theme (Omarchy, GNOME, KDE, COSMIC).
- `opendialctl` CLI, a D-Bus API, a loopback HTTP API and a plugin protocol.

## Tested on

Verified with a real Dial on **Hyprland (Arch/Omarchy)**: input, menu,
haptics, tools, theme, out-of-range reconnect, 17+ suspend/resume cycles,
55 reconnect cycles and a 16.5 h idle soak without a daemon restart.

GNOME, KDE Plasma and COSMIC adapters pass automated tests in headless
sessions but have **not yet been tried with a real Dial**. If you use one of
them, a [hardware report](https://github.com/JoshShearer/opendial/issues/new?template=hardware-report.yml)
helps a lot, whether it works or not.

## Install

- Arch: `sudo pacman -U opendial-0.9.0-1-x86_64.pkg.tar.zst` (AUR package to follow)
- Ubuntu 24.04+ / Pop!_OS / Debian 13: `sudo apt install ./opendial_0.9.0-1_amd64.deb`

Then, as your normal user: `systemctl --user enable --now opendial.service`.
See [docs/installation](https://github.com/JoshShearer/opendial/tree/main/docs/installation)
for the one-time device permission step.

Checksums are in `SHA256SUMS`.
