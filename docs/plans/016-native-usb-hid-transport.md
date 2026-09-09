# Native USB HID Transport Plan

Status: **Implemented — physical validation complete with documented coverage gaps**

This plan describes the sixth granular Phase 2 change. It replaces the fixed
runtime USB Serial/JTAG console with one native TinyUSB composite CDC and HID
device, preserving serial diagnostics while adding the USB implementation of
the shared HID transport contract.

Implementation should use:

```text
branch:   feat/016-native-usb-hid
PR title: [016] Add native USB composite serial and HID
```

The implementation branch must begin from updated `main` after plan 015 is
merged.

## 1. Summary

Add:

* an exact, locked Espressif TinyUSB managed component;
* a composite native USB device with CDC-ACM and HID interfaces;
* runtime serial logging and monitoring over the composite CDC interface;
* native USB keyboard and consumer-control reports through `IHidTransport`;
* mounted, suspended, endpoint-ready, busy, and unmounted states;
* native tests, build-configuration regressions, manual updates, and physical
  Cardputer-Adv validation.

The ESP32-S3 USB Serial/JTAG controller and USB-OTG controller share the
internal PHY. The shipped runtime must therefore use one composite USB-OTG
personality rather than attempting to run the existing fixed console and
TinyUSB HID simultaneously. ROM download mode remains responsible for
flashing.

## 2. Dependency, Configuration, and Descriptor Decisions

Update the native ESP-IDF production build to:

* add exact dependency `espressif/esp_tinyusb == 2.2.1` to the component
  manifest and commit its complete resolved dependency graph;
* disable the fixed USB Serial/JTAG runtime console;
* enable the TinyUSB device stack, CDC-ACM, and HID with framework logging at a
  privacy-safe level;
* keep ESP-IDF 5.5.5, the existing flash layout, C++17 mode, warnings-as-errors,
  and all unrelated dependency pins unchanged;
* retain ROM USB download mode and the existing application/partition release
  artifacts.

Use one full-speed composite device with:

* one CDC-ACM Interface Association for serial diagnostics;
* one HID interface containing keyboard and consumer-control reports;
* keyboard input report ID 1 and consumer-control input report ID 2;
* product string `Cardputer Hub`;
* concise interface strings that contain no host, user, or device identity;
* the Espressif TinyUSB component's permitted default VID/PID;
* no chip-derived, MAC-derived, or other unique serial-number string;
* no mass-storage, MIDI, vendor-specific, DFU-runtime, mouse, or gamepad
  interface.

The descriptor bytes are project-owned constants and receive native structural
tests for interface count, endpoint uniqueness, report IDs, total length, and
absence of a unique serial descriptor.

## 3. Public USB Transport Behavior

Implement the plan-015 `IHidTransport` contract behind a hardware-neutral USB
state wrapper and an ESP32/TinyUSB adapter.

Required behavior:

* construction has no USB side effects;
* explicit initialization installs the composite device once and returns a
  checked result;
* repeated initialization is idempotent for the same owned lifecycle;
* `Unavailable` covers uninitialized, cable-powered but unmounted, unmounted,
  and suspended states;
* `Ready` requires TinyUSB mounted state, no suspension, and an available HID
  endpoint; VBUS or cable attachment alone is insufficient;
* endpoint backpressure returns `Busy` without sleeping or dropping the owned
  report;
* adapter failure enters `Error` and remains isolated from Bluetooth, Wi-Fi,
  input polling, and display updates;
* keyboard and consumer reports use the same semantic values as BLE even when
  their wire framing differs;
* `releaseAll` emits neutral keyboard and consumer reports when the mounted
  endpoint remains usable;
* confirmed unmount is treated as host-side neutralization because the USB HID
  device has been removed;
* suspend is not treated as disconnection and must not silently discard an
  active release requirement;
* resume returns to `Ready` only after the HID endpoint reports readiness;
* callbacks copy bounded lifecycle state for polling and never mutate a future
  router directly;
* no callback or log includes report data, key usages, host information, or
  USB control-transfer contents;
* this plan sends reports only from tests and an unshipped validation harness;
  normal physical keyboard input is not routed to USB.

## 4. Composite CDC and Runtime Integration

Preserve the current diagnostics contract while moving its transport:

* install the composite TinyUSB device before `SystemRuntime::start` writes
  normal boot metadata;
* route the existing serial log sink and standard ESP-IDF console output to the
  TinyUSB CDC interface;
* keep logging calls non-blocking when no CDC host is attached;
* prevent a disconnected or slow CDC consumer from delaying the firmware loop
  or HID reports;
* retain 115200 as the documented monitor convention even though USB CDC does
  not depend on a physical baud clock;
* handle application-to-ROM re-enumeration predictably during reset and
  download mode;
* preserve `make upload` where automatic reset works and document the existing
  `G0` plus reset recovery when it does not;
* make `make monitor` work with the application CDC port and document that its
  device path may differ from the ROM download port;
* update release/install instructions if one explicit port cannot identify
  both application and bootloader enumeration on a supported host.

USB initialization failure must not prevent the display, keyboard, or other
local boot behavior. It may make CDC diagnostics unavailable and set USB
transport `Error`, but it must not reboot-loop or block forever.

## 5. Granular TDD Implementation Sequence

1. Add failing descriptor tests for the CDC/HID interface set, report IDs,
   endpoint uniqueness, total lengths, product strings, and missing serial
   number.
2. Add Python build-configuration tests for the exact TinyUSB dependency,
   committed lock, disabled fixed console, and enabled CDC/HID configuration.
3. Add side-effect-free construction and checked/idempotent initialization
   tests with a fake native USB adapter.
4. Add powered-only, mounted, suspended, resumed, unmounted, and endpoint-busy
   state tests; implement the hardware-neutral state wrapper.
5. Add keyboard, consumer-control, owned-report, and semantic-equivalence tests
   shared with the BLE transport.
6. Add release-all, partial release, suspend, definitive-unmount, and
   adapter-error tests.
7. Add tests proving CDC backpressure and absent monitoring do not delay HID or
   the System Core update loop.
8. Add callback generation, queue overflow, stale event, and repeated
   initialization tests.
9. Refactor shared HID validation while keeping both BLE and USB suites green.
10. Add the exact managed dependency, configuration, composite descriptors,
    TinyUSB callbacks, CDC console integration, and thin HID adapter.
11. Add firmware-build assertions preventing simultaneous fixed USB
    Serial/JTAG and internal-PHY TinyUSB ownership.
12. Audit TinyUSB, USB PHY, CDC, HID, and console logging for identity or
    report disclosure.
13. Update the connectivity, hardware-abstraction, logging, and Phase 2
    architecture sections plus the installation and device manuals.
14. Complete this plan with dependency hashes, RED/GREEN evidence, check
    results, firmware size, USB descriptors, and physical findings.

## 6. Test and Verification Plan

Run and record:

```text
uv run --frozen pio test -e native -f test_hid_transport
uv run --frozen pio test -e native -f test_native_usb_hid
uv run --frozen python -m unittest test_python.test_esp_idf_build_config
make format
make format-check
make lint
make test
make build
make check
```

Mandatory Cardputer-Adv validation must:

* inspect the enumerated interfaces and descriptors on a physical host;
* confirm the application exposes CDC and HID simultaneously through the
  Cardputer USB-C connection;
* verify boot metadata, ongoing logs, and `make monitor` over CDC;
* verify keyboard usages, modifiers, six-key reports, consumer controls, and
  explicit releases in a host-side viewer;
* confirm cable power without enumeration is not reported as HID-ready;
* exercise suspend, resume, repeated hotplug, application reset, normal upload,
  and `G0` download-mode recovery;
* confirm a slow or disconnected monitor does not stall the display, keyboard,
  Bluetooth, Wi-Fi, or HID;
* repeat the existing display, keyboard, microSD, serial, and IR production
  smoke checks after the console change;
* audit descriptors and logs for unintended unique device identity.

Where available, repeat enumeration and basic keyboard/consumer validation on
macOS and one additional host family. Record unavailable host coverage rather
than claiming it passed.

## 7. Acceptance Criteria and Assumptions

The change is complete only when:

* the Cardputer enumerates one stable composite CDC/HID application device;
* serial diagnostics remain available without competing for the internal PHY;
* USB readiness reflects enumeration and endpoint state rather than cable
  power;
* native USB implements the same hardware-neutral HID semantics as BLE;
* flashing and monitor recovery procedures are accurate and physically tested;
* USB failures do not break local firmware behavior or other Connectivity;
* every automated and mandatory physical check passes.

Assumptions and defaults:

* plan 015 merges first;
* `espressif/esp_tinyusb` 2.2.1 is the selected exact component; any proven
  incompatibility with ESP-IDF 5.5.5 blocks this plan for an explicit revision
  rather than silently selecting another version;
* the Espressif default VID/PID is acceptable until project-owned identifiers
  exist;
* the application exposes no unique USB serial number;
* native USB remains physically present at runtime, while Action-to-HID routing
  remains later work;
* USB-first arbitration is introduced only by plan 017.

## 8. Implementation Record

Implemented on 2026-09-08 with:

* exact locked `espressif/esp_tinyusb` 2.2.1 and its resolved
  `espressif/tinyusb` 0.21.0~1 dependency, with component hashes
  `9a73a76a6bc17907f6e523342ff6b1e440023e8aac2c11f4691212106ec24e66`
  and
  `a72b7d67472914ab76309340fd50d578b31e310963d45ad0f81144bde3314752`;
* one project-described full-speed composite device using Espressif VID
  `0x303A` and default composite PID `0x4005`, CDC control/data plus one HID
  interface, report IDs 1 and 2, and no serial-number descriptor;
* a hardware-neutral `NativeUsbHidService`, bounded callback queue, generation
  filtering, mount/suspend/endpoint readiness, non-blocking backpressure, and
  suspend-safe partial neutral release;
* a thin ESP32 TinyUSB adapter, non-blocking CDC console redirection, runtime
  initialization before boot metadata, and continued local boot after USB
  initialization failure;
* shared BLE/USB HID report descriptors and an opt-in plan-016 physical
  validation harness;
* native descriptor/lifecycle/report/release/error tests, build-configuration
  regressions, and updated architecture, engineering, installation, and device
  documentation.

RED evidence: the focused build-configuration suite failed on the missing
TinyUSB dependency, USB configuration, adapter integration, and validation
harness assertions before those changes were added. GREEN evidence: the
focused build-configuration suite then passed 28 tests, the shared HID suite
passed 3 tests, and the new native USB suite passed 9 tests before the complete
repository check.

The final `make check` run passed lock consistency, formatting, static
analysis, 45 Python tests, 171 native C++ tests across 15 suites, and the
ESP32-S3 production build. The resulting application image was 469,920 bytes
(`0x72ba0`), leaving 86% of the smallest application partition free. The
opt-in plan-016 validation build also passed at 471,360 bytes (`0x73140`) with
86% free.

Physical Cardputer-Adv validation followed
`docs/validation/plan-016-device-harness.md`; results and unavailable coverage
are recorded below.

Physical validation ran from 2026-09-08 through 2026-09-10 using macOS 26.6.2
on arm64:

* the validation image flashed successfully through the pre-existing
  ESP32-S3 USB Serial/JTAG port and rebooted into the application;
* macOS enumerated `Cardputer Hub` at full speed with VID `0x303A`, PID
  `0x4005`, device class `0xEF`, and `iSerialNumber = 0`;
* the application exposed `/dev/cu.usbmodem1101` as CDC while macOS registered
  a USB HID keyboard device with primary Usage Page 1 / Usage 6;
* the CDC validation command `status` returned `USB HID state=ready`, proving
  simultaneous CDC communication and HID endpoint readiness.
* an application reset caused the monitor to observe disconnect and
  re-enumeration, after which all firmware name, version, commit, and build-type
  metadata plus the validation banner arrived over CDC and HID returned to
  `ready`;
* passive macOS raw-report capture confirmed keyboard report ID 1 with Shift
  plus usage `0x04`, all six keyboard usages `0x04` through `0x09`, consumer
  report ID 2 with usage `0x00E9` in little-endian wire order, and zero-filled
  keyboard and consumer release reports;
* back-to-back neutral keyboard and consumer sends exercised actual endpoint
  backpressure: the harness returned `Busy` after accepting the keyboard
  neutral report, and the documented retry accepted only the pending consumer
  neutral report;
* with the CDC port open but its output deliberately unread, 500 status
  responses filled the diagnostic path without blocking the firmware: a
  subsequent Shift-plus-`0x04` HID report and both neutral reports were captured
  successfully, and reopening the monitor returned `USB HID state=ready`;
* one battery-backed disconnect and power-only interval left the local display
  operating and changed the validation indicator from `USB: ready` to
  `USB: unavailable`. After a functioning host data connection was restored,
  the indicator returned to `USB: ready`, macOS registered the same descriptor
  identity and HID keyboard, and CDC returned `USB HID state=ready`. Moving the
  side power switch to the documented charging position (`ON`) left the same
  `0x303A:0x4005` CDC connection active and the HID state `ready`, confirming
  that charging-mode power and USB data can coexist;
* a second host disconnect/reconnect created a fresh macOS USB session with
  the same descriptor identity and HID keyboard. CDC reused
  `/dev/cu.usbmodem101`; restarting the monitor on that path returned
  `USB HID state=ready`;
* the documented `G0` plus Reset sequence entered the ESP32-S3 ROM USB
  Serial/JTAG personality (`0x303A:0x1001`). Flashing the validation image on
  that ROM port succeeded with SHA verification; one ordinary Reset without
  `G0` then returned to `Cardputer Hub` (`0x303A:0x4005`), restored CDC and HID,
  and returned `USB HID state=ready`;
* a macOS sleep/wake cycle retained the same USB session rather than
  unmounting it. CDC remained usable, HID remained registered, `status`
  returned `ready`, and a post-resume Shift-plus-`0x04` report followed by both
  neutral reports was captured successfully;
* the final 469,920-byte production image was restored with the validation
  option disabled. It enumerated again as `Cardputer Hub` (`0x303A:0x4005`),
  displayed `Cardputer Hub` and `0.1.0-dev` without the validation indicator,
  and emitted the firmware name, version, commit, and `cardputer-adv` build type
  through CDC after an application reset. No validation banner was present.

Only a macOS host was available, so enumeration and basic HID validation on an
additional host family are recorded as unavailable rather than passed. The
production runtime visibly exercises the display and CDC and polls the
keyboard, but it exposes no observable keyboard action, does not compose the
microSD adapter, and has no IR implementation; those production smoke items
could not be claimed as physically exercised. The slow-monitor result covers
the System Core loop and USB HID; Bluetooth and Wi-Fi remain inactive
foundations in this validation image and likewise were not claimed as
physically exercised by that check.
