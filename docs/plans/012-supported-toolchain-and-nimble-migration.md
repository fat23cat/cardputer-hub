# Supported Toolchain and ESP-NimBLE Migration Plan

Status: **Implemented — physical validation pending**

This plan describes the third granular Phase 2 change. It moves the production
firmware to a maintained Espressif toolchain and replaces the Bluetooth
adapter's Bluedroid internals with ESP-NimBLE before pairing and HID behavior
are added.

Implementation should use:

```text
branch:   feat/012-supported-toolchain-and-nimble
PR title: [012] Upgrade the production toolchain and adopt ESP-NimBLE
```

The implementation branch must begin from an updated `main` after plan 011 is
merged.

## 1. Summary

Migrate only dependencies that materially improve supportability and Bluetooth
stability:

* pin the production firmware to ESP-IDF 5.5.5 and use its native runtime;
* replace the Bluedroid adapter internals with direct ESP-NimBLE;
* preserve `BluetoothService`, `IBluetoothAdapter`, the application-layer
  architecture, and the existing Cardputer hardware integrations; use a native
  `app_main` and native ESP-IDF display, keyboard, storage, and serial support;
* leave unrelated libraries at their existing exact revisions unless a
  compatibility failure requires the smallest stable update;
* retain PlatformIO only for the existing native test runner.

ESP-IDF 6, PIOArduino, NimBLE-Arduino, an alternative Cardputer BSP, and code
copied from another firmware are outside this migration. They add platform or
driver churn without improving the Bluetooth contract delivered by this phase.

## 2. Toolchain and Build Changes

Add a native ESP-IDF CMake production build targeting `esp32s3`:

* require ESP-IDF 5.5.5 in the component manifest and committed dependency
  lock, without Arduino in the production component graph;
* use a native `app_main`, initialize M5Unified directly, and retain the
  required 1000 Hz FreeRTOS tick configuration;
* preserve the current USB CDC behavior, eight-megabyte flash layout, partition
  offsets, build metadata, C++17 mode, and warning-as-error policy;
* keep M5Unified 0.2.21, M5GFX 0.2.28, and the existing immutable hardware
  support sources unless physical validation requires a focused change;
* use exact managed-component versions where upstream publishes ESP-IDF
  metadata and explicitly compile only required native sources from immutable
  revisions;
* record any unavoidable compatibility update and its reason in the completion
  record rather than broadly refreshing dependencies;
* preserve the public `make build`, `upload`, `monitor`, `clean`, and `check`
  commands while changing production build, flash, and monitor operations to
  `idf.py`;
* keep the PlatformIO native environment and existing host tests, but remove
  the PlatformIO production firmware environment;
* update CI, tagged rebuilds, caching, firmware artifact paths, checksums, and
  release packaging for the ESP-IDF output layout;
* make developer setup validate an activated ESP-IDF 5.5.5 installation and
  initialize every pinned dependency without undocumented manual library
  installation.

Establish a green ESP-IDF 5.5.5 build with the existing Bluetooth behavior
before changing Bluetooth stacks. This checkpoint must include the necessary
ESP-IDF 4.4-to-5.5 API corrections so build-system failures remain
distinguishable from the ESP-NimBLE port.

## 3. ESP-NimBLE Adapter Migration

Keep the hardware-neutral Bluetooth contract unchanged and replace only the
ESP32 adapter internals:

* use the ESP-NimBLE APIs bundled with ESP-IDF, not NimBLE-Arduino or another
  BLE facade;
* enable peripheral, broadcaster, and GATT-server behavior with one maximum
  connection; disable Bluedroid, Classic Bluetooth, central, observer, and
  scanning roles;
* retain ESP32 software coexistence for simultaneous Wi-Fi station and BLE
  operation;
* keep Arduino and its Bluetooth facades out of the production component graph
  so controller memory remains available for direct ESP-NimBLE use;
* translate NimBLE host, GAP, advertising, and connection callbacks into the
  existing bounded owned-event model;
* preserve lifecycle-generation isolation, asynchronous operation correlation,
  retryable-versus-fatal classification, adapter-known peer tracking, and
  fail-closed cleanup;
* stop advertising, terminate every known peer, drain or invalidate pending
  callbacks, and stop the NimBLE host before reporting successful shutdown;
* use only documented repeatable NimBLE host lifecycle operations and do not
  irreversibly deinitialize the controller during logical disable;
* preserve explicit-enable recovery: no callback, polling path, or teardown
  failure may reactivate advertising after the Service enters `Disabled` or
  `Error`;
* audit NimBLE, controller, GAP, ATT, SMP, and storage logging so peer
  identities, keys, bond references, passkeys, and future HID payloads cannot
  enter diagnostics.

This plan does not add pairing, bond management, HID services, reports, or
user-facing connectivity controls.

## 4. Granular Implementation and Verification Sequence

1. Record a green plan-011 baseline, including native test results and firmware
   flash and RAM usage.
2. Add the ESP-IDF project, exact component manifest and lock, `sdkconfig`
   defaults, native hardware integrations, and build metadata integration.
3. Make the existing application compile under native ESP-IDF 5.5.5 without
   changing observable behavior.
4. Update `make` targets and CI, rebuild, and smoke-test display, keyboard,
   microSD, serial logging, and IR before changing the Bluetooth stack.
5. Add or retain failing behavioral regressions for every observable Bluetooth
   lifecycle difference found during the port. Framework callback glue remains
   a documented thin-adapter TDD exception.
6. Switch configuration from Bluedroid to ESP-NimBLE and implement the minimum
   adapter behavior needed to restore all lifecycle tests.
7. Verify lifecycle generation, advertising completion, peer rejection,
   disable cleanup, retry classification, and error recovery against the
   existing Service contract.
8. Audit identity-bearing framework logs and verify the final image contains no
   enabled Bluedroid host.
9. Update architecture, engineering workflow, README setup/build/release
   instructions, and affected plan references. Audit the device manual without
   documenting a new user-facing feature.
10. Complete this plan with dependency decisions, RED/GREEN evidence, exact
    automated checks, size results, and physical validation findings.

Run and record all applicable repository checks. At minimum these include:

```text
make format
make format-check
make lint
make test
make build
make check
```

Physical Cardputer-Adv validation is mandatory for this infrastructure change:

* verify display, keyboard, microSD, serial logging, and IR behavior;
* complete at least 100 Bluetooth enable, disable, and re-enable cycles;
* repeatedly advertise, reject an unbonded peer, connect, and disconnect;
* run Wi-Fi station traffic alongside BLE advertising or a BLE connection for
  at least one hour;
* record minimum free heap across the lifecycle run and verify there is no
  monotonic loss;
* confirm there are no watchdog resets, resource exhaustion, stale lifecycle
  callbacks, unexpected advertising restarts, or identity-bearing logs.

## 5. Acceptance Criteria and Assumptions

The change is complete only when:

* production firmware builds reproducibly with native ESP-IDF 5.5.5 from exact
  recorded dependencies and without Arduino in the production graph;
* the normal developer, CI, tagged-rebuild, upload, and release workflows use
  the ESP-IDF production build successfully;
* all existing hardware-neutral Service and adapter interfaces remain source
  compatible;
* ESP-NimBLE is the only enabled Bluetooth host and is configured for the
  required single-peer peripheral roles;
* current Bluetooth lifecycle behavior and identity-free logging guarantees are
  preserved;
* Wi-Fi and Bluetooth operate concurrently on physical Cardputer-Adv hardware;
* the application fits the existing partition without changing storage layout;
* existing user-facing hardware behavior is not regressed;
* every applicable repository check and mandatory physical validation passes.

Assumptions and defaults:

* plan 011 is merged first;
* official support and practical stability take priority over the highest
  numerical framework version;
* PlatformIO host-test removal and a pure ESP-IDF Cardputer driver port are
  deferred;
* the UI requirements change uses number 013; plans 014 and 015 build pairing
  and HID on the ESP-NimBLE adapter established here;
* no implementation branch is pushed without explicit permission.

## 6. Completion Record

Implementation was completed on `feat/012-supported-toolchain-and-nimble` from
plan-011 baseline commit `88688d8`. The plan remains open because the mandatory
Cardputer-Adv validation below requires physical hardware.

### Dependency and migration decisions

* The production project is pinned to ESP-IDF 5.5.5 at upstream commit
  `b774170ff46c393eeb5e495ea37936038d3f4f4f`. M5Unified 0.2.21 and M5GFX
  0.2.28 are exact managed-component requirements, with the complete resolved
  graph committed in `dependencies.lock`. Arduino is not in that graph.
* Physical Cardputer-Adv validation found that Arduino Core 3.3.11 crashes
  deterministically inside `esp_bt_controller_init()` on the first Bluetooth
  enable. A controlled build that changed only Arduino Core to 3.3.6 while
  retaining ESP-IDF 5.5.5 enabled advertising and completed a
  disable/re-enable cycle. The compatible 3.3.6 pin and its resolved
  transitive component graph isolated the original Bluetooth failure. Later
  display validation showed that retaining Arduino still destabilized board
  detection, so the final production graph removes Arduino entirely.
* M5Cardputer remains at immutable commit
  `2d4fa6646e4e5b47e0af96214b003aa7b15b8d81`, and IRremote remains at 4.7.1
  commit `498dc591b255d8ba2e239c875804bdab2ab0fe91`. Production compiles only the
  native TCA8418 keyboard driver source it requires; the Arduino component
  wrappers are not dependencies of `main`.
* ESP-IDF configuration uses a native `app_main`, initializes M5Unified
  directly, drives the TCA8418 keyboard through M5Unified I2C, mounts microSD
  through ESP-IDF SDSPI/FatFs, and uses the USB Serial/JTAG console. This was
  required after physical validation showed that Arduino participation left M5
  board detection at `board_unknown`, causing the first display operation to
  crash. The native graph preserves the 8 MB flash and existing partition
  offsets, the 1000 Hz tick, 240 MHz CPU, project C++17 compilation, and
  warnings as errors. Direct ESP-NimBLE is the only Bluetooth host.
* The adapter now owns the ESP-NimBLE host task directly. GAP callbacks copy
  connection identity into the existing bounded event queue, preserve lifecycle
  generations and retry classification, and separately track callback-known
  connections so shutdown closes a peer even before Service polling consumes
  its connect event. Logical shutdown stops advertising and peers, stops and
  deinitializes the host, and disables—but does not irreversibly deinitialize—the
  controller.
* Production build, flash, monitor, CI, release, and tagged-rebuild paths use
  native ESP-IDF output. Historical tags without the new component manifest
  retain their original PlatformIO rebuild path; current PlatformIO use is
  limited to native tests and static analysis.
* PR CI runs host validation and firmware compilation in parallel. The exact
  ESP-IDF checkout, managed components, and a bounded compiler cache are stored
  separately so source-only changes do not reinstall the toolchain. Release
  builds reuse those caches and rebuild the versioned image without repeating
  host checks already passed by the triggering CI run.

### TDD and automated verification

The build-configuration regressions were added first and failed for the missing
ESP-IDF manifest, production-build routing, NimBLE configuration, and adapter
migration. They passed after the minimum production configuration and adapter
changes. Framework callback glue remains the thin-adapter exception described
by this plan; its observable lifecycle behavior continues to be covered through
the unchanged hardware-neutral Service contract.

The following local checks pass with the exact pinned toolchain:

```text
make format
make format-check
make lint
make test        # Python and native C++ test suites
make build       # ESP-IDF 5.5.5, project sources compiled with -Werror
make check       # lock, format, lint, tests, and production build
```

The three GitHub Actions workflows also parse successfully as YAML, the
installer passes `bash -n`, both submodule revisions match their recorded
commits, and `git diff --check` reports no whitespace errors.

### Size results

The plan-011 PlatformIO baseline application image was 497,984 bytes, with the
3,072-byte partition table. Its legacy ELF section report was 360,565 bytes of
text, 137,312 bytes of data, and 382,841 bytes of BSS; those section totals are
recorded for traceability but are not directly comparable with ESP-IDF 5.5's
memory-region report.

The native production ESP-IDF application image is 447,456 bytes. It occupies
about 13% of the unchanged 0x330000-byte OTA partition, leaving 87% free.

### Required physical validation

Physical validation progress on 2026-09-05:

* the validation harness booted and the Wi-Fi connect/status/disconnect check
  passed;
* two native `app_main` display controls rendered successfully: first with the
  M5Stack UserDemo component revisions and then with the production M5Unified
  0.2.21 and M5GFX 0.2.28 components. Images retaining Arduino instead reported
  an unknown board or produced a black screen, so the production component
  graph was changed to match the validated native runtime;
* the resulting native production image was flashed successfully and rendered
  the expected `Cardputer Hub` and `0.1.0-dev` boot screen on the physical
  Cardputer-Adv without a display crash;
* Arduino Core 3.3.11 failed deterministically during the first controller
  initialization, while the controlled 3.3.6 build advertised successfully and
  completed a disable/re-enable check;
* the BLE lifecycle warm-up completed 1/1 and the measured run completed
  100/100 with identical 234,748-byte initial and final free-heap samples and
  no reset or watchdog;
* an external BLE scanner found the advertisement, repeated unbonded connection
  attempts were rejected without peer identity in the application log,
  advertising recovered each time, and the advertisement disappeared after
  disable and reappeared after re-enable;
* the controller's identity-bearing `BLE_INIT` output was observed, suppressed
  before controller initialization, and absent on the next physical enable;
* identity-bearing framework Wi-Fi output was observed, the `wifi` and
  `esp_netif_handlers` tags were suppressed before station initialization, and
  a subsequent physical connection completed without exposing the network or
  interface identities in the serial log;
* the two-minute concurrent Wi-Fi association and BLE advertising smoke test
  completed with zero Wi-Fi interruptions, zero Bluetooth interruptions, no
  reset or watchdog, and the advertisement remained visible to an external
  scanner; an additional five-minute run also completed with zero interruptions
  and an unchanged 151,328-byte free-heap sample at every reported minute.

Still pending: keyboard, microSD, serial, and IR production smoke tests; the
60-minute concurrent Wi-Fi/BLE watch; the separate one-hour traffic-generating
criterion; and a final full-log privacy audit. These results must be recorded
before this plan can be marked complete.
