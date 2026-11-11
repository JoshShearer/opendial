# ADR-0008: Declarative plugin tools over D-Bus with user consent

- **Status:** accepted, 2026-11-10
- **Decision:**
  - Registration: an app calls `RegisterApp` with `{appId, name, matchAppIds, tools:[{id,name,icon,…}]}`.
  - Visibility: the tools appear as `plugin:<appId>:<toolId>` only after the user allows the app; consent is persisted.
  - Events: the daemon emits `PluginEvent(appId, {toolId, slot, steps, event})`, and the app can answer with `PluginFeedback` (OSD text, haptic).
  - Cleanup: registration ends when the app's bus name disappears.
  - Plugins never get raw input, never see other apps' tools, and cannot run actions in the daemon.
- **Consequences:**
  - Works from any language with D-Bus (examples: Python, JS via HTTP/SSE, Rust).
  - Windows RadialController apps are not supported as-is; the capability is reproduced natively instead.
