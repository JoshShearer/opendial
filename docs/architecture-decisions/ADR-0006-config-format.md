# ADR-0006: Versioned JSON config with atomic writes, backups and read-only fallback

- **Status:** accepted, 2026-11-10
- **Context:** The SRS asks for strict validation, explicit versions, migrations, rollback, no silent destructive migration, and import/export.
- **Decision:**
  - Storage:
    - `~/.config/opendial/config.json` with `{"schema":"opendial.config","version":2}` (v2: `settings.menu.accent` may be `"system"`; the v1 default blue migrates to it, a colour the user picked stays).
    - Runtime state lives separately in `~/.local/state/opendial/state.json`.
    - Profiles are exchanged as `{"schema":"opendial.profile","version":1,"profile":{…}}`.
  - Writing:
    - Writes are QSaveFile (atomic) with mode 0600 and refuse symlinks.
    - The 5 previous versions are kept, and rollback walks back through them.
  - Failure cases:
    - A newer or invalid file is loaded **read-only**, with defaults, and never overwritten.
    - Corrupt JSON is moved aside and the newest valid backup is used.
- **Consequences:**
  - JSON rather than TOML: Qt has a native parser, and the same format is used on the wire.
  - Comments are not supported; the GUI is the primary editor.
