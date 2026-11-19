# CLAUDE.md

Guidance for AI coding agents working in this repository.

- **What this is:** OpenDial, a Surface Dial suite for Linux. The SRS is `docs/SRS.md`. Project state: `docs/dev/PLAN.md`, `docs/dev/TASKS.md`, `docs/dev/DECISIONS.md`, `VERIFICATION.md`, `docs/parity-matrix.md`.
- **Build:** `cmake -S . -B build -G Ninja && cmake --build build`.
- **Test:** `QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure`. All 13 suites must pass.
- **Layout:** `src/core` pure logic, `src/device` evdev/hidraw, `src/inject` key/pointer backends, `src/context` focus trackers, `src/integrations` audio/MPRIS/brightness/BlueZ/session, `src/daemon` engine + D-Bus + HTTP + overlay, `src/cli`, `src/ui` (QML).
- **Safe experimentation:** `dbus-run-session -- ./build/src/daemon/opendiald --dry-run --no-device`, with `XDG_CONFIG_HOME`/`XDG_STATE_HOME` pointed at a temp dir. Never run against the user's real config or session bus without asking.
- **Debugging Qt:** set `QT_FORCE_STDERR_LOGGING=1`, otherwise logs go to journald. Offscreen screenshots don't draw Material button backgrounds; that is not a bug.
- **Honesty rule:** simulated or replayed input is never hardware evidence. Update the parity matrix only with real runs.
- **No privileged steps:** installing udev rules or packages needs the user. Ask them.
