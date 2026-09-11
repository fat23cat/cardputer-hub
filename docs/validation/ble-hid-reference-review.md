# BLE HID reference review — 2026-09-11

This source review follows the failed physical manual-disconnect tests. It does
not establish hardware acceptance or change the transport architecture. This is
a historical investigation record; the accepted product policy and current
results are in [plan 017](../plans/017-hid-transport-arbitration.md#0-current-closeout-status).

## Maintained Cardputer reference

[Bruce 1.16.1](https://github.com/BruceDevices/firmware/releases/tag/1.16.1)
was published on 2026-08-11. Its release supports Cardputer and Cardputer-Adv.
The reviewed files are pinned to that release, not a moving default branch.

* [Dependencies](https://github.com/BruceDevices/firmware/blob/1.16.1/platformio.ini)
  select NimBLE-Arduino 2.5.
* [BleKeyboard](https://github.com/BruceDevices/firmware/blob/1.16.1/lib/Bad_Usb_Lib/BleKeyboard.cpp)
  constructs a `NimBLEHIDDevice`, configures keyboard and consumer reports,
  starts its services, and starts advertising on entry. Its disconnect callback
  only clears the connected flag; it does not restart advertising.
* [NimBLEServer 2.5.0](https://github.com/h2zero/NimBLE-Arduino/blob/2.5.0/src/NimBLEServer.cpp)
  initializes `m_advertiseOnDisconnect` to false. Its disconnect handler restarts
  advertising only if that option was enabled. The reviewed Bruce keyboard
  initialization does not enable it. Failed CONNECT is a distinct path that can
  restart advertising.
* [The keyboard application](https://github.com/BruceDevices/firmware/blob/1.16.1/src/modules/badusb_ble/ducky_typer.cpp)
  starts BLE when entering the keyboard function and cleans up on exit. Its
  keyboard loop does not implement our continuous reconnect scheduler.
* NimBLE handles subscription and encryption events using the live connection
  descriptor. Subscribing to a protected characteristic can initiate security.
  Bruce uses this library behavior instead of our custom event queue and GATT
  registration. Its HID Information flags are `0x01`, already tested unsuccessfully
  in our adapter; copying that field is not a demonstrated fix.

The source therefore establishes a concrete policy difference: Bruce's reviewed
keyboard path does not automatically resume connectable advertising after an
ordinary disconnect, whereas our Service does. It does **not** establish that
Bruce supports reconnecting solely with macOS Connect after macOS Disconnect.
Stopping connectable advertising also prevents a host connection until it resumes.
At this checkpoint the user required host-only reconnection; changing to a
Cardputer-side policy therefore required a later explicit product decision.

Bruce is a reference, not a drop-in implementation: its keyboard startup uses
function-specific device identities and its application owns stack teardown.
Those choices must not be copied into our dynamic HostProfile model. Replacing
our backend with NimBLE-Arduino would require an explicit architecture and build
dependency change; it cannot be presented as a verified quick fix.

## Local regression boundary

* Before branch 017, `v0.10.0` (`6547070`) and the then-current committed `600905a` contain
  the identical ESP32 BLE adapter blob
  `7a3d0b2e2ef222c8d8f4d886f92baddc4aaf7478`.
* All 90 report-descriptor bytes match the earlier BLE-only `v0.9.0` (`266389d`)
  after resolving the extracted report-ID constants.
* `v0.9.0` already schedules advertising again after a current-peer disconnect.
  Branch 017 did not introduce this unconditional reconnect policy.
* Branch 017 introduced USB-first routing in `13c9fa7`, then removed it in
  `ef9d0f4`. That rollback changed USB ownership from TinyUSB composite HID/CDC
  to fixed Serial/JTAG, plus pairing-interruption recovery. USB regression and
  intentional BLE disconnect must be assessed separately.
* The security-restoration and early-event-order fixes under review were
  additional to that committed checkpoint. Their tests and physical HID-readiness
  evidence are recorded in plan 017; they do not prove manual-disconnect behavior.

This comparison does not contradict the user's earlier working-device experience.
It narrows the source changes; it is not a physical A/B test of the exact old
image, configuration, and saved host state.

## Disposition

The disposable ESP-IDF reference never completed pairing and cannot support a
conclusion about reconnect behavior. Its subsequent convenience revision was
built but **not flashed** after the user redirected the work to source review.
Do not continue asking for pairing attempts with that example. Return to the
previously compiled product validation image, preserving saved bonds. Keep the
physical Disconnect/Connect issue open and avoid another flags/delay experiment.

The product validation image was restored to app0 with a verified flash hash.
At 12:54:42 serial confirmed Cardputer-Adv detection, the plan-015 harness, and
Bluetooth Disabled. NVS and configuration partitions were not written. The
temporary `HID Reference`/`nimble` image is no longer running; no user-side
Bluetooth UI actions or further HID input were performed during this review.


The user subsequently accepted Cardputer-side host selection and BLE On/Off.
Plan 017 now implements HostService and local settings under that policy; it
supersedes Mac-only reconnect as a requirement without claiming those earlier
experiments passed. A future companion command should reach HostService through
an authenticated control channel. Apple's [cancelPeripheralConnection contract](https://developer.apple.com/documentation/corebluetooth/cbcentralmanager/cancelperipheralconnection%28_%3A%29)
only cancels the application's local connection and explicitly does not guarantee
physical disconnection when another client remains connected. Therefore a CLI
wrapper around that call alone is not a solution to system HID reconnection.
