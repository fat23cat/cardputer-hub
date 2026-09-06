# Plan 012 Local Device Validation

This guide validates the real ESP32 Wi-Fi and ESP-NimBLE adapters on a
Cardputer-Adv without requiring a firmware UI.

The harness is local-only and disabled by default. Normal `make build`, CI, and
release firmware do not include or start it. Plan 012 does not implement
pairing or HID: an unbonded BLE connection must be rejected. Pairing and
authenticated reconnection belong to plan 014.

## Know where to perform each action

This guide uses three different places. Follow the label at the start of each
step:

- **Computer terminal (shell):** a macOS or Linux terminal, with the current
  directory set to the Cardputer Hub repository root.
- **Serial monitor:** the same terminal after `idf.py ... flash monitor` starts.
  Type harness commands such as `status` directly and press Enter. Do not type
  a shell prompt character such as `$` or `>`.
- **Phone/computer BLE scanner:** an external BLE scanning application. Use a
  raw BLE scanner, not only the operating system's Bluetooth pairing screen.

Keep the USB cable connected and the serial monitor open during each test.

## 1. Prepare the computer

### First-time setup only

**Computer terminal (shell):** open a terminal and run:

```bash
cd /path/to/cardputer-hub
git submodule update --init --recursive
bash scripts/install_esp_idf.sh \
  "$HOME/.espressif/frameworks/esp-idf-v5.5.5"
```

### At the start of every new terminal session

**Computer terminal (shell):** run:

```bash
cd /path/to/cardputer-hub
source "$HOME/.espressif/frameworks/esp-idf-v5.5.5/export.sh"
```

Connect the Cardputer-Adv with a data-capable USB cable and close any other
program using its serial port. List available ports:

```bash
python -m serial.tools.list_ports
```

Choose the Cardputer port from the output and save it in a shell variable. For
example, on macOS:

```bash
CARDPUTER_VALIDATION_PORT=/dev/cu.usbmodem1234
```

On Linux it will usually resemble `/dev/ttyACM0`. Replace the example with the
port that exists on your computer. If the device is unplugged or changes ports,
run the list command again and reassign `CARDPUTER_VALIDATION_PORT`.

## 2. Build, flash, and open the serial monitor

**Computer terminal (shell):** from the repository root, build the validation
image in its own directory:

```bash
CARDPUTER_HUB_BUILD_TYPE=validation-012 idf.py -B build-validation-012 \
  -D IDF_TARGET=esp32s3 \
  -D CARDPUTER_HUB_PLAN_012_VALIDATION=ON \
  build
```

Then flash it and start the serial monitor:

```bash
idf.py -B build-validation-012 \
  -p "$CARDPUTER_VALIDATION_PORT" \
  flash monitor
```

The terminal is now the **serial monitor**, not a shell. Wait for this banner:

```text
[VALIDATION 012] local Wi-Fi and ESP-NimBLE harness active
```

If the banner does not appear, press the Cardputer reset button once. If it is
still absent, stop: the validation image is not running.

If flashing cannot reset the device automatically, hold `G0`, press and release
reset, release `G0`, and run the flash command again. Do not run
`make migrate-storage-layout`; this harness uses the normal partition layout
and does not require an erase.

Press `Ctrl+]` at any time to exit the serial monitor and return to the shell.

## 3. Confirm the harness is ready

**Serial monitor:** type:

```text
status
```

Before testing, the expected state is:

```text
wifi=idle bluetooth=disabled
```

The status line also reports heap information and, when connected, Wi-Fi RSSI.
Type `help` to see every available command.

## 4. Check Wi-Fi

Use a disposable 2.4 GHz test access point where practical.

1. **Serial monitor:** type `wifi connect` and press Enter.
2. Wait for the SSID prompt. Type the test network name and press Enter.
3. Wait for the passphrase prompt. Type its passphrase and press Enter, or
   press Enter on an empty line for an open network.
4. Wait for `Wi-Fi state=connected`.
5. **Serial monitor:** type `status`. Confirm it reports `wifi=connected` and a
   plausible RSSI.
6. **Serial monitor:** type `wifi disconnect`. Wait for `Wi-Fi state=idle`.

Type `/cancel` at either credential prompt to abort. The harness does not log
or persist the credentials, but the terminal may echo what you type. Remove any
echoed credentials before saving or sharing a transcript.

**Pass:** Wi-Fi reaches `connected`, `status` confirms it, and disconnecting
returns it to `idle` without a reset or adapter error.

**Fail:** a known-good access point produces an adapter error or endless retry,
or the device resets or triggers a watchdog.

This test proves station association and IP configuration. It is not a
sustained application-traffic benchmark.

## 5. Check BLE advertising and unbonded rejection

1. **Serial monitor:** type `bt enable`.
2. Wait for `Bluetooth state=advertising`.
3. **Phone/computer BLE scanner:** start a BLE scan and find
   `Cardputer Hub 012 Test`.
4. In the scanner, attempt to connect once.
5. **Serial monitor:** confirm the log reports `unbonded peer rejected` and does
   not print an address or peer identity.
6. Wait for `Bluetooth state=advertising` again.
7. **Serial monitor:** type `bt disable`. Confirm the state becomes `disabled`.
8. **Phone/computer BLE scanner:** refresh the scan and confirm the
   advertisement disappears.
9. **Serial monitor:** type `bt enable`; wait for advertising and confirm the
   advertisement reappears in the scanner.

**Pass:** the device advertises, rejects the unbonded connection without
identity-bearing logs, advertises again, and can be disabled and re-enabled.

Do not expect an operating-system pairing prompt, passkey, HID service, or
accepted connection from this plan-012 image.

## 6. Run the BLE lifecycle test

Run this test with Wi-Fi disconnected so it isolates the Bluetooth lifecycle.

First perform one warm-up cycle:

**Serial monitor:**

```text
bt cycle 1
```

Wait for `CYCLE PASS completed=1/1`. The first initialization intentionally
keeps the Bluetooth controller initialized across a logical disable, so a
one-time heap reduction during this warm-up is expected.

Now run the measured test:

**Serial monitor:**

```text
bt cycle 100
```

Wait for the command to finish. The harness reports progress every ten cycles.
The final line must begin with:

```text
[VALIDATION 012] CYCLE PASS completed=100/100
```

Record the initial and final heap values from this 100-cycle run and compare
those two values. `boot_minimum` is the lowest historical value since boot and
can only decrease; a lower `boot_minimum`, or the one-time warm-up reduction,
is not by itself evidence of a leak.

**Pass:** all 100 cycles finish, the measured final heap is stable relative to
the measured initial heap, and there is no reset, watchdog, or adapter error.

**Fail:** a cycle times out or fails, the measured final heap falls repeatedly
across reruns, or a reset/watchdog occurs.

## 7. Check Wi-Fi and BLE coexistence

This is a long-running check. Do the two-minute smoke test before committing to
the one-hour test.

1. **Serial monitor:** run `wifi connect`, enter the test credentials when
   prompted, and wait for `Wi-Fi state=connected`.
2. **Serial monitor:** type `coexist 2`.
3. Keep the USB cable and serial monitor connected. Do not attempt a BLE
   connection during the watch; the expected rejection would count as an
   advertising interruption.
4. **Phone/computer BLE scanner:** refresh the scan during the two minutes and
   confirm `Cardputer Hub 012 Test` remains visible.
5. **Serial monitor:** wait for a final line beginning with `COEXIST PASS` and
   confirm it reports zero Wi-Fi and Bluetooth interruptions.
6. If the smoke test passes, type `coexist 60`.
7. Leave the device powered and the serial monitor open for the full hour.
   Periodically refresh the external BLE scan without connecting.
8. Wait for the final `COEXIST PASS` line and confirm both interruption counts
   are zero.

The harness prints progress and a heap sample once per minute. Use
`coexist stop` only when intentionally aborting a run.

**Pass:** the two-minute and 60-minute runs end with `COEXIST PASS`, report zero
interruptions, Wi-Fi remains connected, and BLE remains advertising.

**Fail:** either state is interrupted, a final line reports `COEXIST FAIL`, or
the device resets or triggers a watchdog.

This validates concurrent Wi-Fi association and BLE advertising only. It does
not generate sustained application traffic, so it does **not** complete plan
012's separate one-hour traffic criterion. That criterion remains pending
until a traffic-generating diagnostic is added.

## 8. Restore production firmware

**Serial monitor:** press `Ctrl+]` to exit.

**Computer terminal (shell):** from the repository root, flash the normal
production firmware:

```bash
make upload UPLOAD_PORT="$CARDPUTER_VALIDATION_PORT"
```

After reset, the validation banner must be absent. The normal display should
show `Cardputer Hub` and its version.

## Result checklist

| Test | Pass condition |
| --- | --- |
| Harness boot | Validation banner appears; initial status is Wi-Fi idle and Bluetooth disabled |
| Wi-Fi | Connects to the test AP and returns to idle after disconnect |
| BLE behavior | Advertises, rejects an unbonded peer, re-advertises, disables, and re-enables |
| BLE lifecycle | Warm-up passes; measured run reports `completed=100/100` with stable heap |
| Coexistence smoke | Two-minute run passes with zero interruptions |
| Coexistence watch | 60-minute run passes with zero interruptions |
| Privacy | No peer identity or credentials appear in the retained log |
| Production restore | Validation banner is absent and normal UI starts |

Record only non-identifying results:

```text
device: Cardputer-Adv
production restore smoke test: pass/fail
Wi-Fi state test: pass/fail
BLE advertise/reject/re-advertise: pass/fail
BLE lifecycle warm-up: pass/fail
BLE lifecycle measured cycles: N/100
coexistence smoke duration and interruptions: ...
coexistence watch duration and interruptions: ...
measured lifecycle initial/final heap: ...
resets or watchdogs: ...
identity-bearing logs observed: yes/no
```

Do not record SSIDs, passphrases, IP addresses, MAC addresses, BLE identities,
or other host identifiers.

## Troubleshooting

- **Serial port is missing or changed:** reconnect the USB cable, run
  `python -m serial.tools.list_ports` in the shell, and reassign
  `CARDPUTER_VALIDATION_PORT`.
- **Flash cannot reset the device:** hold `G0`, press and release reset, release
  `G0`, and rerun the flash command.
- **Validation banner is missing:** press reset once. If it remains missing,
  rebuild with the validation option and flash the `build-validation-012`
  image again.
- **BLE advertisement is not visible:** wait for
  `Bluetooth state=advertising`, refresh a raw BLE scanner, and do not rely only
  on the operating system's paired-device list.
- **Wi-Fi will not connect:** recheck the test credentials and confirm that the
  access point supports 2.4 GHz Wi-Fi.
- **An `adapter-error`, reset, or watchdog appears:** save a credential- and
  identity-redacted log, reset the device, and rerun only the single failing
  check before continuing.
