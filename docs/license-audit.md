# License audit

**Last updated:** 2026-11-10. OpenDial is licensed under **MIT** ([LICENSE](../LICENSE)).

## Code in this repository

| Path | Origin | License | Notes |
|---|---|---|---|
| `src/`, `tests/`, `plugins/`, `scripts/`, `packaging/` | written for OpenDial | MIT | SPDX headers in source files |
| `data/icons/` (60 SVGs) | drawn for OpenDial (24 px stroke icons) | MIT | SPDX header in each file; not copied from any icon set |
| `protocols/virtual-keyboard-unstable-v1.xml` | Wayland protocol (Purism / Wayland authors) | MIT | Only the XML is copied; code is generated at build time by `wayland-scanner` |
| `protocols/wlr-virtual-pointer-unstable-v1.xml` | wlr-protocols | MIT | as above |
| `protocols/wlr-foreign-toplevel-management-unstable-v1.xml` | wlr-protocols | HPND (sell variant) | Permissive, GPL- and MIT-compatible; notice kept in the file and in `debian/copyright` |
| `protocols/ext-foreign-toplevel-list-v1.xml` | wayland-protocols (staging) | HPND (sell variant) | as above |
| `protocols/cosmic-toplevel-info-unstable-v1.xml` | pop-os/cosmic-protocols | HPND (sell variant) | as above. The file references two workspace interfaces OpenDial never binds; `CosmicToplevelTracker.cpp` defines empty placeholders for them |

No source code was copied from other Surface Dial projects.
[daniel5151/surface-dial-linux](https://github.com/daniel5151/surface-dial-linux)
(MIT) was **studied** for the protocol facts: the report layouts and the
reconnect-flood behaviour. Facts are not copyrightable, and the implementation
here is independent C++. It is credited in the research report.

## Runtime and build dependencies (dynamically linked or invoked)

| Dependency | License | Use | Compatibility with MIT distribution |
|---|---|---|---|
| Qt 6 (Core, Gui, Widgets, DBus, Network, Quick, QuickControls2, Svg, Wayland) | LGPL-3.0 (or GPL / commercial) | UI, IPC, event loop | OK: dynamically linked from distro packages; users can replace the Qt libraries |
| LayerShellQt | LGPL-2.1+/LGPL-3 | overlay on wlroots/KDE/COSMIC | OK, dynamic |
| libudev (systemd) | LGPL-2.1+ | hotplug, device lookup | OK, dynamic |
| libxkbcommon | MIT | keymaps | OK |
| libwayland-client | MIT | virtual input, toplevel tracking | OK |
| libX11, libXtst | MIT/X11 | X11 injection and focus | OK |
| wayland-scanner (build only) | MIT | code generation | OK; generated glue is MIT |
| wpctl / pactl, brightnessctl, ddcutil, bluez | MIT / LGPL / MIT / GPL-2 / GPL-2 | invoked as separate programs or over D-Bus | OK: no linking |

## Plugins and examples

| Path | License | Runtime it needs |
|---|---|---|
| `plugins/krita/` | MIT | Krita's Python API (Krita is GPL-3; a plugin talking to it through its API at runtime is fine to distribute under MIT) |
| `plugins/gnome-shell/` | MIT | GNOME Shell (GPL-2+). Note: extensions.gnome.org expects GPL-2+-compatible code; MIT is compatible |
| `plugins/examples/python` | MIT | PyGObject (LGPL-2.1+) |
| `plugins/examples/js` | MIT | Node.js built-ins only |
| `plugins/examples/rust` | MIT | zbus (MIT), serde_json (MIT/Apache-2.0); not vendored |

## Trademarks

"Microsoft", "Surface" and "Surface Dial" are trademarks of Microsoft
Corporation. They are used only to describe the supported hardware. No
Microsoft artwork, icons or text are included. The working name "OpenDial" has
**not** been checked for trademark conflicts yet (SRS header). Do that before
publishing.

## Reproducing the audit

```sh
grep -rL "SPDX-License-Identifier" src tests plugins data/icons --include='*.cpp' --include='*.h' --include='*.qml' --include='*.svg' --include='*.py' --include='*.js' --include='*.mjs' --include='*.rs'
grep -A3 "<copyright>" protocols/*.xml
```
