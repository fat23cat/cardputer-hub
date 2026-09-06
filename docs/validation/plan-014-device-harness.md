# Plan 014 Cardputer-Adv Validation Harness

This runbook validates authenticated BLE pairing and bond management on a
physical Cardputer-Adv. The harness is opt-in and must never be used as a
production image. Use a dedicated test device: cleanup deliberately removes
every Bluetooth bond created during the run.

Do not record peer names, addresses, passkeys, comparison values, opaque bond
references, Wi-Fi credentials, or other identifiers. Pairing values appear only
on the Cardputer display and passkey entry uses only its keyboard.

## 1. Prepare the device and peers

Use a Cardputer-Adv with the current `hub_config` partition layout, a data USB-C
cable, a microSD card, a disposable 2.4 GHz test network, two ordinary BLE host
platforms, and a controlled BLE central capable of selecting its SMP I/O
capability. The capacity test requires 17 distinct peer identities; a scripted
central may rotate test-only identities instead of using 17 physical hosts.

Activate ESP-IDF 5.5.5, list serial ports, and save the exact Cardputer port:

```bash
source "$HOME/.espressif/frameworks/esp-idf-v5.5.5/export.sh"
python -m serial.tools.list_ports
CARDPUTER_VALIDATION_PORT=/dev/cu.usbmodem1234
```

Use `/dev/ttyACM0`-style paths on Linux. Do not run the storage-layout migration
for routine validation because it erases the configuration partition.

## 2. Build, flash, and monitor

```bash
CARDPUTER_HUB_BUILD_TYPE=validation-014 idf.py -B build-validation-014 \
  -D IDF_TARGET=esp32s3 \
  -D CARDPUTER_HUB_PLAN_014_VALIDATION=ON \
  build

idf.py -B build-validation-014 \
  -p "$CARDPUTER_VALIDATION_PORT" \
  flash monitor
```

Wait for `[VALIDATION 014] authenticated pairing harness active`. Press reset
once if the banner was emitted before the monitor attached. The serial console
accepts `help`; record its output only after confirming the terminal is not
logging keyboard input containing credentials.

Result enum values follow the declarations in
`src/connectivity/bluetooth/bluetooth_service.h`. A result of zero is the first
successful outcome for enable, open, cancel, selection, and removal operations.

## 3. Pairing challenges, cancellation, and timeout

Enter `bt enable`, wait for `Bluetooth state=advertising`, then enter
`pair open`. Pair from controlled peers using these I/O combinations:

| Peer capability | Expected Cardputer flow |
| --- | --- |
| Keyboard only | Passkey shown on Cardputer; type it on the peer |
| Display only | Type the peer's six digits on Cardputer; Enter submits |
| Display with Yes/No | Matching number appears on both; Enter accepts, Esc rejects |

No pairing value may appear in serial output. Repeat numeric comparison once
with Esc and confirm the peer disconnects. Open pairing, connect but do not
complete the challenge, enter `pair cancel`, and confirm disconnection. Open it
again without connecting; measure from `pairing state=advertising` until the
window closes at 120 seconds. An incomplete peer must be rejected at timeout.

Attempt Just Works and legacy pairing with the controlled peer. Both must fail;
`status` must not report a new bond.

## 4. Persistence, selection, and deletion

Complete authenticated pairing with host A. Run `bonds`; it must report one.
Run `reference save 1`, reset the Cardputer, then:

```text
bt enable
reference verify
```

The result must be `reference stable=yes`, and host A must reconnect without a
new pairing exchange. Pair host B, run `bonds`, select host A's session index
with `bond select <index>`, and attempt to connect host B. Host B must be
rejected without replacing host A. If bond ordering is unclear, select one
index at a time and identify it only by which controlled host reconnects; never
record the mapping.

Remove the inactive bond using `bond remove <index>`, call `status` until the
operation is no longer pending, and confirm that peer can no longer reconnect.
Re-pair it, connect it, and remove its active bond; disconnection must precede
deletion. Exercise `bonds remove-all` and confirm `bonds` reports zero.

## 5. Capacity

Create 16 authenticated bonds with distinct controlled identities. After every
pairing, use `bonds` and confirm the count increases by one. At 16, `pair open`
must report the capacity result and the seventeenth identity must not pair.
Confirm `bonds` still reports 16 and previously bonded test peers still exist.
No bond may be silently evicted.

## 6. Failure isolation

Connect the disposable Wi-Fi network with `wifi connect`, verify a Cardputer
key increments `keyboard_events` in `status`, and run `storage check` with the
test microSD inserted. Then enter `fault bluetooth`. Bluetooth must enter error
while all of the following remain responsive:

- `status` continues updating and reports Wi-Fi connected;
- pressing a Cardputer key increments `keyboard_events`;
- the display redraws normally;
- `storage check` still passes;
- there is no reset, watchdog, or boot-loop stall.

The injected fault is validation-only and passes through every normal adapter
operation except the next event poll.

## 7. Privacy audit and cleanup

Retain the complete serial output from boot through cleanup. Search it for MAC
address patterns and manually inspect all Bluetooth, controller, NimBLE, SMP,
GAP, ATT, and storage records. The log must contain no peer identity, bond
reference, reference key, passkey, or comparison value.

Before restoring production firmware:

```text
bt enable
bonds remove-all
reference clear
bonds
```

Confirm zero bonds, then remove the Cardputer entry from every controlled peer.
Exit the monitor with `Ctrl+]` and restore the normal image:

```bash
make upload UPLOAD_PORT="$CARDPUTER_VALIDATION_PORT"
```

After reset, the validation banner must be absent and the normal Cardputer Hub
display must appear.

## Result record

Record only non-identifying outcomes:

```text
challenge display/entry/comparison: pass/fail
explicit reject/cancel/120-second timeout: pass/fail
Just Works and legacy rejection: pass/fail
reboot reconnect/reference stability: pass/fail
selected-peer enforcement: pass/fail
inactive/active/remove-all behavior: pass/fail
capacity count and seventeenth rejection: pass/fail
Wi-Fi/display/keyboard/storage isolation: pass/fail
resets or watchdogs: count
identity/reference/key/pairing values in logs: yes/no
test bonds and saved reference removed: yes/no
production firmware restored: yes/no
```
