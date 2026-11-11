# Replay fixtures

| File | Origin | Content |
|---|---|---|
| `synthetic-click-rotate.jsonl` | **Synthetic** (hand-written from the HID descriptor / kernel mapping, NOT captured from hardware) | connect, click, 3 CW + 2 CCW detents, long press, hold-and-turn |
| `synthetic-evtest.txt` | **Synthetic**, evtest text format | rotation burst with a SYN_DROPPED in the middle |
| `synthetic-reconnect-flood.jsonl` | **Synthetic** | buffered rotation flood immediately after connect (upstream surface-dial-linux issue #15) |

Real-hardware captures must be added with `opendialctl record tests/replay-fixtures/hw-<date>-<kernel>.jsonl`
(see `tests/hardware/README.md`). Captures contain only EV_KEY/EV_REL/EV_SYN codes and
timestamps; no device addresses or user data.
