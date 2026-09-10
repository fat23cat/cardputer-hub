# Plan 017 HID Transport Routing Device Harness

This opt-in firmware validates USB-first HID routing and handover on a physical
Cardputer-Adv. It reuses the authenticated BLE pairing controls from the plan
014/015 harness and composes the production `HidTransportRouter` with the real
BLE and native USB transports. The production firmware does not include these
validation key mappings.

Do not record host names, addresses, bond references, report contents, pairing
values, typed content, or Wi-Fi credentials. Use blank host-side HID viewers.

## Build and install

Activate ESP-IDF 5.5.5, then build the dedicated image:

```bash
CARDPUTER_HUB_BUILD_TYPE=validation-017 idf.py -B build-validation-017 \
  -D IDF_TARGET=esp32s3 \
  -D CARDPUTER_HUB_PLAN_017_VALIDATION=ON \
  build

idf.py -B build-validation-017 -p "$CARDPUTER_VALIDATION_PORT" flash monitor
```

Use `bt enable`, `pair open`, the on-device pairing flow, `bonds`, and
`bond select <index>` to prepare the selected BLE host. `status` prints only
numeric transport states and non-identifying lifecycle state.

The validation-only local controls work without the CDC cable:

| Control | Transaction |
| --- | --- |
| `Fn+1` (`F1`) | Shift plus keyboard usage, neutralized after five seconds |
| `Fn+2` (`F2`) | Volume-increment consumer usage, neutralized after five seconds |
| `Fn+3` (`F3`) | Cancel the active transaction and request neutral release |

The existing serial `hid ...` commands submit the same five-second routed
transactions when CDC is available. Report payloads are never printed.

## Handover checklist

- With the selected BLE host ready and USB data disconnected, press `Fn+1` or
  `Fn+2`; attach USB during the five-second dwell. BLE must receive release, USB
  must not receive the interrupted transaction, and later input must use USB.
- Start a USB transaction, remove USB during the dwell, and confirm no replay
  on BLE and no stuck state on either host. A later transaction may use only
  the selected BLE host.
- Suspend USB during a transaction. Confirm the router waits, releases after
  resume, or treats a confirmed unmount as neutralization before accepting new
  output.
- Confirm cable power without enumeration and mounted-but-suspended USB do not
  take ownership.
- Select a different controlled BLE bond while USB owns output, remove USB,
  and confirm only the newly selected bond receives later transactions.
- Make the selected bond unavailable while another bond exists. Confirm no HID
  output is redirected to the other bond.
- Repeat attachment, removal, BLE disconnect/reconnect, suspend, and resume
  cycles while checking for stale callbacks, duplicates, stuck controls,
  watchdogs, resets, and monotonic heap loss.
- Keep Wi-Fi traffic, BLE HID, USB CDC, and USB HID active together for at least
  one hour. Record minimum free heap and repeat the production display,
  keyboard, microSD, serial, and IR smoke checks afterward.

## Result record

Record only non-identifying outcomes:

```text
USB-ready precedence: pass/fail
BLE-to-USB release before switch: pass/fail
cancelled transaction replayed on USB: yes/no
USB unmount neutralization: pass/fail
interrupted USB transaction replayed on BLE: yes/no
USB suspend/resume release: pass/fail
USB suspend/unmount neutralization: pass/fail
power-only USB ownership: yes/no
unavailable selected bond redirected: yes/no
CDC remained usable: pass/fail
one-hour coexistence soak: pass/fail
minimum free heap: <bytes>
resets/watchdogs/duplicates/stuck controls: <counts>
identity or report content found in logs: yes/no
production smoke checks after soak: pass/fail
```

After validation, remove test bonds and restore the production firmware with
`make upload UPLOAD_PORT="$CARDPUTER_VALIDATION_PORT"`. The validation banner
must be absent after reset.
