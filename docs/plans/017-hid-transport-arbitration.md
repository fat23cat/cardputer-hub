# BLE-only Phase 2 Closeout

Status: **Implemented; automated checks passed; final device verification pending**

The user superseded both automatic USB-first routing and the proposed saved
manual-channel setting on 2026-09-11. BLE is the only host-control transport.
The plan number and filename remain stable for branch/history references.
Neither a router nor an Active channel setting is part of the current scope.

```text
branch:   feat/017-hid-transport-arbitration
PR title: [017] Keep BLE host control and remove USB HID
```

PR #21 retains its existing branch. Its `feat/` prefix intentionally selects a
minor release under the engineering versioning rules: if the highest permanent
tag at release time is `v0.10.0`, the next release is `v0.11.0`.

## 1. Final Phase 2 Scope

Retain the implemented foundations from plans 010-015:

* hardware-independent WiFiService and its checked ESP-IDF station adapter;
* BluetoothService lifecycle, authenticated pairing, bounded retries, and
  opaque persistent bond references;
* selection of a BLE bond without hardcoded personal devices;
* BLE keyboard and consumer-control HID readiness, report delivery, release,
  failure isolation, and the opt-in plan-015 hardware harness;
* shared report validation and `IHidTransport` as the extension boundary.

Remove the software USB HID stack, descriptors, USB lifecycle facade/adapter,
TinyUSB managed dependencies, USB validation harness/tests, routing machinery,
manual/automatic channel selection, and its proposed configuration/UI schema.
Restore the fixed USB Serial/JTAG console for diagnostics and firmware upload.
A cable is never an application-level host-control channel.

No new application Service is required to finish this reduced Phase 2 scope.
Phase 3 introduces HostService and ConfigurationService; later phases supply
Settings, Device Manager, and Action-to-HID mapping. Normal firmware retains
the boot screen, keyboard polling, and diagnostics; it does not yet enable
Bluetooth or send keyboard input to a host. Do not mark later product features
supported merely because their Connectivity foundation compiles.

## 2. Extensibility Without Dormant Implementations

Services depend on `IHidTransport`, not on NimBLE or ESP32. BLE supplies the
current implementation through `BluetoothService.hidTransport()`. Retain the
shared report descriptor and report IDs, since BLE uses the same HID report
format; a HID usage or report ID is not a USB transport implementation.

A future transport belongs under Connectivity with hardware behind adapters.
Its own plan must define lifecycle, target identity, delivery/release semantics,
physical validation, and any required selection/persistence/UI changes. Do not
pre-create channel enums, routers, settings, or NVS records for that future.
Wi-Fi infrastructure remains available for future integrations independently
of the single BLE host-control transport.

## 3. Remaining BLE Correction

A disconnected peer during an open pairing attempt previously moved pairing
to terminal Error. The regression test now covers resuming advertising,
clearing the old challenge, refusing its stale response, leaving stored bonds
untouched, and closing at the original 120-second deadline. A cancellation or
timeout must not reopen a pairing window.

This is a Service state-machine correction. It does not replace the BLE GATT
profile, change I/O/security policy, erase pairing data, or introduce implicit
pairing. The previously tested ESP-NimBLE adapter is retained.

## 4. Build and Upgrade Contract

`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` selects diagnostics. The production
manifest/lock and component graph contain no TinyUSB dependency. A build using
an old generated console selection must fail with a migration instruction,
not silently produce a firmware image without diagnostics. Regenerate local
`sdkconfig` from current defaults; preserve device NVS and `hub_config`.

Retain the existing installation layout, paired application/partition images,
and non-destructive upload procedure. Upgrading the removed USB experiments
may require the G0 download-mode sequence and rediscovering the serial port.
See [installation](../manuals/installing-firmware.md).

## 5. Verification Record

RED evidence in this implementation session:

* new BLE-only build-contract tests failed against the composite-USB graph;
* interrupted-pairing test failed with expected Advertising versus actual Error;
* stale-console configuration test failed before the CMake guard existed.

GREEN verification on 2026-09-11:

* `make host-check UV=/private/tmp/uv-0.12.7/uv`: dependency lock validation,
  clang-format, Cppcheck, 44 Python tests, and 163 native C++ tests across
  14 suites passed. Cppcheck retains one low-priority callback-const suggestion
  in the unchanged ESP32 BLE adapter.
* `make firmware-check`: the standard `build/` production image compiled with
  ESP-IDF v5.5.5 for ESP32-S3; application size is 436432 bytes.
* A separate clean production build in `build-ble-only/` also passed using a
  fresh `sdkconfig` generated from the current defaults.
* The plan-015 BLE validation image compiled in `build-validation-ble-only/`
  with `CARDPUTER_HUB_PLAN_015_VALIDATION=ON`; application size is 1262656 bytes.
* Reconfiguring with the previous composite-USB `sdkconfig` failed at the new
  console guard with the expected migration message. The old local config was
  backed up and regenerated before the standard production build.
* `git diff --check`, relative file links, and fenced blocks in changed Markdown
  documents were checked successfully. No standalone Markdown linter is
  configured in the repository.

The shared plan-014/015 console uses a zero-timeout serial read. The removed
USB suites are not retained as tests of an unsupported feature. These results
cover automated verification, not cable behavior or Bluetooth radio behavior
on the physical device. No firmware was flashed in this implementation session.

Previous plan-014/015 physical results remain historical evidence; they are not
presented as a rerun of this image. The user explicitly deferred plugging in
and testing the device until implementation is finished.

Final physical acceptance uses
[the BLE harness runbook](../validation/plan-015-device-harness.md):

1. Install the BLE-only validation image without erasing bonds or configuration.
2. Verify serial diagnostics, authenticated pairing, and selected-host HID
   readiness. Recover stale host-side pairing state only when necessary,
   against the intended test pair.
3. Test keyboard/modifier/six-key and consumer reports, neutral release,
   disconnect/reconnect, and repeated disable/re-enable.
4. Cycle USB power/data while the board remains battery-powered. Confirm BLE
   remains usable, no USB keyboard enumerates, and serial returns after replug.
5. Confirm pairing interruption recovery and the unchanged timeout.
6. Repeat selected-host and Wi-Fi coexistence checks and inspect non-identifying
   diagnostics for resets, stale output, resource loss, or secret disclosure.
7. Carry forward any equipment-limited security/capacity cases from plan 014
   explicitly; do not convert an unexecuted case into a pass.

Until this rerun is recorded, Phase 2 is complete in software scope but not
fully verified on the final firmware. No USB handover or channel-persistence
work remains. Documentation reflects that boundary; manuals contain no new
Settings or BLE product controls.
