# Decisions

Architecture decisions are in [docs/architecture-decisions/](../architecture-decisions/).
Smaller decisions:

| Date | Decision | Why |
|---|---|---|
| 2026-11-10 | C++20/Qt 6 instead of Rust (ADR-0001) | One toolkit for the overlay, GUI and IPC; distro-native Qt |
| 2026-11-10 | Long-press default 500 ms (Windows ~1 s) | Feels slow on Linux, where there is no pen-oriented UI; configurable |
| 2026-11-10 | Haptics off below 2° per step | Microsoft's guidance against haptics at fine resolution |
| 2026-11-10 | uinput off and not shipped active (ADR-0004, threat T7) | Least privilege |
| 2026-11-10 | JSON strings over D-Bus (ADR-0005) | Schema evolution without signature breaks |
| 2026-11-10 | Media tool always shown; `requires: mpris` is opt-in per tool | Windows always shows Media; no surprise menu changes |
| 2026-11-10 | User config never removed on uninstall or purge | SRS §13 |
| 2026-11-23 | Commands: one process per action fingerprint; `overlap` defaults to coalesce on rotation, queue (4) otherwise | A fast turn must not fork without bound, but deliberate presses should not be lost |
| 2026-11-23 | `overlap` is optional, so no config version bump | Older files load unchanged and keep their approvals; older versions ignore the field |
| 2026-11-10 | Hardware rows stay unverified until physically tested | SRS §17.6 and §17.8 |
| 2026-11-10 | Treat Hyprland's `[[EMPTY]]` as unset, and retry the keymap with the layout only | Found in E2E: injection failed completely on Hyprland |
