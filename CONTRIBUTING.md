# Contributing

## Build and test

```sh
cmake -S . -B build -G Ninja -DOPENDIAL_WERROR=ON
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```
Integration tests run on a private session bus (`dbus-run-session`) and never
inject real input (`--dry-run`). Optional E2E check on Hyprland:
`tests/hardware/nested-hyprland-injection.sh`.

## Rules

- **Never claim hardware success from simulation.** Changes that affect device behaviour need a hardware run recorded in `VERIFICATION.md`, and an update to `docs/parity-matrix.md`.
- Add a test with every bug fix.
- Logic goes into `src/core` (pure, unit-tested). I/O goes into `src/device`, `src/inject`, `src/context`, `src/integrations`.
- No new privileges: no root helpers, no wider udev rules, no shell execution of strings.
- Config schema changes: bump the version, add a migration in `migrateConfig`, and keep older files loadable.
- SPDX header (`MIT`) on every new file.
- Commit messages: imperative summary line, then the why.

## Recording a fixture

```sh
opendialctl record my-case.jsonl --seconds 10
```
Check the file for anything identifying, add it under `tests/replay-fixtures/`
with a line in that README, and add a case to `tests/integration/test_replay.py`.
