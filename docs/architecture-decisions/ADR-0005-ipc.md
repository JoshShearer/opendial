# ADR-0005: Session D-Bus API with JSON payloads; optional loopback HTTP

- **Status:** accepted, 2026-11-10
- **Context:** The GUI, CLI, plugins and scripts all need state, control and events. The schema will evolve.
- **Decision:**
  - Name `io.github.joshshearer.opendial`, object `/io/github/opendial/Daemon`, versioned interface `io.github.joshshearer.opendial.Daemon1`.
  - Methods take and return JSON strings, so the schema can grow without D-Bus signature breaks, and every language can use it.
  - Signals: StateChanged, ConfigChanged, GestureEvent, RawEvent (opt-in), PluginEvent, MenuChanged.
  - An HTTP/SSE API on 127.0.0.1 with a bearer token, off by default, for environments without D-Bus bindings.
- **Consequences:**
  - Type checking happens in the validators, not in D-Bus introspection.
  - A breaking change means a new `Daemon2` interface next to `Daemon1`.
