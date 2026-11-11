# ADR-0001: C++20 + Qt 6 instead of a Rust workspace

- **Status:** accepted, 2026-11-10
- **Context:** The SRS recommends a Rust service plus a Qt 6/QML GUI, and allows alternatives if justified. The overlay, the settings GUI and the tray all need Qt anyway. Wayland layer-shell (LayerShellQt), D-Bus (QtDBus) and the event loop integration are native in C++.
- **Decision:** One C++20 code base with Qt 6.4+:
  - `opendialcore`: pure logic, no I/O, unit-tested
  - `opendialsvc`: device, injection, context, integrations
  - `opendiald`, `opendialctl`, `opendial` (GUI)
- **Consequences:**
  - One toolchain per distro, and distro-native Qt packages.
  - No cxx-qt FFI layer.
  - Memory-safety tooling instead: ASan/UBSan CI job, bounded parsers, garbage-input tests.
  - The SRS's crate names map to directories: `src/device` (opendial-device), `src/core` (gesture, haptics policy, profile), `src/context`, `src/inject` + `src/integrations` (actions), `src/daemon` (ipc, daemon), `src/cli`, `src/ui`, `tests` (testkit).
