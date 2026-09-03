# Bluetooth Lifecycle Foundation Plan

Status: **Complete**

This plan describes the second granular Phase 2 change. It establishes a
testable BLE peripheral lifecycle and reconnect behavior without yet exposing
pairing or HID reports.

Implementation should use:

```text
branch:   feat/011-bluetooth-lifecycle-foundation
PR title: [011] Establish Bluetooth lifecycle foundation
```

The implementation branch must begin from an updated `main` after plan 010 is
merged.

## 1. Summary

Add:

* a hardware-independent `BluetoothService` and `IBluetoothAdapter`;
* explicit enable, disable, advertising, connection, and failure states;
* adapter-event polling that isolates asynchronous ESP callbacks;
* single-host connection enforcement and reconnect advertising;
* a direct ESP-IDF Bluedroid BLE peripheral adapter;
* native behavioral tests and architecture documentation.

Bluetooth remains inactive during normal boot. Authenticated pairing and bond
management are added by plan 012, and BLE HID reports are added by plan 013.

## 2. Public Contract and Behavior

Add the hardware-independent API under `src/connectivity/bluetooth/`:

* `BluetoothDeviceConfig`: owned device name, with `Cardputer Hub` used by
  diagnostic validation until persistent configuration exists;
* `BluetoothState`: `Disabled`, `Idle`, `Advertising`, `Connected`,
  `RetryWaiting`, or `Error`;
* `BluetoothEvent`: advertising-started, advertising-failed with retryable or
  fatal classification, peer-connected, peer-disconnected, or adapter-failed;
* `BluetoothAdapterResult`: `Success` or `AdapterError` for ordinary checked
  adapter operations;
* `BluetoothAdvertisingResult`: `Started`, `RetryableFailure`, or
  `AdapterError` for an advertising launch;
* `BluetoothPollResult`: an owned event, no event, or adapter error;
* `BluetoothBondQueryResult`: `Bonded`, `Unbonded`, or `AdapterError`;
* `BluetoothEnableResult`: `Enabled`, `AlreadyEnabled`, or `AdapterError`;
* `BluetoothDisableResult`: `Disabled`, `AlreadyDisabled`, or `AdapterError`;
* `IBluetoothAdapter`: checked initialize, advertising start and stop-request,
  peer disconnect, and definitive shutdown operations; checked polling of owned
  events; and a checked query identifying whether a peer already has a bond;
* `BluetoothService`: `enable`, `disable`, `update(elapsed)`, `state`, and
  read-only current-connection access.

Contract rules:

* construction has no hardware side effects;
* `enable` initializes the adapter once per enabled lifecycle and begins
  advertising without blocking; a failed, rolled-back initialization or a
  completed disable may be initialized again only by a later explicit
  `enable`;
* repeated enable and disable calls are idempotent and return their explicit
  outcome;
* ESP callback contexts enqueue events only; Service state changes occur when
  `update(elapsed)` drains them;
* only one host connection is supported at a time; additional peers are
  disconnected without replacing the active connection;
* until plan 012 opens a pairing window, unbonded peers are rejected;
* explicit disable clears reconnect and advertising-retry intent, checks every
  required stop request and disconnect request, then uses adapter shutdown as a
  synchronous completion barrier before returning `Disabled`; shutdown closes
  adapter-known peers even when their connection events remain queued, and any
  request or shutdown failure returns `AdapterError` and enters `Error` rather
  than reporting false successful cleanup;
* an unexpected disconnection waits one second and then restarts advertising so
  a bonded host can reconnect;
* failed advertising retries after 1, 2, 4, 8, 16, then 30 seconds and remains
  capped at 30 seconds only when the adapter classifies the launch or event
  failure as retryable;
* successful advertising or connection resets advertising backoff;
* the Service exclusively owns advertising and reconnection retries; the
  adapter and ESP-IDF configuration do not perform hidden automatic retries;
* initialization or fatal advertising-launch failure rolls back adapter state,
  clears pending enable and advertising intent, enters `Error`, and is not
  retried implicitly; a later explicit `enable` starts a clean attempt;
* failed shutdown retains Service and adapter cleanup ownership; later disable
  retries shutdown, while later enable must complete cleanup before attempting
  initialization;
* failed rejection of an unbonded or additional peer is a fatal adapter error,
  because the single-connection policy can no longer be guaranteed;
* an event-queue overflow or polling failure enters `Error` rather than
  silently dropping lifecycle state;
* every asynchronous advertising operation retains its issuing lifecycle
  generation, and stale events from a rolled-back or disabled lifecycle cannot
  be relabeled as or reactivate a later lifecycle;
* connection events expose only an opaque adapter peer handle at this stage;
* logs contain state transitions but never peer addresses or identity data;
* the Service and adapter are synchronous and single-threaded from the
  Service caller's perspective;
* injected adapters and optional loggers are non-owning and must outlive the
  Service.

Add the ESP32 BLE adapter under `src/hardware/esp32/bluetooth/`:

* use the direct ESP-IDF Bluetooth controller, Bluedroid, GAP, and GATTS APIs
  bundled with the pinned framework; do not use an Arduino BLE facade;
* operate as a BLE peripheral only; do not enable Classic Bluetooth or central
  scanning;
* configure the injected device name and standard general-discoverable flags;
* exclusively own controller/Bluedroid initialization and GAP/GATTS callback
  registration, rejecting incompatible pre-initialized resources rather than
  adopting state with unknown ownership;
* initialize transactionally in checked stages: controller initialization and
  enable, Bluedroid initialization and enable, callback registration, and
  advertising configuration; quiesce completed stages in reverse order after a
  partial failure;
* never deinitialize the controller during a logical disable or rollback: the
  pinned ESP-IDF cannot initialize it again afterward; retain its allocation
  and exclusive ownership for the adapter process lifetime, disable it while
  Bluetooth is inactive, and initialize a fresh Bluedroid host on re-enable;
* clear a teardown stage or ownership marker only after that stage succeeds, so
  repeated shutdown can retry a partial failure;
* use checked ESP-IDF primitives rather than convenience APIs that abort on
  recoverable initialization or configuration failures;
* copy callback payloads into bounded, owned lifecycle events; callback code
  must never retain ESP-IDF-owned pointers or mutate Service state;
* latch queue overflow as a fatal condition surfaced by the next poll;
* contain all ESP Bluetooth types, callback classes, and peer handles;
* configure no framework-owned automatic reconnect or advertising retry;
* audit the actual pinned ESP-IDF/Bluedroid logging paths before activation and
  apply compile-time or runtime tag caps where necessary to prevent identity
  disclosure;
* do not add a third-party BLE dependency.

## 3. Granular TDD Implementation Sequence

1. Add `test/test_bluetooth_service/test_main.cpp` with a failing construction
   test proving that the Service must not touch the adapter.
2. Add enable/disable tests and implement explicit results, idempotent
   initialization, advertising, disconnection, and shutdown intent.
3. Add event-queue tests, then implement state transitions only through
   `update(elapsed)`.
4. Add tests for a bonded peer connecting, remaining active, and disconnecting;
   implement the single-connection state.
5. Add tests proving that unbonded and additional peers are rejected without
   disturbing the active peer.
6. Add unexpected-disconnection tests and implement the one-second reconnect
   delay.
7. Add advertising-failure tests at each backoff boundary and implement capped
   retry only for explicitly retryable failures plus reset behavior.
8. Add failing regression tests for initialization and advertising-launch
   failure cleanup, retry after a later explicit enable, stop/disconnect error
   propagation, stale events, and queue overflow; implement the stable `Error`
   and clean-recovery behavior.
9. Refactor the event and state helpers while keeping the focused suite green.
10. Add the thin direct ESP-IDF adapter with staged rollback and explicit
    ownership checks. Direct ESP calls and callback glue are a thin-adapter TDD
    exception verified by Cardputer-Adv compilation; Service behavior is not.
11. Add identity-free lifecycle logging, audit the pinned dependency logging
    paths, and verify log records contain no peer data.
12. Update the Bluetooth, failure-isolation, hardware-abstraction, and Phase 2
    architecture sections. Update README phase status without changing the
    device manual's runtime claims.
13. Complete the plan record with TDD and verification evidence.

## 4. Test and Verification Plan

Native scenarios must cover:

* side-effect-free construction;
* first and repeated enable/disable calls;
* event delivery only during Service update;
* valid bonded connection and unexpected disconnection;
* rejection of unbonded and second peers;
* reconnect delay and advertising retry boundaries;
* backoff reset after successful advertising and connection;
* retryable versus fatal advertising failures;
* partial-initialization rollback and a clean later explicit enable;
* failed advertising launch clearing unstarted intent without an unnecessary
  stop call;
* stop, disconnect, polling, rejection-disconnect, and adapter event failures;
* stale callbacks after disable or rollback and fatal queue overflow;
* disable while advertising, waiting, or connected;
* identity-free logs;
* Bluetooth failures leaving Wi-Fi and System Core behavior untouched.

Run and record:

```text
uv run --frozen pio test -e native -f test_bluetooth_service
make format
make format-check
make lint
make test
make build
make check
```

Optional hardware validation may use a temporary, unshipped harness to confirm
advertising and reconnect advertising. It must not create or erase user bonds.

## 5. Acceptance Criteria and Assumptions

The change is complete only when:

* higher layers can control BLE lifecycle without ESP32 types;
* asynchronous callbacks cannot mutate Service state directly;
* lifecycle, single-connection, rejection, and retry behavior have native
  coverage;
* no failed operation is reported as success; partial initialization is
  quiesced when possible, and failed cleanup remains explicitly owned until it
  succeeds;
* Bluetooth initialization and updates never block boot indefinitely;
* the real adapter compiles under strict Cardputer-Adv warnings;
* no pairing, bond mutation, HID action, or runtime activation is introduced;
* all applicable checks pass.

Assumptions and defaults:

* plan 010 is merged first;
* BLE is peripheral-only and initially supports one live host connection;
* multiple persisted bonds remain a later-plan requirement even though only one
  connection may be active;
* paired hosts initiate reconnection after the device resumes advertising;
* elapsed time is injected by the eventual composition owner;
* direct ESP-IDF Bluedroid/GAP/GATTS is the selected Bluetooth API layer;
* normal `main.cpp` and `SystemRuntime` behavior remain unchanged.

## 6. Completion Record

Completed on 2026-09-03.

TDD evidence:

* RED: the first construction test failed because
  `connectivity/bluetooth/bluetooth_service.h` did not exist; the minimal public
  contract and side-effect-free constructor then made it pass.
* RED: the enable/disable slice failed at link time because lifecycle methods
  were not implemented; explicit results, idempotent initialization,
  advertising intent, and checked cleanup then made eight focused tests pass.
* Event delivery, bonded single-peer behavior, peer rejection, reconnect
  timing, lifecycle-generation isolation, retry boundaries, fatal cleanup, and
  identity-free logging were added incrementally. The final focused native
  suite contains 35 passing behavioral tests, including review regressions for
  queued-peer shutdown, asynchronous stop completion, retained cleanup
  ownership, shutdown retry, polling isolation after fatal errors, and
  Bluetooth failure isolation from Wi-Fi and System Core. Peer-rejection
  regressions also prove that advertising remains blocked until every rejected
  connection has reported its asynchronous disconnection.
* RED: cleanup-ownership regressions showed that a second `disable` made no
  shutdown call after an initial teardown failure and that `enable` initialized
  directly over that failure. Retaining cleanup intent until a successful
  shutdown made both tests pass.
* The missing failure-isolation scenario was a coverage omission rather than a
  production defect: a native integration test now proves Wi-Fi can connect and
  System Core can update after Bluetooth enters `Error`.
* The direct ESP-IDF adapter was treated as the documented thin-adapter TDD
  exception. Its first Cardputer build exposed a missing standard include and a
  pinned-SDK name-length macro mismatch; both were corrected before the build
  passed.

Delivered behavior:

* `BluetoothService` and `IBluetoothAdapter` provide checked, hardware-neutral
  lifecycle, polling, bond-query, single-connection, shutdown, and opaque-peer
  contracts.
* Service-owned retry policy implements the one-second reconnect delay and
  retryable advertising delays of 1, 2, 4, 8, 16, and capped 30 seconds. The
  concrete adapter classifies Bluedroid's generic `ESP_FAIL` dispatch result as
  transient while retaining fatal classification for specific invalid argument
  and state errors. A transient dispatch failure after deferred advertising-data
  configuration is surfaced as a retryable event so it follows the same Service
  backoff path.
* Fatal paths shut down adapter-owned resources, clear Service intent, and
  require a later explicit enable. Lifecycle generations prevent stale events
  from a failed or disabled attempt from affecting a later attempt.
* Normal disable also shuts down the adapter as the definitive completion
  barrier after stop and disconnect requests. This closes adapter-known peers
  whose callbacks have not yet reached the Service and prevents an accepted
  asynchronous stop request from being mistaken for completed cleanup.
* `Esp32BluetoothAdapter` directly owns the ESP-IDF BLE controller, Bluedroid,
  GAP, and GATTS lifecycle. It uses a bounded callback queue, checked lifecycle
  quiescence, internal peer-address storage for bond lookup, and no Arduino BLE
  facade or third-party BLE dependency. The controller stays initialized but
  disabled between logical lifecycles because the pinned ESP-IDF makes
  controller deinitialization terminal until reboot. Advertising callbacks
  preserve their issuing generation, permanent ESP statuses are fatal, and all
  audited identity-bearing stack log tags, including `BT_GATT`, `BT_APPL`,
  `BT_L2CAP`, and `BT_BTIF`, are disabled. In particular, the pinned `BT_BTM`,
  `BT_L2CAP`, and bond-storage implementations contain identity-bearing Warning
  records, so a Warning cap was rejected as insufficient and the sensitive tags
  use `ESP_LOG_NONE`.
* Bluetooth remains unconstructed during normal runtime. Pairing, bond
  mutation, HID reports, and user-visible behavior were not introduced, so the
  device manual required no behavior change.

Verification:

```text
make format                                      PASS
make format-check                                PASS
make lint                                        PASS (no defects)
uv run --frozen pio test -e native \
  -f test_bluetooth_service                      PASS (35 tests)
make test                                        PASS (17 Python + 134 native tests)
make build                                       PASS
make check                                       PASS
```

Optional physical Cardputer advertising and reconnect validation was not run.
No user bonds were created or erased.
