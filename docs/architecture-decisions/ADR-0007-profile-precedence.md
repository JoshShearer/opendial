# ADR-0007: Profile precedence and tool selection on focus change

- **Status:** accepted, 2026-11-10
- **Decision:**
  - Precedence: manual lock > window match (app + title regex/class) > app match > workspace > global. Within a layer: higher `priority` wins, then file order (deterministic).
  - `menuMode` decides whether app tools are shown in the menu: `replace` the global ones, or `extend` them (added before the global tools).
  - On a switch, the tool is either remembered per profile (default) or reset to the profile's `defaultTool`.
  - A focus change never re-runs an action. It only changes which bindings apply to the next gesture.
  - Secure windows and the lock screen suppress actions instead of switching.
- **Tested in:** `test_profiles`, `appProfileSwitching`, `rememberToolPerApp`, `lockedProfileIgnoresFocus`.
