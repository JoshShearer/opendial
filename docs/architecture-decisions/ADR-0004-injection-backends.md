# ADR-0004: Desktop-native injection, uinput only after opt-in

- **Status:** accepted, 2026-11-10
- **Context:** Wayland blocks global synthetic input by design. The SRS forbids X11-only shortcuts while claiming Wayland support, and asks for least privilege.
- **Decision:**
  - Backends, in order: Wayland virtual keyboard/pointer protocols → xdg-desktop-portal RemoteDesktop (restore token persisted) → XTest → uinput.
  - uinput needs a user opt-in plus an admin-installed rule. It is not shipped active.
  - Each backend reports `supported` / `permission-needed` / `not-supported` / `fallback-available`, and the UI shows that per action.
- **Consequences:**
  - GNOME users see one portal prompt.
  - Actions that don't need injection (volume, MPRIS, brightness, D-Bus, OSC) work everywhere.
