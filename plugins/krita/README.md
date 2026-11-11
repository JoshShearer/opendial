# OpenDial for Krita

Adds Dial tools that act inside Krita through its Python API, instead of
through keyboard shortcuts:

| Tool | Turn | Click |
|---|---|---|
| Brush size | ± 8 % per step (at least 1 px), 1–1000 px | toggle eraser |
| Opacity | ± 5 % | 100 % |
| Canvas zoom | ×1.1 per step | fit page |
| Canvas angle | ± 5° | reset rotation |

**Status:** written against the Krita 5.2 API and PyQt5/PyQt6 QtDBus, but not
yet run inside Krita. Please report results.

## Install

```sh
mkdir -p ~/.local/share/krita/pykrita
cp -r /usr/share/opendial/plugins/krita/opendial /usr/share/opendial/plugins/krita/opendial.desktop ~/.local/share/krita/pykrita/
```
In Krita, go to *Settings → Configure Krita → Python Plugin Manager*, enable
**OpenDial** and restart Krita. Then allow the plugin in OpenDial (*Integrations
→ Plugins*). The tools show up in the radial menu while Krita is focused.
