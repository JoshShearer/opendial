# ADR-0003: Radial menu overlay drawn by the service

- **Status:** accepted, 2026-11-10
- **Context:** The menu has to appear within 150 ms of the long-press threshold, even when the GUI is not running.
- **Decision:**
  - `opendiald` owns a QML overlay (`src/daemon/qml/RadialMenu.qml`, shared with the GUI preview).
  - It uses a `zwlr_layer_shell_v1` overlay surface (no keyboard focus) when LayerShellQt and the protocol are available.
  - Otherwise it uses a frameless always-on-top tool window (X11, GNOME).
  - The window is created hidden at startup and only shown, so it is never re-created.
- **Consequences:**
  - The daemon links Qt Quick (measured 65 MB RSS idle with the overlay on Wayland, 0 CPU ticks over 10 s idle; 2026-11-10, reference machine).
  - `--no-overlay` runs headless.
  - The GUI preview is the exact same component as the real menu.
