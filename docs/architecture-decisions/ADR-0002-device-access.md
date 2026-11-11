# ADR-0002: Read evdev, write hidraw, grant access with udev uaccess

- **Status:** accepted, 2026-11-10
- **Context:** The kernel already decodes the Dial into `BTN_0`/`REL_DIAL`. Haptics and resolution need HID feature/output reports, which evdev cannot send. A second consumer of the same events must be avoided (SRS §4).
- **Decision:**
  - Read the "Surface Dial System Multi Axis" evdev node and grab it (`EVIOCGRAB`, configurable), so the desktop does not also scroll.
  - Write reports to the matching hidraw node on a worker thread.
  - Access comes from a udev rule that tags only `0005:045E:091B` nodes with `uaccess` (logind ACL for the active seat).
- **Consequences:**
  - No root daemon, no group membership.
  - The ACL follows seat changes.
  - Without the rule, input still works for users in `input` (common on Arch), but haptics are reported as unavailable.
  - Not chosen: hidapi (an extra dependency for two ioctls) and BlueZ GATT directly (it would fight the kernel HOGP driver).
