# Surface Dial hardware protocol

**Last updated:** 2026-11-10.

**Evidence levels used here:**
- **observed**: seen on the reference unit
- **source**: taken from kernel or community driver source
- **spec**: from the HID usage tables

## Identity

| Property | Value | Evidence |
|---|---|---|
| Transport | Bluetooth Low Energy, HID over GATT (HOGP) | observed: GATT service UUIDs 0x1812 (HID) and 0x180F (Battery) listed by BlueZ |
| HID bus/vendor/product | `0005:045E:091B` (BUS_BLUETOOTH, Microsoft, Surface Dial) | observed: BlueZ modalias `usb:v045Ep091Bd0108` (version 0x0108) |
| BLE address | Random static. Treated as **sensitive** (redacted in exports) and **not** used as a stable identity | SRS §9; diagnostics redact `uniq` |
| Kernel driver | `hid-generic` via `hid-input`. `hid-microsoft` carries no quirk for 091B in current kernels; the generic HID mapping produces the events below | source: `drivers/hid/hid-input.c`, `hid-ids.h` |

## Input nodes

`hid-input` creates two evdev nodes for the device:

| Node name | Events | Use in OpenDial |
|---|---|---|
| `Surface Dial System Multi Axis` | `EV_KEY BTN_0` (press = 1, release = 0); `EV_REL REL_DIAL` (signed; + = clockwise); `EV_MSC MSC_SCAN`; `SYN_REPORT` | Main input. Opened read-only; optionally `EVIOCGRAB`bed (setting `device.grabDevice`, default on) so the desktop does not also treat the dial as a wheel |
| `Surface Dial System Control` | System control usages (unused by the hardware in practice) | Shown in diagnostics only |

The decoder (`src/core/DialDecoder.cpp`):
- collects events into frames that end at `SYN_REPORT`
- drops everything from `SYN_DROPPED` until the next `SYN_REPORT`, as the evdev protocol requires
- counts malformed frames

The device must report `REL_DIAL` or it is refused (`DialDevice::openNode`).

### Reconnect flood

After waking, the Dial can send the rotation it buffered while asleep in one
burst. Rotation is dropped for `device.connectGuardMs` (default 800 ms) after
each connect. The fixture `tests/replay-fixtures/synthetic-reconnect-flood.jsonl`
reproduces this. *Source:* surface-dial-linux issue history. *Not yet observed
on the reference unit.*

## HID reports (hidraw)

All reports use report ID 1. hidraw access needs the udev rule
(`packaging/udev/70-opendial-surface-dial.rules`). Without it, haptics are
reported as unavailable and input still works over evdev.

### Input report 1 (diagnostics only)

```
byte 0     report id = 0x01
byte 1     bit0 = button, bit1 = touch (on-screen contact; Surface Studio only)
byte 2..3  int16 LE dial delta
byte 4..5  int16 LE X   (optional; only with an on-screen contact)
byte 6..7  int16 LE Y   (optional)
byte 8     u8 contact width (optional)
```
*Source:* surface-dial-linux and the report descriptor layout. On ordinary
displays the X/Y fields are absent or zero. That is why WP-20 is
`unsupported-hardware`.

### Feature report 1: resolution and automatic haptics (write: `HIDIOCSFEATURE`)

```
byte 0     0x01
byte 1..2  u16 LE resolution multiplier: subdivisions per full turn (1..3600)
byte 3     repeat count (0)
byte 4     auto-trigger waveform: 0x03 = click on every subdivision, 0x02 = none
byte 5     waveform cutoff time (0)
byte 6..7  u16 LE retrigger period (0)
```
- After writing, the report is read back (`HIDIOCGFEATURE`) and the applied resolution is compared with the requested one. A mismatch is shown in the UI as "applied vs requested".
- `REL_DIAL` units per turn follow the resolution, so the decoder converts units to degrees as `360 / resolution` (`HardwareMode::unitsToDegrees`).
- *Source:* HID usage tables, Haptics page 0x0E (auto trigger, retrigger period, waveform list), and surface-dial-linux `haptics.rs`.
- *Status:* encoder unit-tested. **Not yet written to the reference unit**, because hidraw was not accessible before the udev rule was installed.

### Output report 1: manual haptic pulse (write)

```
byte 0     0x01
byte 1     repeat count
byte 2     manual trigger waveform: 0x03 click, 0x04 buzz (continuous), 0x05 rumble (continuous)
byte 3..4  u16 LE retrigger period in ms (clamped to 2000)
```
- Used for menu ticks, selection and boundary feedback, and the "Test haptics" buttons.
- Writes run on a dedicated thread (`HapticsWorker`), so a slow BLE round trip never stalls input (SRS §5.2).
- *Status:* as for the feature report.

## Haptics policy

`hardwareModeFor()` in `src/core/Rotation.cpp`:
- **Haptics off** (global or per tool): resolution 3600, auto-trigger off. This gives the finest software control.
- **Haptics on:** resolution `round(360 / detent)`. A detent finer than `haptics.minDetentDegrees` (default 2°) is clamped. That follows Microsoft's guidance against haptic feedback at fine resolution, and the UI says the clamp was applied.

## Battery

- The HID descriptor carries no battery usage, so the kernel creates no `power_supply` node.
- BlueZ exposes the GATT Battery Service as `org.bluez.Battery1.Percentage`. The value is read from BlueZ and shown as reported (observed: 99 %).
- When neither source exists, the UI shows **not reported**, never an estimate.

## Verification still to do on hardware

Use `tests/hardware/README.md` → "Protocol capture":
1. `opendialctl record dial.jsonl --seconds 20` while turning 1, 10 and 100 detents each way, then add the file to `tests/replay-fixtures/` after scrubbing.
2. Read the report descriptor (`/sys/bus/hid/devices/0005:045E:091B.*/report_descriptor`) and check it against the layouts above.
3. Write feature report 1 at 36 and 3600 steps; confirm the read-back and the felt detents.
