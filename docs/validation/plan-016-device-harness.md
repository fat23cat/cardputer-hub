# Plan 016 Native USB Device Harness

This opt-in firmware is for mandatory physical validation of the native USB
CDC/HID transport. It is not enabled in production builds and never routes the
Cardputer keyboard automatically.

## Build and install

Use the exact ESP-IDF 5.5.5 environment, then build in a separate directory:

```bash
source "$HOME/.espressif/frameworks/esp-idf-v5.5.5/export.sh"
idf.py -B build-validation-016 -D IDF_TARGET=esp32s3 \
  -D CARDPUTER_HUB_PLAN_016_VALIDATION=ON build
idf.py -B build-validation-016 -p /dev/ttyACM0 flash
```

After reset, locate the application CDC port again; it may differ from the ROM
download port. Connect the monitor explicitly when automatic selection is
ambiguous:

```bash
idf.py -B build-validation-016 -p /dev/ttyACM1 monitor
```

If automatic reset cannot reach ROM download mode, hold `G0`, tap reset,
release `G0`, and repeat the flash command with the ROM port. If flashing
finishes but the device remains on the ROM port, tap reset once more without
holding `G0`, then locate the application CDC port again.

## Commands

The CDC console accepts:

| Command | HID report |
| --- | --- |
| `status` | Print the transport state and whether an unavailable interval followed readiness |
| `key` | Shift plus keyboard usage `0x04` |
| `six` | Six keyboard usages, `0x04` through `0x09` |
| `consumer` | Consumer usage `0x00E9` (volume increment) |
| `release` | Neutral keyboard and consumer reports |

Use a host-side HID viewer and always follow `key`, `six`, or `consumer` with
`release`. The console intentionally prints only state and result codes; it
does not echo report payloads, host identity, or control transfers.

The validation screen shows the live hardware-neutral state as
`USB: unavailable`, `USB: starting`, `USB: ready`, `USB: busy`, or
`USB: error`. It redraws only when that state changes.

## Validation checklist

- Inspect the device and configuration descriptors: VID `0x303A`, PID
  `0x4005`, three interfaces, four unique endpoints, product `Cardputer Hub`,
  and no serial-number string.
- Confirm CDC logging and HID reports work simultaneously.
- Exercise cable power without enumeration, suspend/resume, repeated hotplug,
  application reset, normal upload, and `G0` recovery.
- After a ready host session, move the device to a power-only source and then
  reconnect it to the host. `status` must report
  `unavailable_after_ready=yes`.
- Confirm a disconnected or deliberately unread monitor does not delay display
  refresh, keyboard polling, or HID reports.
- Repeat display, keyboard, microSD, serial, and IR production smoke checks.
- Record macOS results and, where available, one additional host family.
- Audit descriptors and logs for unique device identity, report contents, and
  USB control-transfer contents.

Record physical results in the plan implementation record. Do not mark an
unavailable host family or unperformed check as passed.
