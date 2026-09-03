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

* pin the production firmware to ESP-IDF 5.5.5 with Arduino Core 3.3.11 as an
  official ESP-IDF component;
* replace the Bluedroid adapter internals with direct ESP-NimBLE;
* preserve `BluetoothService`, `IBluetoothAdapter`, the application-layer
  architecture, Arduino `setup` and `loop`, and the existing Cardputer hardware
  integrations;
* leave unrelated libraries at their existing exact revisions unless a
  compatibility failure requires the smallest stable update;
* retain PlatformIO only for the existing native test runner.

ESP-IDF 6, PIOArduino, NimBLE-Arduino, an alternative Cardputer BSP, and code
copied from another firmware are outside this migration. They add platform or
driver churn without improving the Bluetooth contract delivered by this phase.

## 2. Toolchain and Build Changes

Add a native ESP-IDF CMake production build targeting `esp32s3`:

* require ESP-IDF 5.5.5 and pin `espressif/arduino-esp32` 3.3.11 exactly in the
  component manifest and committed dependency lock;
* keep Arduino autostart so the current `setup` and `loop` entry points remain
  valid, and retain the required 1000 Hz FreeRTOS tick configuration;
* preserve the current USB CDC behavior, eight-megabyte flash layout, partition
  offsets, build metadata, C++17 mode, and warning-as-error policy;
* keep M5Unified 0.2.21, M5GFX 0.2.28, the existing immutable M5Cardputer
  commit, and IRremote 4.7.1 unless one fails against Arduino 3.3.11;
* use exact managed-component versions where upstream publishes ESP-IDF
  metadata and thin local CMake component wrappers around the existing
  immutable revisions for Arduino-only libraries;
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
* ensure Arduino startup retains the controller memory required by direct
  ESP-NimBLE use;
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
   defaults, Arduino-library wrappers, and build metadata integration.
3. Make the existing application compile under ESP-IDF 5.5.5 and Arduino
   3.3.11 without changing observable behavior.
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

* production firmware builds reproducibly with ESP-IDF 5.5.5 and Arduino Core
  3.3.11 from exact recorded dependencies;
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
* plans 013 and 014 build pairing and HID on the ESP-NimBLE adapter established
  here;
* no implementation branch is pushed without explicit permission.

## 6. Completion Record

Implementation was completed on `feat/012-supported-toolchain-and-nimble` from
plan-011 baseline commit `88688d8`. The plan remains open because the mandatory
Cardputer-Adv validation below requires physical hardware.

### Dependency and migration decisions

* The production project is pinned to ESP-IDF 5.5.5 at upstream commit
  `b774170ff46c393eeb5e495ea37936038d3f4f4f`. Arduino Core 3.3.11,
  M5Unified 0.2.21, and M5GFX 0.2.28 are exact managed-component requirements,
  with the complete resolved graph committed in `dependencies.lock`.
* M5Cardputer remains at immutable commit
  `2d4fa6646e4e5b47e0af96214b003aa7b15b8d81`, and IRremote remains at 4.7.1
  commit `498dc591b255d8ba2e239c875804bdab2ab0fe91`. They are Git submodules behind
  thin local ESP-IDF component wrappers. No compatibility-driven dependency
  update was required.
* ESP-IDF configuration preserves Arduino `setup` and `loop`, USB CDC startup,
  the 8 MB flash and existing partition offsets, the 1000 Hz tick, 240 MHz CPU,
  project C++17 compilation, and warnings as errors. Arduino selective
  compilation excludes its BLE and BluetoothSerial libraries so direct
  ESP-NimBLE is the only host.
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
make test        # 24 Python tests and 134 native C++ tests
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

The final ESP-IDF application image is 526,848 bytes, an increase of 28,864
bytes (5.8%). It occupies 16% of the unchanged 0x330000-byte OTA partition,
leaving 84% free, and the partition-table binary remains 3,072 bytes. `idf.py
size` reports 90,783 of 341,760 DIRAM bytes used (26.56%, including 8,304 bytes
of BSS), 16,384 IRAM bytes, 325,558 bytes of flash code, and 102,280 bytes of
flash data.

### Required physical validation

Not yet run: display, keyboard, microSD, serial, and IR smoke tests; 100
Bluetooth enable/disable/re-enable cycles; repeated advertise/reject/connect/
disconnect behavior; the one-hour concurrent Wi-Fi/BLE run; minimum-free-heap
tracking; and checks for resets, exhaustion, stale callbacks, advertising
restarts, and identity-bearing logs. These results must be recorded before this
plan can be marked complete.
