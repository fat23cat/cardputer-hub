# BLE HID Transport Plan

Status: **Planned**

This plan describes the fifth granular Phase 2 change. It defines the shared
hardware-neutral HID report contract and adds a BLE HID keyboard and consumer
control transport to the authenticated, selected Bluetooth connection.

Implementation should use:

```text
branch:   feat/015-ble-hid-transport
PR title: [015] Add BLE HID keyboard and consumer transport
```

The implementation branch must begin from updated `main` after plan 014 is
merged.

## 1. Summary

Add:

* shared hardware-neutral keyboard and consumer-control report types;
* a common `IHidTransport` contract for later USB/BLE arbitration;
* BLE HID readiness based on authenticated connection and subscriptions;
* a standard HID-over-GATT service using the selected plan-014 bond;
* checked, non-blocking keyboard, consumer-control, and release reports;
* native behavioral tests and physical host validation.

This plan does not translate text, interpret logical Actions, define host
shortcuts, implement mouse reports, or activate host control during normal
boot. Phase 7 will resolve Actions into the HID transactions routed by Phase 2.

## 2. Shared HID Contract

Add the hardware-independent API under `src/connectivity/hid/`:

* `HidKeyboardReport`: an eight-bit USB HID modifier mask and exactly six
  keyboard usage slots; zero-filled slots are unused;
* `HidConsumerReport`: one 16-bit Consumer Page usage, with zero representing
  release;
* `HidReport`: a tagged keyboard-or-consumer value;
* `HidTransportState`: `Unavailable`, `Starting`, `Ready`, `Busy`, or `Error`;
* `HidSendResult`: `Sent`, `NotReady`, `Busy`, or `AdapterError`;
* `IHidTransport`: `state`, `send`, and `releaseAll` operations.

Contract rules:

* report values are HID usages, not printable characters or platform-specific
  shortcuts;
* keyboard reports reject duplicate non-zero usage codes and invalid rollover
  input before reaching an adapter;
* a neutral keyboard report has no modifiers and six zero usages;
* a neutral consumer report has usage zero;
* `releaseAll` sends both neutral reports and succeeds only if both are
  accepted or the transport is definitively disconnected;
* calls are synchronous and non-blocking; `Sent` means the report was accepted
  for transport, not that the remote application acted on it;
* `Busy` is retryable by the caller and must not be converted into a blocking
  wait;
* an adapter error enters transport `Error`; it must never cause silent output
  over a different transport;
* implementations own copies of any report queued after a call returns;
* report contents are private input data and must never be logged.

The initial report surface intentionally supports the six-key keyboard format
and one consumer usage at a time. Mouse, gamepad, NKRO, text encoding, macros,
and host-specific key maps remain outside this plan.

## 3. Bluetooth Service and Adapter Behavior

Make `BluetoothService` provide the BLE implementation of `IHidTransport` and
extend `IBluetoothAdapter` with checked HID readiness, report-send, and
release operations.

Required behavior:

* BLE HID is `Unavailable` while Bluetooth is disabled, advertising, retrying,
  pairing, disconnected, or connected to an unselected peer;
* a selected connection becomes `Ready` only after the link is encrypted,
  authenticated, bonded, and the host has subscribed to every required input
  report;
* connection alone is insufficient for readiness;
* loss of encryption, subscription, selection, or connection removes readiness
  before any later report is accepted;
* keyboard and consumer reports are sent only to the current selected peer;
* `releaseAll` sends neutral keyboard and consumer reports to that same peer;
* disabling Bluetooth or changing the selected bond requests release before
  disconnection when the link remains usable;
* a hard BLE disconnect is treated as neutralization by the host and does not
  cause reports to be replayed after reconnection;
* re-enable creates a fresh HID service lifecycle with no queued report or
  subscription state from the prior lifecycle;
* stale, wrong-peer, or prior-generation HID callbacks are ignored;
* HID send, subscription, registration, or teardown failure follows existing
  Bluetooth cleanup ownership and enters `Error` when safe operation cannot be
  guaranteed;
* pairing and bond-management behavior from plan 014 remains unchanged.

## 4. ESP-NimBLE HID Implementation

Extend the existing direct ESP-NimBLE adapter:

* enable the ESP-IDF 5.5.5 NimBLE HID service support already present in the
  pinned framework; do not add NimBLE-Arduino or a second host lifecycle;
* add `esp_hid` to the production component requirements and use its NimBLE HID
  device support where it can coexist with the adapter-owned host,
  advertising, store, and shutdown lifecycle;
* use a project-owned report map with keyboard input as report ID 1 and
  consumer-control input as report ID 2;
* expose standard HID Information, Report Map, Protocol Mode, Control Point,
  and report characteristics required for keyboard interoperability;
* include the HID service UUID and keyboard appearance in the existing
  adapter-owned advertisement;
* do not let ESP-IDF HID helpers start advertising, initialize another host, or
  hide automatic retries;
* copy HID start, subscription, protocol, and failure callbacks into bounded
  owned events for Service polling;
* translate the hardware-neutral reports only inside the adapter;
* ignore keyboard LED output safely until a later feature defines a consumer;
* do not expose or log output reports;
* do not advertise a fabricated battery service or fixed battery value;
* deinitialize the HID device before the NimBLE host stops and preserve failed
  teardown ownership for a later retry;
* retain the controller, GAP, ATT, SMP, HID, and store log caps required to
  prevent identity and report disclosure.

If the pinned `esp_hid` helper cannot preserve the existing lifecycle and
advertising ownership, use the pinned NimBLE HID service directly behind the
same adapter contract. Do not change the public contract or introduce a new
dependency to work around helper ownership.

## 5. Granular TDD Implementation Sequence

1. Add failing tests for valid keyboard, neutral keyboard, consumer press, and
   consumer release values.
2. Add duplicate-key, invalid-usage, and adapter-not-called validation tests;
   implement the shared report validation.
3. Add tests proving construction and an ordinary Bluetooth connection do not
   make HID ready.
4. Add encrypted, authenticated, selected, and fully subscribed readiness
   tests; implement the minimum readiness state.
5. Add keyboard, modifier chord, six-key, consumer press/release, and report
   ownership tests.
6. Add not-ready, busy, adapter-failure, and identity/report-free log tests.
7. Add `releaseAll` tests for both report types, partial failure, and definitive
   disconnect.
8. Add selection change, unexpected disconnect, explicit disable, and pairing
   transition tests proving no stale reports survive.
9. Add stale lifecycle, wrong-peer subscription, registration failure,
   teardown failure, and clean re-enable regression tests.
10. Refactor common HID validation and Bluetooth readiness helpers while the
    focused suites remain green.
11. Add the thin ESP-NimBLE HID registration, report-map, callback, and send
    integration. Framework glue is a thin-adapter TDD exception verified by
    strict firmware compilation and physical validation.
12. Audit dependency logging and confirm no HID payload or host identity enters
    project or framework diagnostics.
13. Update the Bluetooth, HID action, hardware-abstraction, and Phase 2
    architecture sections. Update README without claiming host-control actions
    are available in normal firmware.
14. Complete this plan with TDD evidence, dependency decisions, check results,
    size changes, and physical findings.

## 6. Test and Verification Plan

Run and record:

```text
uv run --frozen pio test -e native -f test_hid_transport
uv run --frozen pio test -e native -f test_bluetooth_service
make format
make format-check
make lint
make test
make build
make check
```

Mandatory Cardputer-Adv validation must:

* pair through plan 014 and reconnect after reboot;
* verify printable keyboard usages, Shift/Ctrl/Alt/GUI chords, six simultaneous
  usages, and release behavior in a host-side key viewer;
* verify supported consumer usages such as volume and media controls;
* disconnect during a held modifier and confirm the host has no stuck state;
* reconnect and confirm the interrupted report is not replayed;
* exercise repeated subscription, disconnect, disable, and re-enable cycles;
* run Wi-Fi traffic during BLE HID input and confirm coexistence;
* audit the full log for reports, keys, bond references, and peer identity.

## 7. Acceptance Criteria and Assumptions

The change is complete only when:

* higher layers can express keyboard and consumer reports without BLE or ESP32
  types;
* BLE output is accepted only for the authenticated, selected, subscribed peer;
* report delivery and release remain non-blocking and testable on a host;
* disconnect, target change, disable, and failure cannot leave reusable stale
  report state;
* the real adapter compiles under strict Cardputer-Adv warnings and works with
  a physical host;
* every automated and mandatory physical check passes.

Assumptions and defaults:

* plan 014 merges first;
* keyboard and consumer control are the complete Phase 2 HID report scope;
* the BLE host remains a single ESP-NimBLE peripheral lifecycle;
* normal firmware still does not route keyboard input or logical Actions to a
  host;
* native USB implements the same contract in plan 016.
