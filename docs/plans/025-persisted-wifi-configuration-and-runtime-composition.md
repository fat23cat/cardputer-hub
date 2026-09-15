# 025 — Persisted Wi-Fi Configuration and Runtime Composition

Status: **Complete**

## 1. Goal

Make the existing Wi-Fi connectivity foundation part of the normal firmware runtime and give it persistent configuration through the Services layer.

The firmware should be able to:

* persist one configured Wi-Fi station network;
* remember whether Wi-Fi is enabled;
* restore that connection intent after reboot;
* run the existing Wi-Fi connection/retry state machine in normal firmware;
* expose Wi-Fi configuration and runtime status through a Service-level domain boundary suitable for future System UI and Mini Apps.

The intended dependency is:

```text
Apps / future System UI
        ↓
   NetworkService
        ↓
ConfigurationService
        +
connectivity::WiFiService
        ↓
    IWifiAdapter
        ↓
 Esp32WifiAdapter
```

`connectivity::WiFiService` remains responsible for Wi-Fi station lifecycle and retry behavior.

The new Service-level boundary owns persisted user intent and presents domain-level state without exposing Connectivity implementation details to Apps.

---

## 2. Current Problem

The project already has a working Wi-Fi Connectivity foundation.

`connectivity::WiFiService` currently owns:

```text
station initialization
connect
disconnect
connection state
retry waiting
bounded retry backoff
attempt timeout
RSSI access
adapter failure handling
```

An ESP32 Wi-Fi adapter also exists.

However normal firmware does not currently compose or update this service.

As a result:

```text
Wi-Fi implementation exists
        ↓
but
        ↓
normal firmware never starts it
```

The current `SystemConfiguration` persists:

```text
HostConfiguration
sound volume
```

but has no Wi-Fi settings.

Normal firmware therefore cannot restore Wi-Fi connection intent after reboot.

Home currently displays the explicit OFFLINE placeholder because no Wi-Fi runtime is composed.

There is also an architectural boundary to preserve:

```text
Apps
    ↓
Services
    ↓
Connectivity
```

Apps are not allowed to depend directly on Connectivity headers.

Therefore future Settings or Launcher code should not solve the problem by directly consuming:

```cpp
connectivity::WiFiService
WifiState
WifiNetworkConfig
```

A Service-level Wi-Fi domain boundary should be established before UI is added.

---

## 3. Scope

Included:

* persistent configuration for one Wi-Fi network;
* persisted Wi-Fi enabled/disabled intent;
* configuration schema version 4;
* lossless loading of existing version 1, 2, and 3 configuration;
* shared Wi-Fi credential validation;
* Service-level Wi-Fi configuration and status boundary;
* startup restoration of Wi-Fi intent;
* normal-firmware composition of the existing Wi-Fi Connectivity service;
* normal-firmware Wi-Fi updates on every runtime loop;
* unit and integration tests;
* architecture and plan documentation updates.

Not included:

* Wi-Fi Settings UI;
* SSID scanning UI;
* keyboard/password-entry UI;
* Home Wi-Fi indicator changes;
* multiple remembered Wi-Fi networks;
* network priority;
* roaming policy;
* captive-portal support;
* enterprise Wi-Fi;
* Wi-Fi provisioning over Bluetooth;
* Web UI;
* companion configuration;
* remote configuration;
* NVS encryption changes;
* ActionBus redesign;
* Wi-Fi-specific Actions unless required by implementation constraints.

This plan establishes the runtime and persistence boundary only.

UI should be implemented separately in Phase 4.

---

## 4. Persistent Wi-Fi Configuration

Extend `SystemConfiguration` with a bounded Wi-Fi configuration model.

Example:

```cpp
struct WifiConfiguration {
    bool enabled = false;
    std::string ssid;
    std::string passphrase;
};

struct SystemConfiguration {
    HostConfiguration host;
    WifiConfiguration wifi;
    std::uint8_t soundVolume = 60;
};
```

The exact field names may differ slightly.

The persisted model represents user intent:

```text
enabled
    whether firmware should maintain a Wi-Fi connection

ssid
    configured station network

passphrase
    configured station credential
```

Only one network is supported by this plan.

Do not introduce:

```text
std::vector<WifiNetwork>
priority
lastConnected
preferredNetwork
automaticNetworkSelection
```

until multiple-network behavior is actually required.

---

## 5. Configuration Invariants

The persisted Wi-Fi configuration should have one explicit validity contract.

Valid examples:

```text
enabled = false
ssid = ""
passphrase = ""

enabled = false
ssid = "Home"
passphrase = "correct horse battery staple"

enabled = true
ssid = "Home"
passphrase = "correct horse battery staple"

enabled = true
ssid = "OpenNetwork"
passphrase = ""
```

Invalid examples:

```text
enabled = true
ssid = ""

ssid = ""
passphrase = "orphaned-secret"

SSID longer than supported station limit

invalid WPA/WPA2 passphrase length

embedded NUL in SSID or passphrase

invalid 64-character hexadecimal PSK
```

The contract should be:

```text
no configured network
    → disabled + empty SSID + empty passphrase

configured network
    → valid SSID + valid passphrase

enabled
    → configured network must exist
```

A configured network may remain stored while Wi-Fi is disabled.

This permits:

```text
disable Wi-Fi
        ↓
retain credentials
        ↓
enable later without re-entering them
```

---

## 6. Shared Credential Validation

Do not maintain two independent Wi-Fi validation implementations.

The existing Connectivity implementation already validates:

```text
SSID
passphrase
64-character hexadecimal PSK
```

Before this plan is complete, configuration persistence and runtime connection requests must rely on the same validation contract.

A small shared Connectivity-level helper is acceptable, for example:

```cpp
bool validWifiNetworkConfig(const WifiNetworkConfig& config) noexcept;
```

or an equivalent bounded validation function.

Then:

```text
ConfigurationService
        ↓
same validator
        ↑
connectivity::WiFiService
```

must agree on whether credentials are valid.

Do not copy the validation logic into `ConfigurationService`.

The Service layer may additionally enforce persisted-state invariants such as:

```text
enabled requires configured network
empty SSID requires empty passphrase
```

because those are persistence/domain rules rather than station credential rules.

---

## 7. Configuration Schema Version 4

This plan introduces the first configuration schema after version 3.

Define:

```cpp
constexpr std::uint8_t versionFour = 4;
```

Version 4 becomes the format written by `ConfigurationService::save()`.

The existing versions remain readable:

```text
v1
v2
v3
 ↓
in-memory SystemConfiguration
 ↓
v4 on next successful save
```

Do not eagerly rewrite storage merely because an older valid configuration was loaded.

Loading old configuration should remain lossless and side-effect free.

For v1/v2/v3 input, initialize Wi-Fi to:

```text
enabled = false
ssid = ""
passphrase = ""
```

All existing data must survive:

```text
host IDs
host names
bond references
active host
Bluetooth enabled intent
platform metadata
host capabilities
mapping-template references
sound volume
```

---

## 8. Version 4 Serialization Layout

Prefer extending the current binary layout rather than reordering existing fields.

The existing version-3 portion should remain in its current order.

Append the version-4 Wi-Fi fields after the existing host records:

```text
H U B H
version = 4

existing host/Bluetooth fields
sound volume
host records

wifi enabled
wifi SSID
wifi passphrase
```

Conceptually:

```text
[v3-compatible payload]
[Wi-Fi enabled byte]
[SSID length + bytes]
[passphrase length + bytes]
```

This minimizes parser churn and makes migration behavior easier to audit.

Update `maximumSerializedSize` to include the bounded Wi-Fi payload.

The size calculation must explicitly account for:

```text
enabled byte
SSID length prefix
maximum SSID bytes
passphrase length prefix
maximum passphrase bytes
```

No unbounded configuration strings are allowed.

---

## 9. Service-Level Wi-Fi Boundary

Introduce a small Service responsible for persistent Wi-Fi intent and domain-level Wi-Fi status.

Recommended name:

```text
NetworkService
```

The exact name may change, but avoid introducing a second class also named `WiFiService` because:

```text
services::WiFiService
connectivity::WiFiService
```

would make composition and tests unnecessarily ambiguous.

`NetworkService` should depend on:

```text
ConfigurationService
connectivity::WiFiService
optional Logger
```

Conceptually:

```cpp
class NetworkService {
  public:
    NetworkResult start();
    void update(std::chrono::milliseconds elapsed);

    NetworkResult configure(std::string ssid,
                            std::string passphrase);

    NetworkResult setEnabled(bool enabled);
    NetworkResult forget();

    WifiStatusSnapshot status() const;
};
```

Exact APIs may change during TDD.

The responsibilities are important:

```text
NetworkService
    persisted user intent
    configuration mutations
    startup restoration
    domain status mapping
    credential secrecy boundary

connectivity::WiFiService
    station lifecycle
    retries
    timeout
    adapter state
    RSSI
```

Do not move retry/backoff policy into `NetworkService`.

---

## 10. Wi-Fi Domain Status

Apps should not need to interpret raw Connectivity state.

Introduce a Service-level status such as:

```cpp
enum class WifiConnectionStatus : std::uint8_t {
    Off,
    Connecting,
    Connected,
    Error,
};
```

and a read-only snapshot such as:

```cpp
struct WifiStatusSnapshot {
    bool configured = false;
    bool enabled = false;

    WifiConnectionStatus connection =
        WifiConnectionStatus::Off;

    std::string ssid;

    std::optional<std::int32_t> signalStrengthDbm;

    NetworkResult lastResult =
        NetworkResult::Success;
};
```

The snapshot must not expose:

```text
passphrase
WifiNetworkConfig
WifiState
WifiAdapterState
retry index
retry delay
station initialization state
adapter internals
```

Recommended runtime mapping:

| Condition | Domain status |
| --- | --- |
| persisted Wi-Fi disabled | `Off` |
| enabled + Connectivity Connecting | `Connecting` |
| enabled + Connectivity RetryWaiting | `Connecting` |
| enabled + Connectivity Connected | `Connected` |
| enabled + Connectivity Error | `Error` |
| enabled + unexpected Idle state | `Connecting` or `Error`, according to the finalized startup contract |

The final mapping must be centralized inside the Service.

Future UI should only translate:

```text
WifiConnectionStatus
        ↓
presentation
```

not reconstruct connectivity semantics independently.

---

## 11. Configuration Operations

### Configure Network

`configure(ssid, passphrase)` should:

```text
validate credentials
        ↓
create candidate SystemConfiguration
        ↓
persist candidate
        ↓
publish new configuration
        ↓
if Wi-Fi enabled:
    reconnect using new credentials
```

A storage failure must leave both stored and published configuration unchanged.

No connection attempt should use credentials that failed to persist.

If persistence succeeds but the Wi-Fi adapter cannot start the new connection:

```text
stored user intent remains valid
runtime result reports ConnectivityError
```

The persisted configuration must not be rolled back merely because the radio is temporarily unavailable.

This matches the principle that durable user intent and transient transport readiness are separate states.

### Enable Wi-Fi

`setEnabled(true)` should reject the request if no valid network is configured.

Otherwise:

```text
persist enabled = true
        ↓
publish new intent
        ↓
request connection
```

If the connection request fails after persistence:

```text
enabled intent remains stored
status reports runtime error
```

A reboot may retry that persisted intent.

### Disable Wi-Fi

`setEnabled(false)` should:

```text
persist enabled = false
        ↓
publish new intent
        ↓
disconnect Connectivity service
```

Stored credentials remain available.

If adapter disconnect reports a runtime failure, disabled intent remains persisted.

### Forget Network

`forget()` should produce:

```text
enabled = false
ssid = ""
passphrase = ""
```

Persistence must succeed before the configuration is published.

After successful persistence, disconnect any current Wi-Fi session.

Credentials must not remain in the published configuration after a successful forget operation even if the physical disconnect later reports an error.

---

## 12. Startup Restoration

Normal startup should continue to load configuration before dependent Services are started.

Conceptually:

```text
ConfigurationService.ensureLoaded()
        ↓
NetworkService.start()
        ↓
inspect persisted Wi-Fi intent
```

Behavior:

```text
not configured
    → remain Off

configured + disabled
    → remain Off

configured + enabled
    → request Wi-Fi connection

invalid persisted configuration
    → ConfigurationService load fails
    → do not attempt Wi-Fi connection
```

`NetworkService::start()` must not duplicate Connectivity retry behavior.

It should issue the initial connection intent and let `connectivity::WiFiService` own subsequent retry and timeout behavior.

---

## 13. Normal Firmware Composition

Compose the existing ESP32 Wi-Fi implementation in `src/main.cpp`.

Conceptually:

```cpp
hardware::Esp32WifiAdapter wifiAdapter;

connectivity::WiFiService wifiConnectivity(
    wifiAdapter,
    logger);

services::NetworkService network(
    wifiConnectivity,
    configuration,
    &logger);
```

Exact constructors may differ.

Startup should include the Network Service after configuration has been loaded.

The runtime loop should update Wi-Fi independently of UI scheduling:

```cpp
const auto elapsed = ...;

runtime.update(elapsed);

hosts.update(elapsed);
network.update(elapsed);
battery.update(elapsed);
```

Wi-Fi must not depend on:

```text
Home visibility
20 ms UiScheduler cadence
display rendering
keyboard activity
current navigation route
```

A slow or hidden UI must not delay Wi-Fi retry or connection-state processing.

---

## 14. UI Boundary

Do not change Home or Settings behavior in this plan.

The current visible Wi-Fi placeholder may remain until the Phase 4 UI slice.

This plan prepares the API that the UI will consume later:

```text
ApplicationShell / Settings
        ↓
NetworkService::status()
```

Future UI must not include Connectivity headers to obtain Wi-Fi state.

Similarly, future Wi-Fi Settings should mutate configuration through the owning Service rather than doing:

```cpp
auto config = configuration.value();
config.wifi = ...;
configuration.save(config);
```

from Apps code.

`ConfigurationService` remains the atomic persistence mechanism.

`NetworkService` owns Wi-Fi-specific user intent.

---

## 15. Credential Security

The passphrase is secret application data.

It must never appear in:

```text
logs
error messages
status snapshots
Home
debug status text
test failure messages containing actual credentials
```

Generic logging is acceptable:

```text
"wifi configuration updated"
"wifi connection request failed"
"wifi disabled"
```

Logging is not acceptable:

```text
"connecting to Home with password hunter2"
```

The public status API may expose SSID because that is user-visible network identity.

It must not expose the passphrase.

The implementation may temporarily carry the passphrase through the configuration operation and Connectivity request because the station stack requires it.

No additional at-rest encryption mechanism is introduced by this plan.

Do not imply that persisted credentials are encrypted unless the actual storage implementation provides and verifies that property.

NVS encryption or a dedicated credential store should be a separate security plan if required.

---

## 16. Failure Semantics

Use an explicit Service-level result model.

Example:

```cpp
enum class NetworkResult : std::uint8_t {
    Success,
    InvalidInput,
    NotConfigured,
    StorageError,
    ConnectivityError,
};
```

The exact names may differ.

The distinctions should remain observable.

Examples:

```text
invalid SSID
    → InvalidInput

enable without configured network
    → NotConfigured

NVS write failure
    → StorageError

adapter refuses connection
    → ConnectivityError
```

Do not collapse all failures into:

```text
Error
false
Rejected
```

when the caller needs to distinguish persistence, input, and runtime failures.

A transient Wi-Fi failure must not:

```text
disable Bluetooth
change selected host
alter host configuration
alter sound volume
block SystemRuntime
prevent normal UI operation
```

Wi-Fi remains an optional subsystem.

---

## 17. Testing

Follow the repository TDD workflow for behavioral production changes.

### Configuration Tests

Add tests for:

```text
default v4 configuration
v4 round-trip
configured open network
configured protected network
configured-but-disabled network
enabled network
maximum-length SSID
maximum valid passphrase
valid 64-character PSK
invalid SSID
invalid passphrase
enabled without network
orphaned passphrase without SSID
truncated v4 payload
invalid v4 enabled byte
oversized serialized values
storage write failure
```

### Migration Tests

Test:

```text
v1 → current in-memory configuration
v2 → current in-memory configuration
v3 → current in-memory configuration
```

Each migration must produce:

```text
wifi.enabled = false
wifi.ssid = ""
wifi.passphrase = ""
```

while preserving every field that existed in the source version.

Also verify:

```text
loading old schema performs no write
next successful save emits version 4
```

### NetworkService Tests

Use fake Connectivity behavior.

At minimum:

```text
start with no network
    → Off
    → no connect request

start configured but disabled
    → Off
    → no connect request

start configured and enabled
    → one connect request

configure while disabled
    → persisted
    → no connect request

configure while enabled
    → persisted first
    → reconnect requested

enable without network
    → rejected

enable configured network
    → persistence succeeds
    → connect requested

disable
    → persistence succeeds
    → disconnect requested

forget
    → disabled
    → credentials cleared
    → disconnect requested
```

### Failure Ordering Tests

Explicitly test atomicity:

```text
configure + storage failure
    → old published settings retained
    → no reconnect

enable + storage failure
    → old intent retained
    → no connect

disable + storage failure
    → old intent retained
    → no disconnect

forget + storage failure
    → credentials retained
    → no disconnect
```

Also test the opposite ordering:

```text
persistence succeeds
connection fails
    → new intent remains persisted
    → ConnectivityError reported
```

### Status Mapping Tests

Verify:

```text
disabled
    → Off

enabled + Connecting
    → Connecting

enabled + RetryWaiting
    → Connecting

enabled + Connected
    → Connected

enabled + Error
    → Error

Connected
    → RSSI published

not Connected
    → RSSI absent
```

### Secret Regression Tests

Prove that:

```text
status snapshot contains no passphrase
logs contain no passphrase
generic failure logging contains no credential value
```

Use a recognizable test credential so accidental leakage is detectable.

### Integration / Composition Checks

Verify that normal firmware composition includes:

```text
Esp32WifiAdapter
connectivity::WiFiService
NetworkService
```

and that Wi-Fi update remains outside UI scheduling.

---

## 18. Architecture Constraints

This plan must preserve:

```text
Apps
    ↓
Services
    ↓
Connectivity
    ↓
Hardware
```

Allowed:

```text
NetworkService
    ↓
connectivity::WiFiService
```

Allowed:

```text
connectivity::WiFiService
    ↓
IWifiAdapter
```

Allowed:

```text
Esp32WifiAdapter
implements
IWifiAdapter
```

Not allowed:

```text
Apps
    ↓
connectivity::WiFiService
```

Not allowed:

```text
NetworkService
    ↓
Esp32WifiAdapter
```

Not allowed:

```text
ConfigurationService
    ↓
hardware implementation
```

`src/main.cpp` remains the composition root.

The architecture checker must continue to pass without exceptions added specifically to bypass these boundaries.

---

## 19. Documentation Updates

Update the architecture documentation to record that:

```text
Wi-Fi is now composed in normal firmware
one station configuration is persisted
NetworkService owns user-facing Wi-Fi intent/status
Connectivity WiFiService owns radio lifecycle/retries
Wi-Fi UI is still pending
```

Update the Phase 3 checklist accordingly.

The configuration section should identify version 4 as the current persisted schema.

Do not update user-facing manuals to imply that Wi-Fi can already be configured interactively on-device.

Until the Phase 4 UI exists, runtime support and configuration capability are not equivalent to a delivered Wi-Fi Settings screen.

---

## 20. Acceptance Criteria

The plan is complete when:

* `SystemConfiguration` contains bounded persistent Wi-Fi configuration;
* configuration version 4 is the current write format;
* valid v1, v2, and v3 data still load losslessly;
* old schemas default Wi-Fi to disabled and unconfigured;
* loading an old schema does not eagerly rewrite storage;
* Wi-Fi credential validation has one shared credential contract;
* a Service-level Wi-Fi boundary owns persistent Wi-Fi intent;
* Apps do not need Connectivity Wi-Fi types;
* the public Wi-Fi status model contains no passphrase;
* normal firmware composes the ESP32 Wi-Fi adapter and Connectivity Wi-Fi service;
* enabled persisted Wi-Fi intent is restored after reboot;
* disabled Wi-Fi retains configured credentials;
* forgetting a network removes persisted credentials;
* storage failure does not publish partially applied configuration;
* transient connectivity failure does not roll back successfully persisted intent;
* Connectivity remains the owner of retry and timeout policy;
* Wi-Fi updates run independently of UI scheduling;
* Bluetooth/HostService behavior remains unchanged;
* existing configuration and host regression tests pass;
* new migration, persistence, NetworkService, status, and secret-safety tests pass;
* architecture dependency checks pass;
* formatting and static checks pass;
* production firmware builds successfully.

No physical-device UI acceptance is required because this plan does not add interactive Wi-Fi controls.

A basic hardware smoke test is recommended if convenient:

```text
preload valid enabled Wi-Fi configuration
boot firmware
observe station association
interrupt access point
observe reconnect behavior
restore access point
observe reconnection
confirm BLE host behavior remains operational
```

Automated behavior remains the merge gate.

---

## 21. Follow-Up

The natural next Wi-Fi slice belongs to Phase 4:

```text
Wi-Fi Settings UI
        ↓
SSID / password entry
        ↓
NetworkService
```

That plan may add:

```text
Settings → Wi-Fi
configured SSID display
enable / disable
change network
forget network
password-entry modal
connection status
error feedback
```

A later Phase 4 slice may then replace Home's current Wi-Fi placeholder with:

```text
NetworkService::status()
        ↓
OFFLINE / CONNECTING / ONLINE / ERROR
```

Network scanning should be added only if the approved Wi-Fi Settings interaction actually requires it.

Do not introduce scanning, multiple remembered networks, or priority selection as part of this plan.

Separately, the ActionBus cleanup identified after Plan 024 remains independent:

```text
typed/static action IDs
fail-fast registration
HostActionHandler adapter
```

Plan 025 should not expand into that refactor.

The purpose of this plan is one bounded step:

```text
existing Wi-Fi Connectivity foundation
        ↓
persistent user intent
        ↓
Service-level domain boundary
        ↓
normal firmware runtime
```

without prematurely implementing the Phase 4 UI.
