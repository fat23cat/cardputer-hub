# Wi-Fi Connectivity Foundation Plan

Status: **Complete**

This plan describes the first granular Phase 2 change after completion of the
System Core foundations. It introduces testable Wi-Fi connectivity without
adding credential persistence, provisioning UI, or automatic startup behavior.

Implementation should use:

```text
branch:   feat/010-wifi-connectivity-foundation
PR title: [010] Establish Wi-Fi connectivity foundation
```

The implementation branch must begin from an updated `main` after plan 009 is
merged.

## 1. Summary

Add:

* a hardware-independent `WiFiService` and `IWifiAdapter`;
* explicit connection state and result types;
* non-blocking connect, disconnect, signal-strength, and reconnection behavior;
* an ESP32 station-mode adapter using the pinned Arduino framework;
* native behavioral tests with an injected fake adapter;
* architecture documentation for the stable Wi-Fi contract.

Wi-Fi remains inactive in normal firmware. Credentials are supplied in memory
to the Service, are never compiled into the firmware, and are not persisted
until the Phase 3 `ConfigurationService` owns their schema and storage policy.

Deferred work includes network scanning, captive portals, access-point mode,
enterprise Wi-Fi, HTTP/DNS facades, provisioning UI, and automatic startup.

## 2. Public Contract and Behavior

Add the hardware-independent API under `src/connectivity/wifi/`:

* `WifiNetworkConfig`: owned SSID and passphrase;
* `WifiState`: `Idle`, `Connecting`, `Connected`, `RetryWaiting`, or `Error`;
* `WifiConnectResult`: `Started`, `InvalidConfig`, or `AdapterError`;
* `WifiDisconnectResult`: `Disconnected` or `AdapterError`;
* `IWifiAdapter`: station initialization, connection, disconnection, link-state,
  and RSSI operations;
* `WiFiService`: `connect`, `disconnect`, `update(elapsed)`, `state`, and
  connected-only signal-strength access.

Contract rules:

* an SSID contains 1-32 bytes and no embedded NUL;
* the passphrase is empty for an open network, contains 8-63 bytes for a
  personal WPA passphrase, or is exactly 64 hexadecimal digits for a raw PSK;
  embedded NUL is rejected;
* invalid configuration never reaches the adapter or replaces active intent;
* a new valid connection request replaces the previous target and starts an
  immediate attempt;
* an explicit disconnect cancels connection and retry intent and clears its
  in-memory credentials; it returns `Disconnected` and enters `Idle` on
  success, or returns `AdapterError` and enters `Error` when the hardware
  operation fails;
* `update(elapsed)` is non-blocking and receives elapsed monotonic time from its
  eventual composition owner, avoiding dependence on Arduino clocks;
* an attempt that remains incomplete for 15 seconds is disconnected and
  scheduled for retry;
* retries wait 1, 2, 4, 8, 16, and then 30 seconds, remain capped at 30 seconds,
  and reset after a successful connection or replacement request;
* loss of an intended connection enters `RetryWaiting` rather than silently
  abandoning the target;
* RSSI is exposed only while connected;
* `Error` represents initialization or other non-retryable adapter failure;
  ordinary connection loss continues through the retry state machine;
* initialization or connection-launch failure clears unstarted intent and
  credentials so a later request can retry without a replacement disconnect;
* calls are synchronous and single-threaded, and the adapter must not retain
  references passed by the caller;
* the Service holds a non-owning adapter reference, so the adapter must outlive
  it;
* logs may describe state and retry outcomes but never the SSID, passphrase,
  BSSID, IP address, or traffic contents.

Add an ESP32 Wi-Fi adapter under `src/hardware/esp32/wifi/`:

* use the ESP-IDF Wi-Fi APIs bundled with the pinned ESP32 Arduino framework;
* configure station mode only and never start an access point;
* exclusively own driver initialization, select RAM-backed credential storage,
  roll back partial initialization, and avoid independent framework
  reconnection behavior;
* begin connection attempts without waiting for association or DHCP;
* bounds-check configuration before copying it into fixed driver buffers;
* propagate driver connect and disconnect failures and translate association,
  IPv4 readiness, and RSSI into hardware-independent values;
* contain all Arduino, ESP32, and Wi-Fi-library headers inside the adapter.

## 3. Granular TDD Implementation Sequence

Follow RED-GREEN-REFACTOR for each behavior and record the observed failures in
the completion record.

1. Include `src/connectivity/` in the native source filter and add
   `test/test_wifi_service/test_main.cpp`; confirm the initial test fails because
   the Wi-Fi contract does not exist.
2. Add configuration-boundary tests, then implement SSID/passphrase validation
   and invalid-request short-circuiting.
3. Add tests for an immediate first connection, replacement of active intent,
   and explicit disconnect; implement only those transitions.
4. Add adapter-status tests for successful connection, connection loss, and
   non-retryable initialization failure; implement state polling.
5. Add tests at every 15-second attempt-timeout boundary and every retry-delay
   boundary; implement the capped exponential schedule.
6. Add tests proving a successful connection and a replacement request reset
   backoff to one second.
7. Add RSSI tests for connected, disconnected, and adapter-error conditions;
   implement connected-only forwarding.
8. Refactor state-machine helpers only after the focused suite remains green.
9. Add the thin ESP32 adapter. Treat direct framework interaction as a
   thin-adapter TDD exception and verify it through strict Cardputer-Adv
   compilation.
10. Add credential-free state logging using the existing injected logger where
    useful for diagnostics.
11. Update the Wi-Fi, persistence, hardware-abstraction, and Phase 2 sections of
    `docs/ARCHITECTURE.md` with the delivered contract.
12. Update `README.md` to mark Phase 2 as in progress without claiming active
    connectivity. Audit `docs/manuals/device-guide.md` and retain its current
    Wi-Fi limitation because `main.cpp` remains unchanged.
13. Complete this plan with RED evidence, delivered behavior, compilation
    corrections, actual checks, and optional hardware-validation status.

## 4. Test and Verification Plan

Native scenarios must cover:

* empty, maximum-length, overlong, and embedded-NUL SSIDs;
* open-network, minimum/maximum WPA, valid and invalid 64-digit raw PSKs,
  overlong, and embedded-NUL passphrases;
* immediate connect, active-request replacement, and manual disconnect;
* connection success and unexpected loss;
* the attempt timeout and every retry interval without real waiting;
* indefinite retry at the 30-second cap;
* retry reset after success and replacement;
* RSSI visibility only while connected;
* invalid requests and state queries that must not reach the adapter;
* retryable connection failures versus fatal adapter errors;
* logs containing no credential or network identity data.

Run and record:

```text
uv run --frozen pio test -e native -f test_wifi_service
make format
make format-check
make lint
make test
make build
make check
```

Physical Wi-Fi validation is optional because the adapter is not composed into
normal firmware. If performed with a temporary local harness, use disposable
credentials, do not commit or log them, verify connect/loss/reconnect behavior,
and record the result in the completion record.

## 5. Acceptance Criteria and Assumptions

The change is complete only when:

* higher layers can use `WiFiService` without Arduino or ESP32 headers;
* connect, disconnect, state, RSSI, timeout, and retry behavior have native
  behavioral coverage;
* every update call is bounded and non-blocking;
* explicit disconnect prevents further retry and removes credentials from
  Service-owned state;
* the ESP32 adapter cannot persist credentials independently;
* Wi-Fi failure cannot affect System Core or Bluetooth code;
* normal firmware boot, display, keyboard, and controls remain unchanged;
* relevant architecture and status documentation is accurate;
* every applicable repository check passes.

Assumptions and defaults:

* plan 009 is merged before implementation begins;
* the existing C++17 and pinned toolchain remain unchanged;
* personal WPA and open networks are the initial configuration shapes;
* elapsed time is supplied by a later composition owner;
* `ConfigurationService` will own persistent credentials and defaults;
* network selection and editing require a later UI design;
* no external Wi-Fi library or compile-time secret is introduced.

## 6. Completion Record

### TDD Evidence

The focused native suite recorded these expected RED failures before the
corresponding hardware-independent behavior was implemented:

1. the initial contract test did not compile because
   `connectivity/wifi/wifi_service.h` did not exist;
2. the configuration-boundary tests failed to link because the Service
   constructors and `connect` operation did not exist;
3. connection-intent tests failed to link because `state`, `disconnect`, and
   `update` did not exist;
4. association, connection-loss, and fatal-status cases remained in
   `Connecting` until adapter polling was implemented;
5. the exact timeout and retry-boundary cases failed because no timed state
   transitions existed;
6. connected-only signal-strength tests failed to link until RSSI access was
   implemented;
7. disconnect-failure regression tests failed to compile until the adapter and
   Service contracts exposed operation results;
8. the link-loss-before-RSSI-poll regression failed to compile until adapter
   signal strength represented absence explicitly;
9. retry-after-launch-failure regressions observed an unnecessary disconnect
   until failed initialization, initial connection launch, and retry launch
   cleared unstarted intent.

Each RED result was followed by the minimum behavior and a green focused suite
before the next state-machine behavior was added. Additional green coverage
then verified state queries, fatal retry-start failures, and credential-free
logging. Logging was wired while implementing RSSI because the native compiler
correctly rejected the then-unused optional logger field under `-Werror`; the
confidentiality assertion was added immediately afterward.

### Delivered Behavior

The completed change provides:

* owned `WifiNetworkConfig`, explicit Service and adapter states/results, and a
  hardware-independent `IWifiAdapter` boundary;
* complete SSID, open-network, personal-WPA, raw-PSK, length, hexadecimal, and
  embedded-NUL validation before adapter calls;
* immediate non-blocking connection attempts, active-target replacement,
  explicit cancellation, and connected-only RSSI access;
* adapter-polled association, retryable connection failure and loss, fatal
  adapter errors, a 15-second attempt timeout, and capped 1/2/4/8/16/30-second
  retry scheduling without a clock or sleep dependency;
* retry reset after success or replacement, owned target data across retries,
  no retry or status polling after explicit disconnect, and clean recovery on
  a new request after initialization or connection-launch failure;
* fixed-message Wi-Fi diagnostics that contain no network identity or
  credential data, plus a build-time guard against sensitive Arduino framework
  Debug and Verbose diagnostics;
* `Esp32WifiAdapter`, which exclusively initializes the ESP-IDF Wi-Fi driver,
  rolls back partial initialization, selects station mode and RAM-backed
  configuration, starts one association per Service request, bounds-checks
  fixed-buffer copies, distinguishes ordinary non-association from fatal query
  errors, polls IPv4 readiness, propagates connect and disconnect failures, and
  returns no RSSI when the live access-point record has disappeared;
* native source-filter integration and architecture/status documentation for
  the stable contract and its deferred scope.

`WiFiService` and `Esp32WifiAdapter` are compiled but not constructed by
`main.cpp`. No credentials are compiled into or persisted by the firmware, no
network attempt starts during boot, and current display, keyboard, storage,
Action, registry, and navigation behavior is unchanged.

### ESP32 Compilation

The thin adapter passed strict Cardputer-Adv compilation against the ESP-IDF
Wi-Fi APIs bundled with Arduino-ESP32 2.0.16. Framework review showed that the
Arduino Wi-Fi facade has cached status edge cases, an unconditional first
reconnect that can also occur after an established link is lost, and
Debug/Verbose event logs containing SSIDs, BSSIDs, and assigned IP data. The
final adapter therefore does not initialize or link that facade. It owns the
ESP-IDF driver lifecycle directly, uses the driver's single-attempt connect
operation, polls live association and IPv4 state, and returns every driver
operation result needed by the Service state machine.

RAM storage is selected before station configuration, malformed direct adapter
input is rejected before fixed-buffer copies, and preexisting Wi-Fi station
interfaces or initialized drivers are rejected rather than inheriting unknown
event, persistence, or retry policy. Partial initialization failures deinitialize
the driver and destroy the adapter-created station interface, while access-point
query failures other than ordinary non-association surface as fatal adapter
errors. The Cardputer build caps Arduino core diagnostics at Info and the adapter
has a compile-time guard preventing a later Debug/Verbose build from silently
reintroducing sensitive framework output.

### Verification

The following checks passed:

```text
focused test_wifi_service suite    32 cases passed
make format                         passed
make format-check                   passed (also exercised by make check)
make lint                           native and Cardputer-Adv passed
make test                           17 Python and 99 native cases passed
make build                          Cardputer-Adv firmware compiled
make check                          lock and format checks passed
                                    native and Cardputer-Adv lint passed
                                    17 Python tests passed
                                    99 native cases passed
                                    Cardputer-Adv firmware build passed
```

The device guide and installation manual were audited and remain accurate:
Wi-Fi is not currently available to users because the new Service and adapter
have no runtime composition owner. Physical Wi-Fi validation was not performed;
no disposable credentials or temporary device harness were used.
