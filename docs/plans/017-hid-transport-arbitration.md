# BLE-only Phase 2 Closeout

Status: **Software complete; core product device checks passed; extended acceptance partial**

The user superseded both automatic USB-first routing and the proposed saved
manual-channel setting on 2026-09-11. BLE is the only host-control transport.
The plan number and filename remain stable for branch/history references.
Neither a router nor an Active channel setting is part of the current scope.

```text
branch:   feat/017-hid-transport-arbitration
PR title: [017] Add persistent BLE hosts and interactive system UI
```

PR #21 retains its existing branch. Its `feat/` prefix intentionally selects a
minor release under the engineering versioning rules: if the highest permanent
tag at release time is `v0.10.0`, the next release is `v0.11.0`.

## 0. Current Closeout Status

Reviewed on **2026-09-11** against code, automated checks, serial records and
operator feedback. This section is the current status; sections 5 onward are a
chronological investigation/implementation log. Older pending items, controls,
image sizes, names and instructions there describe their checkpoints and do not
override this summary or the [device guide](../manuals/device-guide.md).

### Implemented and automatically checked

- [x] BLE-only host control; USB HID/router/channel settings removed.
- [x] Wi-Fi foundation, Bluetooth lifecycle, pairing/bonds and HID transport.
- [x] NimBLE first-sync barrier, reconnect security/subscriptions, early-event
  ordering and pairing interruption recovery.
- [x] HostService and persisted host configuration: pair/add, select, rename,
  per-host delete, selected-host intent and BLE On/Off.
- [x] Home and Settings/Bluetooth UI, Tab entry, per-host menus, pairing prompts,
  Micro 5 host/code glyphs, battery estimate and explicit unavailable telemetry.
- [x] Buffered partial rendering, 220 ms page transitions, 28-second ambient
  wave, and removal of Settings/Bluetooth's Esc Home footer.
- [x] Manuals and phase checklist aligned with the delivered behavior.
- [x] Latest local validation: 44 Python tests, 217 native tests in 20 suites,
  lock/format/Cppcheck, and ESP-IDF 5.5.5 production compilation.
- [x] Last flashed product image (779504 bytes) flashed to app0 with hash verification;
  NVS/configuration preserved. Startup and advertising observed at 16:26:48–49.

### Physical acceptance

A checked row means the stated scenario was actually observed, not that all
variants or hostile-peer cases have been exercised.

- [x] Fresh authenticated pairing and READY: operator confirmation and serial at
  14:37:34–51. The earlier global-cleanup recovery flow was subsequently replaced
  by per-host Delete; its individual failure cases have native coverage.
- [x] One local Off → On cycle reused the saved pair at 14:39:20–26.
- [x] Selected-host restoration/reconnection after Reset at 14:40:02 and 14:40:07;
  the operator confirmed multiple Reset presses, explaining multiple splashes.
- [x] Two computers added, switching works, and powering Cardputer on reconnects
  to the last selected host: explicitly confirmed by the operator on 2026-09-11
  after the latest UI update.
- [x] Home, navigation and page-transition appearance accepted by the operator;
  the subsequent footer removal was visually checked with native render captures.
- [ ] Extended Off acceptance: at least 60 seconds without reconnect, reboot
  while Off, then explicit On and readiness on the final product image.
- [ ] Final-image report matrix: modifiers/six-key/consumer reports, neutral
  release on interruption, and no held-report carryover during host switching.
- [ ] Final pairing cancel/interruption/timeout and stale-code rejection matrix
  with existing profiles preserved. Native tests cover these paths.
- [ ] Final USB cable/HID usability rerun and disposition of serial hotplug.
  Earlier BLE stayed connected across a cable cycle, but serial did not return;
  the same serial observation occurred with BLE disabled. Root cause is open.
- [ ] Outstanding duration/equipment cases from plans 012/014: long Wi-Fi/BLE
  traffic/soak, IR smoke, controlled unsupported pairing methods and bond-capacity
  boundary. Historical successful cases remain evidence for their tested images.

Phase 2's software scope is complete. These remaining physical items prevent an
unqualified full-acceptance claim; they do not reopen USB HID, Mac-only reconnect
policy, or the future companion. Phases 3/4/6 are partially delivered; see the
[authoritative phase checklist](../ARCHITECTURE.md#47-initial-development-order).

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

Following physical testing, the user approved Cardputer-side host selection and
BLE On/Off, superseding the earlier Mac-only reconnect requirement. This plan
therefore brings forward HostService and host configuration from Phase 3, and a
Home and Bluetooth settings panel from Phases 4/6. It includes saved profiles,
selection, BLE On/Off, pairing prompts, existing-pair import, rename, and per-host deletion. Only the
selected host may connect outside explicit pairing. Off prevents connections and
survives reboot. macOS Disconnect may be followed by automatic reconnection while
Cardputer stays On; it is not the persistent Off control.

Normal firmware composes these Services and opens compact Home; Tab opens
Settings with a Bluetooth entry (Fn+Tab and G0 also work). Platform/capability
editing, the full Launcher/Mini App shell, broader configuration,
companion protocol, and Action-to-HID mapping remain later work. Phases 3/4/6
are only partially implemented; do not mark them complete.

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

## 3. Delivered BLE Interruption Correction

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

## 5. Historical Verification Record

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
on the physical device. No firmware was flashed during those automated checks.

Previous plan-014/015 physical results remain historical evidence; they are not
presented as a rerun of this image. The user explicitly deferred plugging in
and testing the device until implementation is finished.

### Physical rerun started on 2026-09-11 — incomplete

The plan-015 image was rebuilt from `600905a` and flashed without erasing NVS
or `hub_config`. Serial input and the validation banner were confirmed.

* The existing saved pair did not establish usable HID: the device logged a
  connection and a later disconnection, while the user reported connection
  failure in the OS. At the user's explicit request, all device bonds were
  removed; completion was verified with a zero count.
* Fresh authenticated pairing succeeded. Selecting the new sole bond reached
  `connected=yes`, `count=1`, and `hid=ready`.
* On the first battery-powered cable cycle, the user reported that the BLE
  connection remained. USB serial did not return. HID report delivery during
  that cycle was not checked and is not recorded as passing.
* USB serial also failed to return with Bluetooth never enabled after boot
  and the serial monitor closed before unplugging. The user confirmed the
  screen stayed on and still displayed the shared `Validation 014` screen.
* A cold entry into the ROM downloader on the same cable restored the port;
  a read-only capture confirmed `DOWNLOAD(USB/UART0)` and `waiting for download`.
  After repeating that entry sequence, switching battery power ON without
  Reset, and cycling USB with no serial client open, the port again disappeared
  and did not return. Port presence was observed without sending device commands;
  ROM residency throughout this last cycle was not independently instrumented.

The USB observation is reproducible independently of enabling BLE. Its root
cause and whether it is an expected device/host limitation remain unconfirmed;
do not label it a BLE routing regression or a passed serial-recovery check.
The validation image starts with Bluetooth disabled after Reset, so an OS
reconnect attempt before `bt enable` does not test BLE reconnection behavior.
Continue the BLE lifecycle/report checks with USB attached. Final physical
acceptance remains pending, including the serial-recovery observation.

With USB continuously attached, fresh pairing and selection again reached
`hid=ready`. After the user disconnected in macOS, advertising resumed and the
bonded host reconnected automatically, but `hid=starting` persisted for over a
minute. This is an unresolved HID readiness observation, separate from USB.
The plan-015 harness now traces only boolean security/subscription/protocol
flags from adapter events to identify the missing prerequisite. Adding this
temporary hardware diagnostic before a native test is the TDD exception for
this investigation: the harness depends on physical ESP-IDF integration and
the trace preserves events unchanged. Any behavioral fix must have a failing
regression test before implementation.

The diagnostic rerun on the preserved bond produced no security or subscription
events after the host reconnected. Inspection found that the Service requested
security only for fresh pairing. The correction explicitly requests security
restoration for each admitted bonded connection through
`IBluetoothAdapter.restoreBondSecurity`, independently of new-pairing capacity.
It preserves the explicit pairing window and existing bond data. New native
tests failed before implementation (Ready remained Starting; an initiation
failure was not handled) and passed afterward. Rejected peers are also checked
to ensure they never start security restoration. This change alone did not
resolve the physical symptom: five disable/re-enable cycles still lost HID
readiness even though a direct adapter snapshot confirmed encryption.

The raw GAP trace then established the cause: NimBLE delivered encryption and
restored CCCD subscriptions before CONNECT. The adapter discarded events for
the not-yet-announced peer, then reset subscription state on CONNECT. Its new
bounded peer-event ordering helper announces a live peer before forwarding
early data, suppresses duplicate CONNECT, and resets on disconnect/shutdown.
The temporary callback trace was removed after diagnosis; callbacks retain
bounded event copying without serial output. Four native regressions cover
early security/subscriptions, normal and duplicate
CONNECT, handle reuse, and rejection of a different concurrent handle. The
legacy ordering failed three cases before correction; all four passed afterward.

The corrected validation image was flashed without erasing bonds. On the same
saved pair, both observed event orders (security before and after CONNECT)
restored encryption, authentication, and both HID subscriptions across five
disable/re-enable cycles. One immediate status sample was still Starting before
security completion; the subsequent events and live snapshot confirmed restored
prerequisites. No new pairing code was needed. The user then pressed macOS
Disconnect once; the trace recorded disconnection, the one-second advertising
retry, and automatic reconnection with encryption and both subscriptions. A
later status confirmed `hid=ready`, with the sole bond intact. This is not a pass
for the complete physical acceptance matrix. Additional earlier manual disconnections were confirmed by the user,
not classified as spontaneous loss.

After removing the temporary callback trace, the final validation image was
flashed with the saved bond preserved and again reached `hid=ready`. With the
user's explicit approval and a user-positioned text cursor, one keyboard press
and immediate neutral release both returned Success. The user confirmed one
character in the active keyboard layout with no repeat. Report delivery after
a manual host disconnect/reconnect, modifiers, six-key input, and consumer
controls remain pending.

Acceptance correction: the user requires explicit macOS Disconnect to remain
disconnected. The automatic reconnect observations above demonstrate HID state
restoration only; they do not pass intentional-disconnect behavior. Currently
the adapter discards the disconnect reason and the Service restarts advertising
after every current-peer disconnection. This policy defect remains unresolved.
BLE was disabled to stop unwanted reconnection. The validation adapter now copies
the reason into its bounded event and logs only that numeric code from polling,
not from the callback. This is a hardware diagnostic with no retry-policy change;
it uses the same documented diagnostic TDD exception. Capture the host's actual
reason before deciding how to preserve explicit disconnection while retaining
unexpected link-loss recovery and an intentional way to connect again.

The subsequent user-triggered macOS Disconnect produced NimBLE reason 531
(`0x213`, HCI Remote User Terminated Connection `0x13` with the NimBLE HCI
error base `0x200`). The current implementation restarted advertising one
second later and accepted reconnection. BLE was then explicitly disabled.
This establishes remote-requested termination for this observed case; the code
does not identify a particular OS UI action in general. The resume interaction
is being clarified: stopping connectable advertising prevents both automatic
and manual host connection until the device resumes it.

The user requires reconnecting from the Mac's Connect control, without a
Cardputer action. A disposable plan-015-only probe therefore keeps undirected
connectable advertising after remote termination but removes general discovery
and uses the HOGP host-initiated idle interval (1–2.5 seconds). It does not alter
bonds or HID attributes. This is an unverified hardware experiment under the
documented exploration exception, not a production fix; remove it if ineffective,
or replace it with a tested Service/adapter contract if the experiment succeeds.

The passive-advertising probe failed acceptance. The device recorded remote
termination followed by restored connections, including roughly 24–28-second
waits. The user confirmed the Connect button was absent, so no manual Connect
request preceded those later connections. BLE was disabled and the probe was
removed. Neither suppressed automatic reconnection nor manual reconnection
from macOS was established. Do not retain the probe as a fix or replace the
requested host-side controls with an unapproved Cardputer resume requirement.

Read-only macOS diagnostics narrowed the behavior further. At each user-side
disconnect (12:02:54, 12:02:58, and 12:03:45 local time), `bluetoothd` recorded
successful locally initiated disconnection, followed within milliseconds by
`reconnecting: Y`, `Auto Reconnecting`, and `Connect Requested` for the system
`com.apple.BTLEServer` session. These requests preceded the Cardputer's next
advertising start. The passive probe delayed fulfillment of an already-pending
host request; it did not disable that request. This evidence identifies the
reconnecting host client but does not establish why its policy remains active
after the OS Disconnect control. The earlier attribution solely to firmware
retry policy was premature. No macOS settings, pairing records, or UI were
changed by the agent. A firmware-only solution preserving the user's required
Mac Disconnect/Connect controls has not been demonstrated. Physical acceptance
and this issue remain open; the restored diagnostic image is left BLE-disabled.

The user requested comparison with ordinary Bluetooth HID implementations.
The supplied `fat23cat/codex-microputer-adv` implementation uses ESP-IDF
`esp_hidd` and also restarts advertising on disconnect; its dependency patches
do not introduce a manual-host-disconnect policy. Upstream ESP32-BLE-Keyboard
sets HID Information flags to `0x01` (RemoteWake, without NormallyConnectable),
while this adapter uses `0x02`, Adafruit BLE HID also uses `0x02`, and ESP-IDF's
NimBLE HIDD uses `0x03`. There is no single flags value shared by all references.

A disposable plan-015-only comparison changes only the HID Information flags
to ESP32-BLE-Keyboard's `0x01`; production flags remain unchanged. The harness
queues a non-identifying diagnostic when the host actually reads HID Information
so a cached host profile cannot be mistaken for a test of the changed value.
This is a hardware comparison under the documented exploration exception, not
a validated correction. Preserve bonds, and do not infer success from a changed
advertising delay or from automatic reconnect. Remove the probe if ineffective.

The preserved bond reconnected but did not read HID Information in the first
probe run. The comparison therefore also sends one standard GATT Service Changed
notification per validation lifecycle after authenticated subscriptions return,
requesting rediscovery without deleting the bond. A host read of the new flags
is still required before attributing behavior to them. This cache-refresh probe
is also confined to plan 015 and is not a production migration policy.

Reference: [ESP32-BLE-Keyboard initialization](https://github.com/T-vK/ESP32-BLE-Keyboard/blob/master/BleKeyboard.cpp)
and [the supplied Cardputer BLE implementation](https://github.com/fat23cat/codex-microputer-adv/blob/main/main/ble_transport.cpp).

The host read flags `0x01` at 12:18:11 on 2026-09-11, confirming that the
cache-refresh comparison exercised the changed value. The user then reported
automatic reconnection again. The device trace recorded remote termination at
12:28:34 and 12:28:38, followed by connections at 12:28:35 and 12:28:39.
This probe failed acceptance and was removed from the source, including the
forced Service Changed notification and the HID Information read diagnostic.
Production HID flags remain `0x02`. Neither flags nor a slower advertising
interval has established the required host-side Disconnect/Connect behavior.
The next control comparison uses the pinned ESP-IDF HID device example in a
separate disposable build; it is not a replacement architecture or a claimed fix.

That control image compiled and was flashed into app0 only, with the write hash
verified. Serial confirmed `esp_hid_device` startup on the Cardputer. It uses the
example's NimBLE keyboard profile and disconnect callback, with automatic input
tasks disabled. A fresh static-random identity and volatile bond storage isolate
it from the product's saved pair and cached host profile; NVS-error erase and
repeat-pairing bond deletion are disabled. The example's metadata and battery
value are test fixtures, not product identity or battery telemetry. No Cardputer
display UI is initialized. It is advertised as `HID Reference`; user-operated
pairing and the manual Disconnect/Connect comparison are pending. This comparison
can test whether the symptom also occurs without Cardputer Hub's services, but
cannot by itself identify a particular profile field or prove a universal macOS
limitation. The cleaned product validation image is built and available to restore.

After probe removal, the repository checks were rerun successfully: formatting,
static analysis, 44 Python tests, 170 native tests, production firmware build,
and plan-015 validation build. Relative documentation links, code fences, and
`git diff --check` also passed. These automated results do not close physical
Disconnect/Connect acceptance.

The first reference pairing attempts did not establish an encrypted connection,
so they provide no evidence about intentional-disconnect behavior. One attempt
failed exactly 30 seconds after passkey injection; its error status was not
printed by the example, so timeout is an inference rather than a decoded cause.
The comparison also exposed test-setup problems: a reboot changed its temporary
address, creating duplicate nearby entries, and the GAP name remained `nimble`
despite the advertising name. The disposable setup was revised to use the
consistent name `HID Control`, a separate static-random address fixed for the
test build, and a random passkey prepared at boot so it can be given to the user
before opening the host dialog. Numeric encryption failure status is now logged;
passkeys remain excluded from persisted serial logs. These are comparison-harness
changes, not production Bluetooth policy changes or evidence of a resolved issue.

The user redirected the investigation to maintained Cardputer firmware before
that convenience revision was flashed. The subsequent
[release-pinned Bruce review](../validation/ble-hid-reference-review.md) identifies
its different disconnect advertising policy and records the local regression
boundary. The failed disposable example is not a valid reconnect comparison;
its pairing sequence is discontinued. Host-only manual reconnect remains open.

Automated verification of the reconnection correction passed: lockfile check,
clang-format, Cppcheck (the existing low-priority callback-const suggestion),
44 Python tests, and 170 native C++ tests across 15 suites. The production
ESP32-S3 firmware and the BLE validation image both compiled with ESP-IDF
v5.5.5. The validation image was rebuilt again after removing the temporary
callback trace. At that checkpoint these diagnostics and fixes affected only the opt-in BLE
harness and Connectivity foundation. Product controls and manual instructions
were subsequently added by the HostService work below.

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
work remains. This historical harness checkpoint is superseded for product
controls by the HostService acceptance section below.


## 6. HostService and Local Host Settings (2026-09-11)

This addition implements the revised user-approved control model rather than
another macOS reconnection-delay workaround. HostService owns selected-host and
pairing policy, ConfigurationService owns the bounded version-1 host schema in
`hub_config`, BluetoothService owns connections/security, and HostSettings owns
only local input/rendering through ActionBus. USB remains diagnostics-only.
The service contract and phase boundaries are authoritative in Architecture
sections 16, 43, and 47; delivered controls are in the device guide.

RED/GREEN evidence: configuration round-trip/invalid-data/write-failure tests,
HostService import/switch/Off/storage-failure/pairing-cancel tests, local UI Action
routing, and idle-start/advertising-filter policy regressions were first observed
failing and then passing. Additional tests cover pairing completion, timeout,
and a missing selected bond. Hardware-only controller accept-list behavior and
composition wiring require firmware compilation and device acceptance; native
fakes do not establish physical Bluetooth behavior.

Original product acceptance checklist at the HostService checkpoint. Current
results and remaining cases are consolidated in section 0 above:

1. Boot without erasing NVS; see Hosts settings and imported profiles. With no
   previous host configuration, BLE stays Off and does not advertise.
2. Select/pair a host with the local controls; authenticate and reach READY.
3. Rename and reboot; verify stable profile identity, selection, and enabled state.
4. Turn Off locally; wait at least 60 seconds and verify no host reconnects.
   Reboot while Off and verify it remains Off. Turn On and verify readiness.
5. Pair a second computer. With both available, switch in both directions; only
   the selected computer may connect and no held reports reach the new host.
6. Cancel/interrupt pairing and let it expire; verify restoration of saved intent,
   stale-code rejection, and preservation of existing pairs.
7. Complete the outstanding HID report and USB cable checks above, preserving
   the recorded distinction between serial enumeration and BLE behavior.

A future companion CLI is not part of this implementation. It can eventually
invoke the same HostService intentions through an authenticated control channel;
simply cancelling a Mac application's CoreBluetooth connection is not a proven
way to stop the separate system HID client. Resuming after radio Off also needs
an available control path or local Cardputer input.


### Final automated verification for the HostService addition

On 2026-09-11, `make host-check UV=/private/tmp/uv-0.12.7/uv` passed dependency
lock validation, clang-format, Cppcheck, 44 Python tests, and 187 native C++ tests
across 18 suites. The new integration tests exercise real HostSettings →
ActionBus → HostService → BluetoothService with fake hardware/storage; they
check local selection/Off, rename cancellation, pairing-progress text, and
240×135 geometry. Configuration tests additionally reject every truncation of a
valid record, trailing bytes, and an unknown version without overwriting data.
Full-list pairing rejects before interrupting the selected host.

The ESP-IDF v5.5.5 production image and the plan-015 BLE diagnostic image both
compiled. `git diff --check` and the relative-file link check passed (19 local
Markdown links checked). These results do not establish physical acceptance.

Adapter verification exception: correcting advertising-stop bookkeeping is based
on the pinned ESP-NimBLE `ble_gap_adv_stop()` implementation, which returns after
stopping and does not emit ADV_COMPLETE. The hardware adapter cannot run in the
native fake suites. Its production/validation builds pass; physical selected
advertising and pairing-filter transitions remain required device checks.


The production image (734880 bytes) was flashed to app0 at 0x10000 with esptool
hash verification. Boot at 13:37:16 reported Cardputer-Adv and the normal product
entry point; BLE finished Disabled without a reset/error in the observed startup.
Only the application partition was written; no NVS or bond erase was performed.
Screen visibility, READY, local Off, reboot restoration, and two-host switching
still await operator confirmation on this final image.


### Local navigation feedback

The operator confirmed the Hosts screen responds to navigation, but reported
that holding Fn is inconvenient and every focus move flashes the display.
Regression tests reproduced both problems: plain `.` did not navigate and a
single focus move issued a second full-screen clear. The list now maps plain
`;`/`.` to Up/Down Actions and repaints only changed rows/regions. Name entry
retains those punctuation characters, pairing entry remains digits-only, and
named arrow events still work. Additional coverage checks scrolling and cache
invalidation on returning from rename. Physical smoothness on the revised image
still requires operator confirmation; no BLE acceptance result is inferred from
this UI feedback.


Navigation follow-up verification: `make host-check` passed formatting, static
analysis, 44 Python tests and 190 native C++ tests; `make firmware-check` compiled
the revised production image (736112 bytes). `git diff --check` passed. The native
regressions confirm a single focus move leaves the header and unchanged list
rows untouched, while scrolling and return from rename refresh the correct areas.

The revised image was flashed to app0 with hash verification, preserving NVS.
Serial confirmed the normal Cardputer-Adv startup at 13:44:30. Perceived display
smoothness remains an operator check; no OS UI actions or HID test keys were sent.


Post-flash monitoring then caught three startup panics at 13:44:33–38 before a
successful Off startup. Decoding the captured backtrace against that exact ELF
located a null NimBLE callout inside stage-2 startup (`ble_hs_sync` →
`ble_hs_timer_sched` → `npl_freertos_callout_is_active`). HostService's initial
Idle/import/Off sequence could request shutdown before the new host synchronized.
The adapter now waits for its first synchronization callback before stopping;
the wait is bounded to five seconds and retains ownership on timeout rather than
freeing resources still used by startup. No fixed startup sleep, NVS erase, or
BLE advertisement was added. This is an adapter-only race: the physical crash is
the RED reproduction; the native runner cannot execute ESP-IDF/NimBLE task and
callout scheduling. Firmware builds and repeated physical startups are required
GREEN evidence in addition to the unchanged host lifecycle tests.


Startup-race follow-up GREEN evidence: the corrected production image (736208
bytes) and BLE validation image compiled; full host checks passed with 44 Python
and 190 native tests. Flash hash verification passed. Five consecutive hardware
resets, each observed for six seconds, each produced exactly one product startup
and Bluetooth Disabled, with no panic, watchdog, dropped-host-packet warning, or
error record. NVS and bond data were preserved. This validates the reproduced
startup/Off sequence over those runs, not all BLE lifecycle interleavings or the
still-pending two-host/UI visual acceptance.


## 7. Default Home and Bluetooth Panel

The user requested a default Home screen leading to Bluetooth controls, following
the UI palette and typography. Normal composition now starts ApplicationShell at
Home, with actual selected-host/BLE state and a single Bluetooth settings entry.
Enter/B opens it; Back from the list returns Home without switching BLE off.
Pairing/rename Back dismisses that local view first. The existing Bluetooth row
is the explicit On/Off control. The NavigationStack and ActionBus own routing;
HostService still owns all connection/persistence behavior.

Home uses a native Micro 5 wordmark, Font0 labels, Bone/Ink structural colors,
and labelled Pale/Blue/Leaf/Vermilion BLE state surfaces. The panel uses stable
row ordinals, an Ink focus plate, and right-aligned On/Off/ACTIVE values. Canvas
frame grouping in the hardware adapter prevents page-entry clears from appearing
as blank intermediate screens; a settled view transfers nothing. Full animated
shell transitions, sound, display-power management, Launcher and Mini App
lifecycle remain outside this delivered slice of Phase 4.

RED/GREEN tests cover Home-first boot, consuming the opening key once, navigation
without radio side effects, modal-first Back, unchanged-frame suppression,
selected-host display, long-name geometry, and updates that preserve the current
view. A separate font regression found that the existing Micro 5 digit sheet is
ordered `1234560789`; lookup now follows that order so the displayed pairing code
matches the numeric challenge. This bug belonged to the newly introduced UI,
not the earlier Bluetooth reconnection experiments.

Optional visual review captures use only synthetic host fixtures:

```bash
mkdir -p /tmp/cardputer-ui
CARDPUTER_UI_CAPTURE_DIR=/tmp/cardputer-ui make test
# Requires Pillow 12.3.0 and the pinned M5GFX managed component:
python scripts/render_ui_capture.py /tmp/cardputer-ui
```

The captures record real UI draw commands and render with the pinned Font0 bitmap
and generated Micro 5 masks; no running-device state, pairing code, or OS UI is
captured. Screen updates inside a frame present once at completion. The hardware
canvas/clip transfer is verified by firmware compilation and operator inspection;
native tests verify grouping/geometry but cannot establish LCD timing.


Home/panel verification: full host checks passed (44 Python tests, 194 native C++
tests, formatting and Cppcheck). ESP-IDF v5.5.5 built the production image and the
BLE validation image with the updated display adapter. `git diff --check` and
local Markdown link checks passed. Home, Bluetooth, maximum-length selected-host,
and rename captures were reviewed at native size and nearest-neighbour 3× size;
labels, right-aligned values, and footer hints stay inside the 240×135 raster.
The wordmark mask was also compared against the pinned source font raster.


The Home production image (746288 bytes) was flashed to app0 with hash verification,
without writing or erasing NVS. Serial at 14:12:05 confirmed normal Cardputer-Adv
startup and Bluetooth Disabled; no panic/error followed in the observed startup.
The final Home/panel appearance and LCD transition smoothness still require the
operator's confirmation. No OS UI actions or host HID input were performed.

### First-enable regression

The operator reported that Enter on Bluetooth appeared to do nothing and Home
then displayed ERROR. Real-service UI regression tests reproduced the path:
imported profiles had no active selection, enabling returned InvalidInput, and
Home treated every rejected action as a Bluetooth fault. Three tests failed
before the fix and passed afterward. HostService now returns the distinct
HostSelectionRequired prerequisite without touching the adapter or storage.
The panel focuses a saved host or Add device and displays an Enter instruction;
confirmation selects the host or opens pairing. Home keeps showing OFF for this
prerequisite and invalid input, while real backend/missing-bond failures remain
ERROR. Existing selected-host On/Off and pairing cancellation are also covered.

Verification: `make host-check` passed (44 Python and 197 native C++ tests,
formatting, and static analysis); `make firmware-check` built the 746448-byte
production image. Local Markdown link targets and `git diff --check` passed.
The image was flashed to app0 with hash verification, without NVS writes/erase.
Serial at 14:21:22 confirmed normal startup and Bluetooth Disabled. The corrected
button flow still needs operator confirmation on the physical screen.

### Host activation startup race

After first-enable guidance, the operator could select a host but saw Bluetooth
Off / ERROR and a misleading ACTIVE label. Added non-identifying HostService
failure-stage diagnostics; the device reproduced `bond selection failed` on
boot at 14:26:20, before advertising. The pinned NimBLE source calls
`ble_store_config_init()` again from `ble_hs_pvcy_set_default_irk()` during
stage-2 startup with CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC enabled, clearing and
reloading bond counts while immediate Service registry queries were running.

The adapter now waits for its initial synchronization callback before returning
successful initialization. The bounded completion barrier already used for safe
shutdown prevents callers observing the transient bond registry. Timeout retains
cleanup ownership and reports initialization failure. Bonds and Host Profiles
are not erased. This hardware concurrency fix uses the observed failure as RED:
native adapter fakes cannot reproduce the ESP-IDF privacy task/store race. Native
selection/restart tests remain required, followed by physical startup verification.
The selected-host label has a separate failing native UI regression and changes
from ACTIVE to SELECTED, including on Home, to distinguish intent from READY.

Verification: formatting, Cppcheck, 44 Python tests and 197 native C++ tests
passed; production and BLE validation firmware compiled. The 746944-byte
production image was flashed with hash verification and no NVS erase/write.
At 14:29:17 the same saved selection reached advertising requested/started,
without the previous bond-selection failure. At 14:29:26 the operator opened
pairing, which reached advertising normally; pairing completion and READY are
not yet verified. Repeated boot checks were deferred when that operator-driven
pairing window appeared, to avoid interrupting it.

### Explicit host cleanup for pairing recovery

The operator then reported that macOS offered the device under Nearby Devices,
while Add device did not produce a pairing challenge. The current pairing policy
rejects already-bonded peers in the new-pair window, making divergent one-sided
records a plausible separate cause. Pairing success is not inferred from
advertising. To support the requested clean start, HostService now exposes
`forgetAllHosts` / `host.forget-all`, with a Cardputer X / Enter confirmation.
It stops BLE, persists Off before deleting keys, clears only host profiles and
Bluetooth bonds, and preserves the next-host ID and unrelated configuration.
Failures stay closed and retryable. Successful cleanup focuses Add device but
does not open pairing until Enter. Individual deletion remains later Phase 3/6
work; this recovery slice does not close the pending physical BLE acceptance.
Two real-service integration tests failed before implementation, then passed:
confirmation/cancellation, persisted empty state, new pairing after cleanup,
storage failure before deletion, and bond-deletion failure with retry.

Cleanup verification: `make host-check` passed formatting, Cppcheck, 44 Python
and 199 native C++ tests. Production firmware compiled (748368 bytes), flashed
to app0, and its hash verified. Boot at 14:37:08 reached Bluetooth Disabled
without a startup error. Flashing did not erase NVS or invoke cleanup; the
operator still needs to confirm X / Enter and validate a fresh pairing.

The operator subsequently confirmed that cleanup and fresh pairing succeeded.
Serial records the pairing window at 14:37:34 and authenticated pairing completed
at 14:37:51. This verifies recovery through a fresh authenticated pair on the
physical Cardputer-Adv. Repeated Off/On, selected-host restoration after reboot,
and filtering between two computers remain separate pending acceptance checks.

The operator confirmed the subsequent Cardputer Off → wait → On cycle worked.
Serial shows Bluetooth Disabled at 14:39:20, advertising resumed at 14:39:26,
and a bonded peer connected at 14:39:26. This confirms one successful reuse of
the saved pair without a new pairing procedure. Reboot restoration and
selection/filtering between two computers are still pending.

The operator confirmed selected-host reconnection after Reset without re-pairing,
but reported the version splash flashing several times. Serial records two
startups at 14:40:02 and 14:40:07, each followed by a bonded connection; both
captured reset reasons are USB_UART_CHIP_RESET (0x15). No panic/backtrace appears
in those startup records. The number of physical Reset presses is not yet
confirmed, so repeated splash appearances must not be classified as harmless
redrawing or a firmware crash without further evidence.

The operator clarified that Reset was pressed two or more times. The two
recorded startups and repeated splash appearances are therefore consistent
with the requested resets; this observation does not indicate spontaneous
reboots. Selected-host restoration and reconnection after Reset are confirmed.

### Per-host actions and quieter Bluetooth settings

The operator requested removal of Move/Enter hints and the global X cleanup.
The list now shows only Esc Home. Opening a saved host shows Connect, Rename,
and Delete without changing connection intent. Deletion has a named-host
confirmation, removes only that profile/bond, and returns to the list. The
previous `forgetAllHosts` / `host.forget-all` slice is replaced by `deleteHost(id)`
/ `host.delete`. Deleting the selected host persists Off and clears selection;
deleting another host preserves the current connection. Missing bonds permit
stale-profile cleanup; failures retain the profile for retry. Tests cover these
behaviors, cancellation, rename, plain-arrow navigation, and incremental menu
painting. Individual deletion is now delivered in the Phase 3/6 slice; platform,
templates, and full Mini App lifecycle remain pending.

Verification: formatting, Cppcheck, 44 Python tests and 202 native C++ tests
passed; production firmware compiled (749984 bytes). Synthetic captures of
the host action menu, deletion confirmation, and maximum-length host list were
reviewed for layout and legibility. Local Markdown links and `git diff --check`
passed. The image was flashed with hash verification, preserving NVS; startup
at 15:30:03 reached advertising. Physical rename/delete interaction remains
for the operator to validate; no actual host was deleted by the agent.

### Compact Home and general Settings

Following the operator's mockup, Home now contains only a HOME header, selected
HOST name and labelled BT status. Fn+Tab opens SETTINGS, with Bluetooth as its
only implemented item. Enter opens the existing Bluetooth/host UI unchanged,
including Esc Home. Fn+Tab returns from the BLE list to Settings but does not
abandon a host submenu, edit/delete confirmation, or pairing. The shell owns
`ui.settings` / `ui.bluetooth`; the Cardputer layout only exposes Fn+Tab as Tab
with the Fn modifier. Enter/B/plain Tab do nothing on Home. The unused Home
wordmark mask, associated generator and duplicate license copy were removed;
the Micro 5 pairing digits and their license are retained.

Before implementation, native tests failed for the compact Home/Settings flow
and physical Fn+Tab mapping; both now pass. Service integration tests use the
new route while retaining existing BLE action coverage. Repeated shortcuts,
modal preservation, idle drawing suppression and long host names are covered.

Verification: `make host-check` passed formatting, Cppcheck, 44 Python and
203 native C++ tests. Production firmware compiled (749920 bytes), flashed to
app0, and its hash verified without erasing NVS. Boot at 15:40:15 reached
advertising at 15:40:16. Synthetic Home (including a maximum-length host name)
and Settings captures were visually reviewed. Local Markdown links and
`git diff --check` passed. Physical Fn+Tab operation and appearance await the
operator's confirmation; the Bluetooth panel implementation was not changed.


### Approved Home refinement (11 September 2026)

The operator selected dashboard variant 02 with a smaller Micro 5 host label,
a single Wi-Fi status without SSID, and variant 04's slow dotted wave. Home now
uses that layout at the native 240×135 raster. Fn+Tab → Settings → Bluetooth
and the existing Bluetooth panel are unchanged. Long host labels use an
ellipsis without modifying HostProfile data.

BatteryService supplies the board's estimated charge through a hardware adapter,
with five-second sampling and explicit unavailable state. Time synchronization
and Wi-Fi setup remain future work; their slots display `--:--` and `OFFLINE`.
The wave is a deliberate UI motion exception: a 32-second cycle, at most 2 fps,
limited to the lower 36 rows and paused off Home. Display-power policy remains
pending; when implemented it must suspend the wave while the display is off.

TDD: new Home animation/unavailable-telemetry tests were observed failing before
rendering was implemented; battery sampling/unavailable-value tests likewise
failed before the Service implementation. Native drawing captures verify the
actual font masks, compact/long names, and header spacing. Hardware visual
acceptance follows the production build; it does not replace pending
multi-computer BLE acceptance.

Verification: `make host-check` passed (44 Python tests, 208 native tests across
19 suites, clang-format, Cppcheck, and lock check). ESP-IDF 5.5.5 production
`make firmware-check` passed; image size is 778704 bytes. Drawing captures cover
Home with no host, a compact name, an ellipsized name, unavailable battery,
100% battery, and a wave-only update. `git diff --check` passed.

The application alone was flashed at `0x10000` with hash verification on
11 September 2026. NVS and configuration partitions were preserved. Serial
showed normal startup and advertising at 16:05:42 local time, followed by
`bonded peer connected` at 16:05:43. Physical appearance/motion and Fn+Tab
acceptance of this refinement still require the operator's observation.


### Single-button Settings and quieter battery header (11 September 2026)

G0 now opens Settings during normal operation, using M5Unified's debounced BtnA
press edge as a local SystemMenu input routed through `ui.settings`. Fn+Tab
remains available. Holding G0 does not repeat; boot/reset download behavior is
unchanged. The button remains readable even if matrix-controller initialization
is retrying. Existing host submenus and rename/delete/pairing modals consume
this menu input without dismissing their state or changing BLE intent.

Home now shows battery percentage only. The unused battery-icon drawing helper
was removed. At the operator's request, the ambient wave is slightly more
visible (`#D1CEC4`) and its cycle is shortened from 32 to 28 seconds, retaining
the two-frame-per-second limit and lower-region-only drawing.

The new SystemMenu routing test failed before implementation, then passed.
Tests cover repeated menu input, return from the BLE list, modal preservation,
and absence of radio Actions. The hardware read itself is a thin M5Unified
call; its physical acceptance requires the operator pressing G0.


G0/Home verification: `make host-check` passed with 44 Python and 209 native
tests, formatting, static analysis, and lock validation. Production compilation
passed (778448 bytes). The app-only flash was hash-verified, preserving NVS;
normal startup/advertising was observed at 16:13:12 and bonded reconnection at
16:13:13 local time. G0 physical acceptance remains with the operator.

### Screen transitions (11 September 2026)

At the operator's request, add short horizontal slides between Home, Settings,
Bluetooth, host actions, rename/delete, and pairing views. Forward enters from
the right, Back from the left; timing is a 220 ms cubic ease-out with positions
updated at most every 16 ms. Focus movement, text entry, and status refresh do
not start page transitions. The Home wave pauses during a transition.

The shared Core SlideTransition owns time/geometry. Views request direction;
the shell supplies elapsed time, and the hardware display adapter captures and
presents pixels. A temporary extra 240×135 RGB565 snapshot is released on
completion. Allocation failure retains immediate completed-frame presentation.
Inputs and BLE continue during animation; interruption composes the visible
frame in place as the next outgoing snapshot instead of waiting or jumping.

TDD: timing, bounded progression, both snapshot directions, and navigation tests
were observed failing before implementation. Native coverage includes modal
navigation, ignored focus/status refresh, oversized/negative elapsed time,
and exact pixel continuity during interruption. Physical smoothness and
G0/button operation remain operator acceptance items.


The operator found G0 awkward and requested a main-keyboard alternative. Plain
Tab is now the primary Settings key on built-in system screens; Fn+Tab and G0
remain alternatives. Modals consume it without abandoning input. It is not a
global Tab override for future text-entry Mini Apps. The new plain-Tab test was
observed failing before routing changed.

Final local verification for Tab/slides: `make host-check` passed (44 Python
and 214 native tests across 20 suites, formatting, Cppcheck, lock validation).
`make firmware-check` passed under ESP-IDF 5.5.5; application size is 779584 bytes.
A native preview using the production SlideTransition and snapshot compositor
verified forward/backward frames at 0, 48, and 220 ms against actual UI drawing
captures. `git diff --check` passed.

The application was flashed at `0x10000` with hash verification. Configuration
and BLE-bond partitions were preserved. Normal startup, advertising, and
`bonded peer connected` were observed at 16:23:12 local time on 11 September
2026. Physical transition smoothness and Tab operation await the operator.


### Remove the Esc Home bar (11 September 2026)

After accepting the updated UI, the operator requested removal of the bottom
Esc Home bar. Settings and the root Bluetooth list now omit both the separator
and label. Escape/backtick navigation is unchanged. Contextual Esc Back and
Esc Cancel hints in host submenus and confirmation/pairing views remain.
The existing footer test was updated and observed failing before removal.

Verification: 44 Python tests and 214 native tests passed with formatting,
Cppcheck and lock validation. ESP-IDF production compilation passed (779504
bytes). Native Settings/Bluetooth captures confirm both footer regions are
empty. The app-only flash was hash-verified with NVS preserved; normal startup
and advertising were observed at 16:26:48–49 local time. `git diff --check`
passed.


### Review fixes and Linux CI (11 September 2026)

Host actions now retry failed startup initialization before applying current
intent, without briefly advertising for the previously saved host. Persistent
errors retain their specific result and preserve stored data. Cached pairing
prompts are tied to the admitted peer and disappear on interruption, including
when a replacement peer arrives in the same update. Architecture and the device
guide describe these recovery paths.

Three new regression tests failed before implementation and passed afterward.
They cover explicit retry, repeated failure, corrupt configuration preservation,
all three pairing prompt types, same-update peer replacement, and the original
pairing timeout. The previous PR CI failed on GCC's range-loop-copy warning in
the font test; that loop now binds its pair by const reference.

Final local checks passed: 44 Python and 217 native tests across 20 suites,
lock/format/Cppcheck, documentation links and `git diff --check`. ESP-IDF 5.5.5
production compilation passed (779808 bytes). This review-fix image has not been
flashed; the physical acceptance evidence above still refers to earlier images.
