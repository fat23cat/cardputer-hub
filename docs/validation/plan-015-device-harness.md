# Plan 015 Cardputer-Adv BLE HID Validation Harness

This runbook validates the BLE keyboard and consumer-control transport on a
physical Cardputer-Adv. The opt-in image extends the plan-014 pairing harness;
it is not production firmware and never routes the Cardputer keyboard to a
host automatically.

Do not record peer names, addresses, bond references, pairing values, Wi-Fi
credentials, or typed host content. Use a blank host-side key viewer and a
disposable 2.4 GHz test network.

## 1. Build, flash, and pair

Activate ESP-IDF 5.5.5 and choose the exact device port as described in the
plan-014 runbook, then build the dedicated image:

```bash
CARDPUTER_HUB_BUILD_TYPE=validation-015 idf.py -B build-validation-015 \
  -D IDF_TARGET=esp32s3 \
  -D CARDPUTER_HUB_PLAN_015_VALIDATION=ON \
  build

idf.py -B build-validation-015 \
  -p "$CARDPUTER_VALIDATION_PORT" \
  flash monitor
```

Wait for `[VALIDATION 015] BLE HID validation harness active`. Run `help`,
then use `bt enable`, `pair open`, and the Cardputer display/keyboard pairing
flow from [`plan-014-device-harness.md`](plan-014-device-harness.md). Run
`bonds`, then `bond select <index>` for the test host. A host normally enables
both HID notifications while connecting; `status` must reach `hid=ready` only
after the selected authenticated bond reconnects and both subscriptions exist.

The harness prints `readiness` records with boolean encryption, authentication,
bond, keyboard/consumer subscription, and report-protocol flags. Use `hid`
events for the complete readiness snapshot; `security` events describe the
security result only. These diagnostics contain no peer identifiers or pairing
values and do not initiate security or change the connection state. `status`
also prints an adapter snapshot using the live link's security and the current
subscription/protocol state. Logging runs from the harness's polling task;
Bluetooth callbacks do not write to serial.

## 2. Keyboard and release reports

Open a host-side HID key viewer with no sensitive application focused. For
each press command below, run `hid release` afterward and confirm every shown
usage and modifier returns to neutral:

```text
hid key
hid shift
hid ctrl
hid alt
hid gui
hid six
```

Expected results are one printable keyboard usage, that usage with each of the
four left-side modifier bits, and six simultaneous printable usages. Every
accepted command reports only the numeric send-result enum to serial; it must
not echo the report payload.

While `hid shift` remains held, disconnect Bluetooth from the host. Confirm the
host viewer shows no stuck modifier. Reconnect the selected host and confirm
the held report is not replayed. Run `hid release` after readiness returns.

## 3. Consumer controls

With host volume and media playback in a safe test state, exercise each
supported validation command followed by `hid release`:

```text
hid volume-up
hid volume-down
hid mute
hid play-pause
hid next
hid previous
```

Confirm the matching host control occurs once and release causes no repeated
action. Host policies may ignore next/previous when no media session exists;
in that case use a local disposable media session and repeat.

## 4. Lifecycle, selection, and coexistence

Repeat at least five cycles of `bt disable`, `bt enable`, host reconnect, and
`status`. HID must begin unavailable or starting and become ready only after
the current selected host subscribes again. Send no report while status is not
ready; an attempted validation send must return NotReady and must not appear in
the host viewer.

The current implementation resumes advertising after every disconnection,
including the host's explicit Disconnect control. Automatic reconnection in
that case is permitted by the revised Cardputer-side On/Off policy in plan 017;
it does not count as validation of local Off. Earlier Mac-only Disconnect/Connect
failures remain recorded as historical evidence. The harness records the numeric
NimBLE disconnect reason from its polling task. Keep host-requested disconnect,
unexpected link loss, and persistent product Off as separate acceptance cases. Use `bt disable` to keep the device disconnected during a
controlled pause, and `bt enable` to resume. After a
device Reset, this opt-in harness starts with Bluetooth disabled: enable it,
run `bonds`, and select the intended session index again without deleting bonds.

Pair a second controlled host through the plan-014 flow. While the first host
is ready, select the second bond. Confirm the first host receives neutral
state before disconnection, only the selected host can reconnect, and no old
report is replayed. Switch back and repeat once.

Run `wifi connect`, enter the disposable network credentials, and sustain
ordinary local network traffic. While traffic is active, repeat the keyboard,
six-key, modifier, consumer, and release checks. Confirm both Wi-Fi and BLE HID
remain responsive without reset, watchdog, or boot loop. Finish with
`wifi disconnect`.

### BLE-only cable and pairing regression checks

The final BLE-only image uses the fixed USB Serial/JTAG console and must not
enumerate a USB keyboard. With the Cardputer running from its battery and the
selected BLE host ready, unplug and reconnect USB at least five times. Confirm
BLE still accepts reports, releases neutralize them, the saved bond is not
changed, and serial diagnostics return. No output channel switch is involved.

During an explicit pairing window, interrupt an incomplete host connection.
The device must clear the obsolete challenge and resume advertising within
the same 120-second deadline. Cancel or allow the window to expire and verify
that it stays closed. Do not erase existing bonds merely to test USB removal.

## 5. Privacy audit and cleanup

Retain the complete serial output from boot through cleanup. Search for MAC
address patterns and manually inspect Bluetooth, controller, NimBLE, SMP, GAP,
ATT, HID, and storage records. The log must contain no peer identity, bond
reference, report bytes, usage values sent by the test, pairing value, or Wi-Fi
credential.

Remove all test bonds, remove the Cardputer from each host, exit the monitor,
and restore production firmware:

```text
bt enable
bonds remove-all
bonds
```

```bash
make upload UPLOAD_PORT="$CARDPUTER_VALIDATION_PORT"
```

After reset, the validation banner must be absent and the normal Cardputer Hub
startup screen followed by Home must appear. Run plan 017's product
acceptance separately: the diagnostic harness does not exercise HostService's
persisted selection or the local settings controls.

## Result record

Record only non-identifying outcomes:

```text
authenticated reboot reconnect: pass/fail
ready requires selected peer and both subscriptions: pass/fail
printable/modifier/six-key reports: pass/fail
keyboard and consumer release: pass/fail
volume/media controls: pass/fail
held-modifier disconnect leaves host neutral: pass/fail
interrupted report replay after reconnect: yes/no
repeated subscribe/disconnect/disable/re-enable: pass/fail
selected-host switch and old-host neutralization: pass/fail
Wi-Fi coexistence: pass/fail
USB cable cycles preserve BLE and serial returns: pass/fail
USB keyboard enumerated: yes/no
interrupted pairing resumes without extending deadline: pass/fail
resets or watchdogs: count
identity/reference/report/key/pairing values in logs: yes/no
test bonds removed and production firmware restored: yes/no
```
