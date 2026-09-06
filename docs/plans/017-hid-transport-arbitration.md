# HID Transport Arbitration Plan

Status: **Planned**

This plan describes the seventh and final granular Phase 2 change. It adds the
hardware-independent USB-first router that delivers one complete HID
transaction to exactly one transport, retains the selected BLE target, and
neutralizes in-flight key state during handover.

Implementation should use:

```text
branch:   feat/017-hid-transport-arbitration
PR title: [017] Add USB-first HID transport routing
```

The implementation branch must begin from updated `main` after plan 016 is
merged.

## 1. Summary

Add:

* an owned, bounded HID transaction model for resolved host-control output;
* a hardware-independent `HidTransportRouter` using fakeable BLE and USB
  transports;
* mounted-and-ready USB precedence over the selected BLE connection;
* cancellation, release, and duplicate-suppression rules for every handover;
* selected-BLE-target retention and unavailable-target reporting;
* native behavioral and integration tests plus mandatory hotplug validation.

The router accepts resolved HID reports, not `Action` identifiers. Phase 7 owns
Action mapping and transaction creation. Phase 3 supplies the active bond
reference, and Phase 6 exposes host selection in Device Manager.

## 2. Public Contract and Behavior

Add under `src/connectivity/hid/`:

* `HidTransportKind`: `None`, `Ble`, or `Usb`;
* `HidTransactionFrame`: one `HidReport` plus a non-negative dwell duration;
* `HidTransaction`: an owned sequence of 1-16 frames;
* `HidRouteResult`: `Accepted`, `Busy`, `NoReadyTransport`,
  `InvalidTransaction`, or `TransportError`;
* `HidRouterState`: `Idle`, `Dispatching`, `Handover`, `Unavailable`, or
  `Error`;
* `HidTransportRouter`: `setBleTarget`, `route`, `cancel`, `update`, `state`,
  `activeTransport`, and `activeTransactionTransport`;
* a BLE-target control interface implemented by `BluetoothService` so the
  router can supply an opaque `BluetoothBondReference` without learning a host
  name, platform, or profile.

Transaction rules:

* each transaction owns its reports and is valid only with 1-16 frames;
* dwell duration is interpreted using elapsed monotonic time supplied to
  `update`; the router never sleeps;
* the final frame must be neutral for every report kind activated earlier in
  the transaction;
* invalid or non-neutral-ending transactions are rejected before either
  transport is called;
* only one transaction may be active; another submission returns `Busy`;
* `Accepted` means the transaction is bound to one transport, not that every
  frame has already been sent;
* report contents and transaction frames must never be logged.

## 3. Selection and Routing Policy

The router applies this precedence whenever it is idle:

```text
USB mounted, resumed, and endpoint-ready  -> USB
otherwise selected BLE bond is HID-ready -> BLE
otherwise                                -> None
```

Required behavior:

* physical cable power, USB initialization, or mounted-but-suspended state is
  insufficient for USB ownership;
* USB owns all new output as soon as it is genuinely ready;
* the selected BLE bond remains selected and may remain connected while USB
  owns output;
* USB ownership never clears, replaces, or mutates the BLE selection;
* after USB loss or suspension, new output returns only to the selected BLE
  bond when it is ready;
* the router never selects another persisted bond as fallback;
* no selected bond, a missing bond, or an unavailable selected host yields
  `NoReadyTransport` without sending elsewhere;
* transport changes expose passive state for a later status indicator and do
  not navigate, display a popup, or alter local UI input;
* wake-only input remains consumed before transaction creation, so the router
  receives no call for that input.

## 4. Transaction and Handover Safety

Bind a transaction to the transport selected when `route` accepts it.

While a transaction is active:

* send every frame only to its bound transport;
* never retry a frame on the other transport;
* an ambiguous send failure cancels the transaction because the old host may
  already have received that frame;
* completion returns to idle selection without emitting an extra report;
* explicit cancellation sends neutral reports to the bound transport before
  completing when the link remains usable.

When USB becomes ready during a BLE transaction:

1. stop advancing the transaction;
2. enter `Handover`;
3. request `releaseAll` from BLE;
4. retry a `Busy` release through later `update` calls without blocking;
5. cancel the old transaction after release succeeds;
6. select USB for future transactions;
7. never replay the cancelled transaction over USB.

When USB becomes unavailable during a USB transaction:

* a confirmed unmount neutralizes the old USB device and cancels the
  transaction immediately;
* suspension without unmount requires release after resume or an eventual
  confirmed unmount before handover is considered safe;
* the interrupted transaction is never replayed over BLE;
* only a future transaction may use the selected BLE connection.

For any still-connected bound transport:

* `Busy` release keeps the router in `Handover` and rejects new transactions;
* adapter release failure enters `Error` and blocks all output;
* explicit `cancel` or a later update may retry owned release cleanup;
* the router returns to service only after release succeeds or physical
  disconnection definitively neutralizes that transport.

This conservative policy favors duplicate and stuck-key prevention over
silently finishing an uncertain command on another host.

## 5. Failure Isolation and Composition

The router owns non-owning references to the USB and BLE transport contracts;
both injected objects must outlive it.

Composition rules:

* the router contains no ESP32, TinyUSB, NimBLE, display, keyboard, or
  `HostProfile` types;
* a USB error does not disable Bluetooth or clear its selected bond;
* a Bluetooth error does not disable USB or the CDC console;
* a transport error may make routing unavailable but must not fail the System
  Core update loop;
* a future composition owner supplies elapsed time and the active bond selected
  by `HostService`;
* Phase 2 may compose the router with fake targets in an unshipped validation
  harness, but normal runtime provides no Action mappings or keyboard-to-host
  routing;
* no automatic reconnect or hidden retry is added outside the existing
  Bluetooth and USB owners.

## 6. Granular TDD Implementation Sequence

1. Add failing transaction validation tests for empty, oversized, owned-copy,
   valid neutral-ending, and invalid non-neutral-ending sequences.
2. Add idle-selection tests for USB-ready precedence, BLE fallback, no target,
   unavailable target, powered-only USB, and suspended USB.
3. Add tests proving one accepted transaction is sent only to USB when both
   transports are ready.
4. Add equivalent BLE-only delivery and selected-bond forwarding tests.
5. Add one-active-transaction, dwell-boundary, completion, and busy-submission
   tests; implement non-blocking frame progression.
6. Add BLE-to-USB mid-modifier handover tests, then implement release-before-
   switch without replay.
7. Add USB-unmount mid-transaction tests proving disconnect neutralization and
   no BLE replay.
8. Add USB-suspend, resume-and-release, and suspend-then-unmount tests.
9. Add release-busy retry and new-transaction rejection tests for handover.
10. Add release-failure, ambiguous-send-failure, explicit cleanup retry, and
    recovery tests.
11. Add selected-BLE-target replacement during USB ownership and after USB loss
    tests, proving no different bond is selected implicitly.
12. Add integration tests using fake BluetoothService, fake native USB
    transport, representative keyboard chords, and consumer-control reports.
13. Add isolation tests proving USB failure leaves BLE state intact, Bluetooth
    failure leaves USB/CDC intact, and router error leaves System Core running.
14. Refactor selection, active-transaction, and release-cleanup state while all
    focused suites remain green.
15. Add the final thin composition required by the physical validation harness;
    do not add production Action mappings.
16. Update the HID, Connectivity, active-host boundary, failure-isolation, and
    Phase 2 architecture sections plus README and current-feature manuals.
17. Mark Phase 2 complete only after every prior plan and the final automated
    and physical acceptance gates pass.
18. Complete this plan with RED/GREEN evidence, exact checks, firmware size,
    soak results, and physical handover findings.

## 7. Test and Verification Plan

Run and record:

```text
uv run --frozen pio test -e native -f test_hid_transport
uv run --frozen pio test -e native -f test_hid_transport_router
uv run --frozen pio test -e native -f test_bluetooth_service
uv run --frozen pio test -e native -f test_native_usb_hid
make format
make format-check
make lint
make test
make build
make check
```

Mandatory Cardputer-Adv validation must:

* pair and select a BLE host, verify BLE HID, then attach USB and verify that
  only USB receives later transactions;
* attach USB while a BLE modifier or consumer control is active and confirm the
  BLE host receives release without receiving the transaction over USB;
* remove USB during an active USB transaction and confirm no duplicate appears
  over BLE and neither host retains stuck input;
* exercise USB suspend/resume and suspend/unmount behavior;
* confirm cable power without successful enumeration never takes ownership;
* confirm an unavailable selected BLE host does not redirect output to a
  different bonded peer;
* repeat attachment, removal, BLE disconnect, reconnect, suspend, and resume
  cycles while watching for stale callbacks or monotonic heap loss;
* confirm CDC monitoring remains usable during routing and handover;
* run Wi-Fi traffic, BLE HID, and USB CDC/HID together for at least one hour;
* record minimum free heap and confirm no watchdog, reset, resource exhaustion,
  duplicate action, stuck key, or identity-bearing log.

After the soak, repeat the production display, keyboard, microSD, serial, and
IR smoke checks required by the engineering guidelines.

## 8. Acceptance Criteria and Assumptions

The change is complete only when:

* one logical HID transaction is bound to exactly one transport;
* mounted-and-ready USB has exclusive precedence for new transactions;
* removal or suspension falls back only to the selected BLE bond;
* every handover cancels rather than replays in-flight output and neutralizes
  all reusable pressed state;
* uncertain delivery never triggers duplicate failover;
* arbitration is hardware-independent and covered with fake transports;
* transport failures remain isolated from each other and System Core;
* all plans 010-017, automated checks, and mandatory physical validation are
  complete before documentation calls Phase 2 complete.

Assumptions and defaults:

* plan 016 merges first;
* transaction production from logical Actions belongs to Phase 7;
* active-host selection and persistence belong to Phase 3;
* Device Manager controls belong to Phase 6;
* local UI and wake-only input remain upstream from HID routing;
* normal firmware exposes the completed infrastructure but no host-control
  shortcut until the owning later phases are implemented.
