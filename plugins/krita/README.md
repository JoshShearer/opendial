# OpenDial for Krita

Adds Dial tools that act inside Krita through its Python API, instead of
through keyboard shortcuts:

| Tool | Turn | Click |
|---|---|---|
| Brush size | ± 8 % per step (at least 1 px), 1–1000 px | toggle eraser |
| Opacity | ± 5 % | 100 % |
| Canvas zoom | ×1.1 per step | fit page |
| Canvas angle | ± 5° | reset rotation |

**Status:** tested in Krita 6.0.4 (PyQt6) with simulated Dial input: the
tools register, and turning changes brush size, opacity, zoom and canvas angle.
Written to work with Krita 5 (PyQt5) too, but not run there. Once you allow the
plugin, it replaces the shortcut-based Krita tools in the menu.

Krita's Python plugins need the Qt Python bindings. On Arch install
`python-pyqt6` (Krita 6), otherwise Krita reports "Python cannot find the Qt6
bindings".

## Install

```sh
mkdir -p ~/.local/share/krita/pykrita
cp -r /usr/share/opendial/plugins/krita/opendial /usr/share/opendial/plugins/krita/opendial.desktop ~/.local/share/krita/pykrita/
```
In Krita, go to *Settings → Configure Krita → Python Plugin Manager*, enable
**OpenDial** and restart Krita. Then allow the plugin in OpenDial (*Integrations
→ Plugins*). The tools show up in the radial menu while Krita is focused.
