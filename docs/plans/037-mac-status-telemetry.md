# 037 — Mac Status Mini App and macOS System Telemetry

Status: **Software implemented; physical Cardputer-Adv acceptance pending**

Software validation on 2026-09-25: `make companion-check`, `make check`, and
`make firmware-size` passed. The firmware image is 1,207,744 bytes. Physical
telemetry, disconnect/reconnect, and Mac battery/no-battery acceptance still
require device testing; no Cardputer was connected during this implementation.

Suggested branch:

```text
feat/037-mac-status-telemetry
```

Suggested PR title:

```text
[037] Add Mac status telemetry mini app
```

Target repository:

```text
fat23cat/cardputer-hub
```

---

# 0. Current State

The repository already provides:

```text
Cardputer Hub
    ↓
CompanionService
    ↓
authenticated Companion GATT transport
    ⇅
Cardputer Companion.app
    ↓
macOS
```

Plan 030 introduced the Companion transport, handshake, capabilities, request/response protocol, liveness and macOS application-control foundation.

Plan 031 added:

```text
MAC CONTROL
    ↓
HostControlService
    ↓
CompanionService
    ↓
APP_ACTIVATE
```

The current Companion protocol is version 1 and supports:

```text
PING
CAPABILITIES
APP_ACTIVE
APP_ACTIVATE
APP_ACTIVE_CHANGED
```

The current protocol does **not** expose macOS system telemetry.

Plan 037 adds read-only macOS system telemetry and a new `MAC STATUS` Mini App.

The MVP metrics are exactly:

```text
CPU usage
RAM used / total
memory pressure
SSD usage
Mac battery percentage
network download rate
network upload rate
thermal state
```

Plan 037 explicitly does **not** use or display the active macOS application.

---

# 1. Goal

Add a `MAC STATUS` Mini App that provides a compact live overview of the currently connected Mac.

Conceptually:

```text
macOS
  ↓
MacSystemMetricsCollector
  ↓
Cardputer Companion.app
  ↓
Companion Protocol
  ↓
CompanionService
  ↓
MacStatusService
  ↓
MAC STATUS Mini App
```

The user should be able to open Launcher and see:

```text
MAC STATUS
```

only when the connected Companion supports system telemetry.

Opening the app starts telemetry polling.

Leaving the app stops telemetry polling.

No telemetry polling is required while `MAC STATUS` is not active.

---

# 2. Capability-Gated Availability

`MAC STATUS` is a capability-gated Mini App.

Register:

```text
id: mac-status
displayName: MAC STATUS
iconId: mac-status
entryRoute: mac-status
requiredCapabilities:
    SYSTEM_METRICS
```

The existing `MiniAppRuntime` availability contract is the only connection-state UI required.

Therefore:

```text
SYSTEM_METRICS unavailable
→ MAC STATUS is unavailable in Launcher

SYSTEM_METRICS available
→ MAC STATUS may be opened

SYSTEM_METRICS disappears while open
→ MAC STATUS deactivates
→ Launcher
```

Do not add connection status inside the Mini App.

Specifically, do not render:

```text
LIVE
CONNECTED
COMPANION
OFFLINE
ONLINE
MAC CONNECTED
connection dot
connection badge
```

The ability to enter the Mini App already communicates that the required host capability is present.

---

# 3. MVP UI

The MVP contains one full-screen Overview.

Do not add multiple pages in Plan 037.

Target display:

```text
240 × 135
```

There is no internal title bar or status header.

The full display belongs to telemetry.

Conceptual layout:

```text
┌────────────────────────────────────────┐
│                                        │
│ CPU  34%          RAM   11.4 / 16G     │
│ █████░░░░          ███████░░            │
│                                        │
│ SSD  63%          BAT   82%            │
│ ██████░░░          ████████░            │
│                                        │
│ ↓ 12.4 MB/s       ↑ 1.8 MB/s           │
│                                        │
│ PRESS NORMAL      THERM NORMAL         │
│                                        │
└────────────────────────────────────────┘
```

The screen should feel like one compact instrument panel rather than a system page containing a title and content.

There is no:

```text
active application
host name
SSID
IP address
VPN state
uptime
swap
top process
sparkline
LED output
```

in this plan.

---

# 4. Screen Geometry

Use the complete 240 × 135 surface.

Recommended structure:

```text
top breathing space
y = 0..8

CPU / RAM
y = 10..40

SSD / BAT
y = 47..77

network RX / TX
y = 86..103

memory pressure / thermal
y = 113..130
```

Horizontal structure:

```text
left margin   = 8 px
right margin  = 8 px

left column:
x = 8..115

right column:
x = 124..231
```

Bars should fit inside their own columns and never touch the neighboring metric.

Exact pixel geometry may be tuned after physical-device review.

Hard requirements:

```text
no header
no footer
no title row
no connection indicator
no persistent navigation chrome
```

---

# 5. Visual Language

Reuse the existing Cardputer Hub palette and typography.

Do not introduce a new visual theme.

Use:

```text
Bone
Ink
Pale
Blue
Leaf
Ordinal
Vermilion
```

according to existing UI semantics.

Recommended normal metric treatment:

```text
label      → normal Ink / Ordinal treatment
value      → Ink
bar track  → Pale
bar fill   → Blue
```

Do not dynamically turn CPU/RAM/SSD bars red based on arbitrary percentage thresholds.

Memory pressure and thermal state already contain semantic state and may use semantic colours.

For example:

```text
PRESS NORMAL
THERM NORMAL
```

may use normal/Leaf treatment.

Serious or critical OS-reported state may use Vermilion.

Do not invent a yellow threshold system specifically for this app.

---

# 6. Data Availability Presentation

There is no global `WAIT`, `LIVE`, `STALE` or `OFFLINE` label.

Freshness still exists internally in `MacStatusService`, but presentation is metric-oriented.

Before the first valid sample:

```text
CPU --
RAM --
SSD --
BAT --
↓ --
↑ --
PRESS --
THERM --
```

If telemetry temporarily becomes stale while `SYSTEM_METRICS` capability is still present:

```text
current numeric values
→ replaced with "--"
```

Do not show old values as current.

Do not add a separate stale banner.

A subsequent valid response restores values directly.

If the Companion session itself is lost, capability loss closes the Mini App through the normal runtime lifecycle.

---

# 7. Launcher Icon

Add a small monochrome Launcher icon consistent with existing system icons.

The icon should communicate monitoring/status rather than reuse the `MAC CONTROL` grid icon.

A simple bar/status-graph motif is sufficient.

Do not make `MAC STATUS` depend merely on:

```text
COMPANION
```

because an older Companion may support Plan 030/031 while not supporting telemetry.

Eligibility must require the specific live capability:

```text
SYSTEM_METRICS
```

---

# 8. Companion Protocol Compatibility

Do not silently extend the existing v1 capability list.

Current v1 clients reject unknown capability IDs.

Advertising a new capability from a new Companion to old firmware would therefore make an otherwise valid v1 session incompatible.

Plan 037 introduces:

```text
Companion Protocol v2
```

while preserving v1 interoperability.

Supported protocol versions after Plan 037:

```text
v2
v1
```

A new Companion advertises both versions.

A new Cardputer selects the highest mutually supported version.

Compatibility matrix:

```text
new firmware + new Companion
→ v2
→ SYSTEM_METRICS available

old firmware + new Companion
→ v1
→ existing functionality continues
→ no SYSTEM_METRICS

new firmware + old Companion
→ v1
→ existing functionality continues
→ MAC STATUS unavailable

old firmware + old Companion
→ unchanged v1
```

Existing GATT service and characteristic UUIDs remain unchanged.

Existing chunk framing remains unchanged.

Existing v1 operation IDs remain unchanged.

---

# 9. Version Negotiation

Refactor the current hard-coded v1 handshake into negotiated protocol selection.

Conceptually:

```text
Mac HELLO
supported:
    2
    1

Cardputer
    ↓
select highest common version

HELLO_ACK
selected:
    2
```

The handshake bootstrap must remain parseable by existing v1 implementations.

After negotiation, the session stores:

```text
selectedProtocolVersion
```

All normal session messages are validated against that selected version.

Do not allow:

```text
v1 session
→ SYSTEM_METRICS operation
```

---

# 10. New Companion Capability

Protocol v2 adds:

```text
CompanionCapability::SystemMetrics = 4
```

Mapped to runtime capability:

```text
SYSTEM_METRICS
```

Existing capability IDs remain:

```text
1 APP_ACTIVE
2 APP_ACTIVATE
3 APP_ACTIVE_EVENTS
```

v1 capability response:

```text
APP_ACTIVE
APP_ACTIVATE
APP_ACTIVE_EVENTS
```

v2 capability response:

```text
APP_ACTIVE
APP_ACTIVATE
APP_ACTIVE_EVENTS
SYSTEM_METRICS
```

When the Companion session disappears, `SYSTEM_METRICS` must disappear through the existing live-capability cleanup path.

---

# 11. New Protocol Operation

Protocol v2 adds:

```text
SYSTEM_METRICS = 6
```

Usage:

```text
REQUEST
RESPONSE
```

Request payload:

```text
empty
```

Response:

```text
OK
→ metrics payload

NOT_AVAILABLE
→ empty payload

MALFORMED
→ empty payload
```

The Mac should normally return `OK` even if an individual metric is unavailable.

Individual field availability belongs in the payload validity flags.

For example, a Mac without a battery may still return valid:

```text
CPU
RAM
SSD
network
thermal
```

with battery marked unavailable.

---

# 12. Metrics Payload v1

The first `SYSTEM_METRICS` payload schema is fixed and binary.

Keep it compact.

Recommended layout:

```text
offset  size  field

0       1     metrics schema version = 1
1       2     validity flags
3       1     CPU percent
4       4     RAM used MiB
8       4     RAM total MiB
12      1     memory pressure
13      1     SSD used percent
14      1     battery percent
15      1     thermal state
16      4     download KiB/s
20      4     upload KiB/s
```

Total:

```text
24 bytes
```

All multi-byte integers remain little-endian.

Suggested validity bits:

```text
bit 0 CPU
bit 1 RAM
bit 2 memory pressure
bit 3 SSD
bit 4 battery
bit 5 network
bit 6 thermal
```

Values whose validity bit is clear must be ignored.

---

# 13. Metric Semantics

## CPU

Represent total machine CPU utilisation normalized to:

```text
0..100%
```

Do not expose:

```text
0..N×100%
```

based on the number of cores.

## RAM

Expose:

```text
used MiB
total MiB
```

The macOS collector owns the exact system-memory calculation.

Document that value as a system physical-memory usage estimate.

It does not need to exactly reproduce Activity Monitor's decimal value at every sample.

Memory pressure remains the more important semantic health indicator.

## Memory pressure

Normalized values:

```text
Unknown
Normal
Warning
Critical
```

Do not infer pressure purely from:

```text
RAM used %
```

If reliable macOS pressure state cannot be obtained, return:

```text
Unknown
```

rather than fabricating a state.

## SSD

MVP shows usage of the filesystem containing the macOS root volume.

Expose:

```text
0..100%
```

No per-volume browser is required.

## Battery

Expose:

```text
0..100%
```

If no system battery exists:

```text
battery validity = false
```

The UI renders:

```text
BAT --
```

Charging state and battery health are out of scope for Plan 037.

## Network

Expose instantaneous rate from the Mac perspective:

```text
download = receive
upload   = transmit
```

Use:

```text
KiB/s
```

on the wire.

UI formatting may display:

```text
B/s
KB/s
MB/s
GB/s
```

as appropriate.

Prefer the primary active network interface rather than blindly summing every virtual interface and tunnel, which may double-count traffic.

The first network sample may be unavailable because a previous counter sample does not yet exist.

## Thermal

Normalize macOS thermal state into:

```text
Unknown
Normal
Fair
Serious
Critical
```

The UI renders compact strings:

```text
NORMAL
FAIR
SERIOUS
CRITICAL
--
```

---

# 14. macOS Metrics Boundary

Add a testable metrics boundary to CompanionCore.

Conceptually:

```swift
public struct SystemMetricsSample {
    ...
}

public protocol SystemMetricsCollecting {
    func collect() -> SystemMetricsSample
}
```

`CompanionSession` depends on:

```text
ApplicationControlling
SystemMetricsCollecting
```

It must not directly implement low-level CPU, VM, disk, battery or network sampling.

Add the production macOS collector separately.

Suggested layout:

```text
companion/macos/Sources/CompanionCore/
    SystemMetrics.swift

companion/macos/Sources/App/
    MacSystemMetricsCollector.swift
```

The App composition creates the real collector and injects it into `CompanionSession`.

This keeps protocol/session logic testable without real machine metrics.

---

# 15. macOS Collector Responsibilities

`MacSystemMetricsCollector` owns:

```text
CPU sampling
memory sampling
memory-pressure state
root-volume usage
battery percentage
network counters/rates
thermal state
```

It must not own:

```text
BLE
GATT
Companion framing
request IDs
session negotiation
Cardputer UI
polling policy
```

Use supported macOS facilities available to the deployment target.

The current Companion deployment target remains:

```text
macOS 13+
```

If additional Apple frameworks are required, add them explicitly to the Swift package target.

No shelling out to tools such as:

```text
top
vm_stat
iostat
netstat
```

on every telemetry request.

Sampling must be native and bounded.

---

# 16. Network Rate Sampling

Network throughput is derived from monotonically increasing interface counters.

Collector state conceptually contains:

```text
previous RX bytes
previous TX bytes
previous sample timestamp
previous interface
```

Rate:

```text
delta bytes / delta time
```

If:

```text
interface changed
counter decreased
sample interval invalid
no previous sample
```

then network is unavailable for that sample.

Do not emit negative or wrapped rates.

Do not retain a rate computed from a previous interface.

---

# 17. CompanionService Extension

Add:

```cpp
CompanionSubmitResult requestSystemMetrics();
```

The service remains responsible for:

```text
session
request IDs
timeouts
protocol validation
capability publication
response correlation
```

`CompanionService` does not decide the one-second monitoring cadence.

That belongs to `MacStatusService`.

---

# 18. Completed-Request Routing Fix

The current production architecture has one global:

```text
CompanionService::takeCompletedRequest()
```

queue.

`HostControlService` currently drains this queue and ignores operations it does not own.

Adding `MacStatusService` as a second unfiltered consumer would create a race:

```text
APP_ACTIVATE response
→ MacStatusService consumes it
→ HostControlService never receives completion

or

SYSTEM_METRICS response
→ HostControlService consumes it
→ MacStatusService never receives snapshot
```

Plan 037 must fix this before adding the second consumer.

Add operation-filtered completion retrieval, conceptually:

```cpp
takeCompletedRequest(CompanionOperation operation)
```

It must remove only a matching completion while preserving unrelated queued completions.

Production ownership becomes:

```text
HostControlService
→ APP_ACTIVATE completions only

MacStatusService
→ SYSTEM_METRICS completions only
```

Internal handshake operations such as:

```text
CAPABILITIES
heartbeat PING
```

must not accumulate forever in the public completion queue.

Mark internal requests as non-deliverable or consume them entirely inside `CompanionService`.

The unfiltered production completion-drain pattern must be removed.

---

# 19. Completion Ownership Invariant

After Plan 037:

```text
HostControlService is the only consumer of APP_ACTIVATE completions.

MacStatusService is the only consumer of SYSTEM_METRICS completions.

Neither service may consume arbitrary Companion completions.
```

This is a required regression invariant.

---

# 20. MacStatusService

Add:

```text
src/services/mac_status/
    mac_status_service.h
    mac_status_service.cpp
```

Responsibilities:

```text
monitoring enabled/disabled state
one-second polling cadence
one in-flight metrics request
decode normalized metrics snapshot
freshness
generation
temporary error handling
```

Conceptual API:

```cpp
class MacStatusService {
  public:
    void startMonitoring();
    void stopMonitoring();

    void update(std::chrono::milliseconds elapsed);

    MacStatusSnapshot snapshot() const;
};
```

The exact API may vary, but the ownership boundary must remain.

---

# 21. MacStatusSnapshot

Conceptually:

```cpp
enum class MacMemoryPressure {
    Unknown,
    Normal,
    Warning,
    Critical,
};

enum class MacThermalState {
    Unknown,
    Normal,
    Fair,
    Serious,
    Critical,
};

enum class MacStatusFreshness {
    Empty,
    Fresh,
    Stale,
};

struct MacStatusSnapshot {
    std::uint32_t generation;

    MacStatusFreshness freshness;

    bool cpuAvailable;
    std::uint8_t cpuPercent;

    bool memoryAvailable;
    std::uint32_t memoryUsedMiB;
    std::uint32_t memoryTotalMiB;

    MacMemoryPressure memoryPressure;

    bool diskAvailable;
    std::uint8_t diskUsedPercent;

    bool batteryAvailable;
    std::uint8_t batteryPercent;

    bool networkAvailable;
    std::uint32_t downloadKiBps;
    std::uint32_t uploadKiBps;

    MacThermalState thermalState;
};
```

Exact storage types may differ.

Do not expose raw Companion payloads to the Mini App.

`freshness` is Service state only.

It does not imply a dedicated freshness label in the UI.

---

# 22. Polling Policy

When `MAC STATUS` activates:

```text
startMonitoring()
→ immediate SYSTEM_METRICS request
```

While active:

```text
poll interval ≈ 1 second
```

At most one metrics request may be in flight.

If the previous request is still pending:

```text
do not submit another
```

Do not queue requests.

Do not attempt to catch up missed polling ticks.

Example:

```text
t=0.0  request
t=1.0  previous still pending
       → skip
t=1.4  completion
t=2.0  next request
```

This keeps BLE traffic bounded.

---

# 23. Monitoring Lifecycle

`MacStatusApp::onActivate()`:

```text
MacStatusService.startMonitoring()
```

`MacStatusApp::onDeactivate()`:

```text
MacStatusService.stopMonitoring()
```

Stopping monitoring must:

```text
stop new polling
clear local in-flight expectation
```

It does not need to cancel an already transmitted Companion request at the transport level.

A late completion after deactivation may be safely consumed by `MacStatusService`, but it must not restart monitoring or navigate the UI.

Reopening the app performs a new immediate request.

---

# 24. Freshness

Recommended freshness threshold:

```text
3 seconds
```

Internal Service states:

```text
no successful sample yet
→ Empty

last successful sample <= 3 s
→ Fresh

last successful sample > 3 s
→ Stale
```

Presentation mapping:

```text
Empty
→ unavailable placeholders

Fresh
→ current metric values

Stale
→ unavailable placeholders
```

There is no textual:

```text
WAIT
LIVE
STALE
```

UI state.

The service may retain the last snapshot internally for diagnostics, but the presentation must not show stale values as current.

Recovery:

```text
next valid response
→ Fresh
→ generation increments
→ values reappear
```

No app restart is required.

---

# 25. Companion Loss

`MAC STATUS` requires:

```text
SYSTEM_METRICS
```

When Companion disappears:

```text
CompanionService
    ↓
live capabilities cleared
    ↓
SYSTEM_METRICS removed
    ↓
MiniAppRuntime
    ↓
MAC STATUS deactivated
    ↓
Launcher
```

Do not add a separate connection-management page or disconnected state inside `MAC STATUS`.

After reconnect:

```text
SYSTEM_METRICS restored
→ MAC STATUS becomes eligible again
```

but:

```text
do not reopen MAC STATUS automatically
```

---

# 26. MAC STATUS Mini App

Suggested files:

```text
src/apps/mac_status/
    mac_status_app.h
    mac_status_app.cpp
    mac_status_graphics.h
    mac_status_graphics.cpp
```

The Mini App owns:

```text
activation/deactivation
presentation state
formatting
drawing
```

It does not own:

```text
BLE
Companion protocol
request IDs
poll timers
macOS APIs
metric collection
response correlation
connection-state UI
```

---

# 27. Rendering

Render only when presentation state changes.

A new service snapshot increments:

```text
generation
```

The app uses generation/freshness to determine whether repaint is required.

Settled state with no metric change should not continuously redraw.

A one-second telemetry update may naturally repaint the metric regions that changed.

Prefer bounded dirty regions:

```text
CPU change
→ CPU block

RAM change
→ RAM block

SSD change
→ SSD block

battery change
→ battery block

network change
→ network row

pressure / thermal change
→ lower status row
```

Do not redraw the entire screen every system loop iteration.

There is no header region to redraw.

---

# 28. Value Formatting

CPU:

```text
CPU 34%
CPU --
```

RAM:

```text
RAM 11.4/16G
RAM --
```

SSD:

```text
SSD 63%
SSD --
```

Battery:

```text
BAT 82%
BAT --
```

Network examples:

```text
↓ 420 KB/s
↑ 18 KB/s

↓ 12.4 MB/s
↑ 1.8 MB/s
```

Keep formatting bounded so numbers do not shift the second column.

Large rates may be clamped to a compact display representation.

Memory pressure:

```text
PRESS NORMAL
PRESS WARN
PRESS CRIT
PRESS --
```

Thermal:

```text
THERM NORMAL
THERM FAIR
THERM SERIOUS
THERM CRIT
THERM --
```

---

# 29. Input

Plan 037 has no interactive controls inside the dashboard.

Therefore:

```text
Left
→ no app action

Right
→ no app action

Up
→ no app action

Down
→ no app action

Enter
→ no app action
```

Escape remains shell-owned and exits through the existing Mini App lifecycle.

Wake-only input remains governed by the shared display-power contract.

A wake key must not trigger any MAC STATUS behavior.

---

# 30. No Active Application Integration

This requirement is explicit.

`MAC STATUS` must not:

```text
read CompanionService active bundle state
request APP_ACTIVE
listen to APP_ACTIVE_CHANGED
show application names
show bundle identifiers
derive telemetry from the foreground application
```

Existing APP_ACTIVE functionality remains available for other features.

Plan 037 simply does not consume it.

---

# 31. Ownership Matrix

| Component | Owns | Must not own |
| --- | --- | --- |
| `MacSystemMetricsCollector` | macOS metric sampling | BLE, protocol sessions, UI |
| Companion protocol codec | binary schema and validation | polling policy, UI |
| `CompanionSession` | handling `SYSTEM_METRICS` requests | metric rendering |
| `CompanionService` | session, capabilities, request correlation, timeout | dashboard polling cadence |
| `MacStatusService` | monitoring cadence, freshness, normalized snapshot | macOS APIs, drawing |
| `MacStatusApp` | presentation and lifecycle | Companion request handling |
| `MiniAppRuntime` | capability eligibility/deactivation | telemetry semantics |
| `main.cpp` | composition/update ordering | metric business logic |

---

# 32. Architecture Invariants

The following must remain true:

```text
Mini Apps never parse Companion protocol payloads.

MAC STATUS never calls macOS APIs.

MacStatusService never draws UI.

MacSystemMetricsCollector never knows about Cardputer UI.

CompanionService remains the owner of protocol request IDs and session state.

MacStatusService never constructs raw Companion envelopes.

Only HostControlService consumes APP_ACTIVATE completions.

Only MacStatusService consumes SYSTEM_METRICS completions.

One service may not steal another service's completion.

Only one SYSTEM_METRICS request may be in flight.

Telemetry polling occurs only while MAC STATUS is active.

Companion loss removes SYSTEM_METRICS capability.

Capability loss deactivates MAC STATUS through MiniAppRuntime.

Reconnect never automatically reopens MAC STATUS.

Protocol v1 behavior remains interoperable.

MAC STATUS never depends on APP_ACTIVE or APP_ACTIVE_CHANGED.

MAC STATUS contains no duplicate Companion connection indicator.

MAC STATUS contains no internal title/header chrome.
```

---

# 33. BDD — App availability

```gherkin
Scenario: MAC STATUS appears when telemetry is supported
  Given a compatible Companion session is Ready
  And the Companion advertises SYSTEM_METRICS
  Then SYSTEM_METRICS is available in CapabilityRegistry
  And MAC STATUS is eligible in Launcher
```

```gherkin
Scenario: Old v1 Companion does not expose MAC STATUS
  Given the negotiated Companion protocol is v1
  Then SYSTEM_METRICS is unavailable
  And MAC STATUS is not eligible
  And existing Companion features continue working
```

---

# 34. BDD — Capability is the connection UI

```gherkin
Scenario: MAC STATUS contains no redundant connection state
  Given SYSTEM_METRICS is available
  And MAC STATUS is open
  Then the screen does not show LIVE
  And the screen does not show CONNECTED
  And the screen does not show COMPANION
  And the screen does not show a connection-state dot
  And the full application surface is reserved for telemetry
```

---

# 35. BDD — Initial load

```gherkin
Scenario: MAC STATUS requests telemetry immediately
  Given SYSTEM_METRICS is available
  When MAC STATUS activates
  Then MacStatusService starts monitoring
  And exactly one SYSTEM_METRICS request is submitted immediately
  And unavailable metric placeholders are shown until a valid response arrives
```

---

# 36. BDD — Live metrics

```gherkin
Scenario: Valid telemetry becomes visible
  Given MAC STATUS is active
  And a SYSTEM_METRICS request is pending
  When a valid metrics response arrives
  Then MacStatusService stores the normalized snapshot
  And snapshot generation increments
  And freshness becomes Fresh
  And the UI renders the available metrics
  And no global LIVE label is rendered
```

---

# 37. BDD — Partial metrics

```gherkin
Scenario: Mac has no battery
  Given the Mac reports valid CPU, RAM, SSD, network and thermal metrics
  But battery validity is false
  When MAC STATUS renders the snapshot
  Then CPU, RAM, SSD, network and thermal values are shown
  And battery renders as "--"
  And the entire snapshot is not treated as failed
```

---

# 38. BDD — Network bootstrap

```gherkin
Scenario: First network sample has no rate baseline
  Given MAC STATUS has just started monitoring
  And the network collector has no previous byte counters
  When the first metrics snapshot is collected
  Then network validity may be false
  And the UI renders both network rates as "--"
  When the next valid counter sample is collected
  Then download and upload rates become available
```

---

# 39. BDD — Polling cadence

```gherkin
Scenario: Active monitoring polls once per interval
  Given MAC STATUS is active
  And the previous metrics request completed
  When one polling interval elapses
  Then one new SYSTEM_METRICS request is submitted
```

```gherkin
Scenario: Slow response does not create overlapping requests
  Given MAC STATUS is active
  And a SYSTEM_METRICS request is still pending
  When another polling interval elapses
  Then no additional SYSTEM_METRICS request is submitted
```

---

# 40. BDD — Stale telemetry

```gherkin
Scenario: Telemetry stops updating temporarily
  Given MAC STATUS has a valid fresh snapshot
  When no successful metrics response arrives for the freshness threshold
  Then old numeric values are not presented as current
  And unavailable placeholders are rendered
  And no STALE banner is shown
```

```gherkin
Scenario: Telemetry recovers
  Given MAC STATUS currently renders unavailable placeholders because telemetry is stale
  When a valid SYSTEM_METRICS response arrives
  Then freshness becomes Fresh
  And generation increments
  And current values are shown directly
```

---

# 41. BDD — Stop monitoring

```gherkin
Scenario: User exits MAC STATUS
  Given MAC STATUS is active
  When the Mini App deactivates
  Then MacStatusService stops submitting new metrics requests
  And a late response does not reopen the app
  And a late response does not restart monitoring
```

---

# 42. BDD — Companion loss

```gherkin
Scenario: Companion disconnects while MAC STATUS is open
  Given MAC STATUS is active
  When the Companion session becomes unavailable
  Then SYSTEM_METRICS is removed
  And MiniAppRuntime deactivates MAC STATUS
  And Launcher becomes visible
```

```gherkin
Scenario: Companion reconnects
  Given MAC STATUS was closed because SYSTEM_METRICS disappeared
  When a compatible v2 Companion reconnects
  Then SYSTEM_METRICS becomes available
  And MAC STATUS becomes eligible
  But MAC STATUS does not reopen automatically
```

---

# 43. BDD — Completion routing regression

```gherkin
Scenario: APP_ACTIVATE and SYSTEM_METRICS are both outstanding
  Given MAC CONTROL submitted APP_ACTIVATE
  And MacStatusService submitted SYSTEM_METRICS
  When the SYSTEM_METRICS response arrives first
  Then only MacStatusService consumes it
  And HostControlService remains Pending
  When the APP_ACTIVATE response arrives
  Then only HostControlService consumes it
  And the MAC CONTROL command completes normally
```

This test is required.

---

# 44. Protocol Tests

Add coverage for:

```text
v1 hello negotiation
v2 hello negotiation
new Companion + v1 peer fallback
new firmware + v1 Companion fallback

v1 capability response remains unchanged
v2 capability response includes SYSTEM_METRICS

v1 rejects SYSTEM_METRICS
v2 accepts SYSTEM_METRICS

SYSTEM_METRICS request must have empty payload

valid 24-byte metrics payload round-trip
wrong schema version rejected
truncated payload rejected
oversized payload rejected
invalid enum rejected
invalid percentages rejected or normalized according to codec contract
validity flags respected
```

Keep matching firmware and Swift fixtures where appropriate.

---

# 45. CompanionService Tests

Cover:

```text
SYSTEM_METRICS capability publication

requestSystemMetrics while Ready
requestSystemMetrics while unavailable
requestSystemMetrics while v1
requestSystemMetrics when pending table is full

correct request ID correlation
timeout
malformed response
session loss

filtered completion retrieval
APP_ACTIVATE completion cannot be stolen by MacStatusService
SYSTEM_METRICS completion cannot be stolen by HostControlService

internal CAPABILITIES completion does not accumulate in the public queue
heartbeat completion does not accumulate in the public queue
```

---

# 46. MacStatusService Tests

Cover:

```text
inactive service does not poll

startMonitoring submits immediately

1-second cadence

only one request in flight

valid response updates snapshot

generation increments only for accepted new snapshot

partial validity preserved

malformed metrics rejected

timeout does not create request storm

fresh → stale transition

stale → fresh recovery

stopMonitoring stops polling

late completion after stop does not restart monitoring

capability loss stops usable status

reconnect does not start monitoring until Mini App activates again
```

Use injected elapsed time.

No real sleeps.

---

# 47. MAC STATUS UI Tests

Cover:

```text
AppRegistry registration

requires SYSTEM_METRICS

no internal title bar

no LIVE indicator

no Companion indicator

no connection-state dot

full-screen telemetry layout

empty-state placeholders

CPU formatting

RAM used / total formatting

SSD formatting

battery formatting

network compact units

memory-pressure labels

thermal labels

individual unavailable metric renders "--"

stale values are replaced by placeholders

no active-app label exists

no active-app bundle is consumed

unchanged snapshot does not continuously repaint

changed metric redraw is bounded

Escape follows normal Mini App exit behavior

wake-only input produces no app action
```

---

# 48. macOS Companion Tests

Use a fake:

```text
SystemMetricsCollecting
```

to verify:

```text
SYSTEM_METRICS request invokes collector once

valid sample produces expected payload

partial sample produces validity flags

collector unavailable produces NOT_AVAILABLE when appropriate

v1 session does not advertise or accept SYSTEM_METRICS

v2 session advertises SYSTEM_METRICS
```

Existing:

```text
APP_ACTIVE
APP_ACTIVATE
APP_ACTIVE_CHANGED
PING
```

behavior remains unchanged.

---

# 49. Composition

Normal firmware composition becomes conceptually:

```text
CompanionService
    ↓
HostControlService

CompanionService
    ↓
MacStatusService
    ↓
MacStatusApp
```

Main update order should preserve:

```text
companion.update(elapsed)
    ↓
hostControl.update()
macStatus.update(elapsed)
```

Both downstream services observe completions only for operations they own.

Register:

```text
MAC STATUS
```

after its Service and Mini App instance are composed.

---

# 50. Build Integration

Add the new firmware sources to the normal ESP-IDF build.

Update Swift package/linker settings if additional macOS frameworks are required by the production metrics collector.

Do not add third-party metrics libraries for this MVP unless a platform API is genuinely insufficient.

Prefer native macOS APIs.

---

# 51. Documentation

Update:

```text
README.md
docs/ARCHITECTURE.md
docs/UI_REQUIREMENTS.md
docs/manuals/device-guide.md
docs/plans/README.md
protocol/companion/README.md
companion/macos/README.md
```

Document:

```text
Companion protocol v2
v1 compatibility
SYSTEM_METRICS capability
SYSTEM_METRICS operation
metrics payload
MAC STATUS Mini App
one-second foreground polling
freshness behavior
capability-loss behavior
```

The UI documentation must make clear that `MAC STATUS` has:

```text
no internal header
no connection-state label
no connection-state icon
```

Do not document planned future metrics as already available.

---

# 52. Physical Acceptance

## Basic display

With a supported Mac connected:

```text
Launcher
→ MAC STATUS
→ Enter
```

Expected:

```text
full-screen telemetry dashboard
```

Display shows:

```text
CPU
RAM
memory pressure
SSD
battery
download
upload
thermal
```

It does not show:

```text
MAC STATUS
LIVE
CONNECTED
COMPANION
active application
```

inside the Mini App.

## CPU

Generate CPU load on the Mac.

Expected:

```text
CPU value visibly rises
then falls after load ends
```

Exact parity with Activity Monitor at a single instant is not required because sampling windows may differ.

## RAM

Start/stop a memory-heavy workload.

Expected:

```text
RAM changes plausibly
total RAM remains stable
memory-pressure state comes from its independent source
```

## SSD

Compare the percentage with the Mac's root-volume usage.

Expected:

```text
same broad usage level
```

## Battery

On a MacBook:

```text
battery percentage is plausible
```

On hardware without a battery:

```text
BAT --
```

## Network

Generate a meaningful download.

Expected:

```text
download rises
upload remains independently represented
```

Generate an upload.

Expected:

```text
upload rises
```

## Thermal

Normal machine:

```text
THERM NORMAL
```

Do not intentionally overheat hardware solely to force higher thermal states.

## Disconnect

While MAC STATUS is open:

```text
close Companion / break the session
```

Expected:

```text
SYSTEM_METRICS disappears
MAC STATUS exits
Launcher appears
```

There is no intermediate:

```text
OFFLINE
DISCONNECTED
LOST CONNECTION
```

screen.

## Reconnect

Expected:

```text
MAC STATUS becomes available again
but does not automatically reopen
```

---

# 53. Non-goals

Plan 037 does not add:

```text
active application

top CPU process
top RAM process
swap
load average
Wi-Fi SSID
Wi-Fi RSSI
local IP
VPN state
uptime

battery charging state
battery health
battery cycle count

disk read/write throughput
multiple disks

network interface selection UI
per-interface traffic

historical graphs
sparklines
metric history persistence

alerts
notifications
threshold configuration

LED Puzzle output

Mac control actions
kill process
sleep Mac
lock Mac
open Activity Monitor

user-configurable dashboard
multiple MAC STATUS pages

internal connection status
LIVE indicator
OFFLINE screen
title/header bar
```

These may be separate future plans.

---

# 54. Recommended Implementation Order

```text
01. Add protocol v2 constants and negotiation model

02. Preserve v1 compatibility

03. Add SYSTEM_METRICS capability

04. Add SYSTEM_METRICS operation

05. Define metrics payload schema and firmware codec

06. Add matching Swift codec

07. Add protocol compatibility fixtures/tests

08. Add SystemMetricsCollecting boundary

09. Implement MacSystemMetricsCollector

10. Inject collector into CompanionSession

11. Handle SYSTEM_METRICS requests in CompanionSession

12. Add requestSystemMetrics() to CompanionService

13. Fix completed-request routing

14. Convert HostControlService to filtered APP_ACTIVATE consumption

15. Add MacStatusService

16. Implement start/stop monitoring lifecycle

17. Implement one-second polling

18. Implement freshness behavior

19. Add MAC STATUS AppDescriptor requiring SYSTEM_METRICS

20. Add Launcher icon

21. Add MacStatusApp

22. Implement full-screen Overview geometry

23. Implement CPU / RAM blocks

24. Implement SSD / battery blocks

25. Implement RX / TX row

26. Implement pressure / thermal row

27. Implement unavailable placeholders

28. Add dirty-region rendering

29. Add protocol tests

30. Add CompanionService completion-routing regression tests

31. Add MacStatusService tests

32. Add MAC STATUS rendering tests

33. Add macOS Companion tests

34. Run companion-check

35. Run host-check

36. Run firmware-check

37. Run firmware-size

38. Run CI

39. Flash Cardputer

40. Perform physical telemetry acceptance

41. Perform disconnect/reconnect acceptance

42. Update documentation
```

---

# 55. Validation Commands

During implementation use targeted tests first.

Final validation should include:

```bash
make companion-check
make host-check
make firmware-check
make firmware-size
```

and the complete applicable CI workflow.

Do not treat a successful firmware build as sufficient validation for the Swift Companion changes.

---

# 56. Completion Criteria

Plan 037 is complete when:

```text
[x] Companion protocol v2 is implemented

[x] v1 compatibility remains intact

[x] SYSTEM_METRICS is a negotiated live capability

[x] SYSTEM_METRICS uses a bounded fixed binary payload

[x] new firmware + old Companion falls back to v1

[x] old firmware + new Companion falls back to v1

[x] MacSystemMetricsCollector provides CPU

[x] MacSystemMetricsCollector provides RAM used / total

[x] memory pressure is reported independently from RAM percentage

[x] root-volume SSD usage is reported

[x] battery percentage is reported when available

[x] download/upload rates are reported

[x] thermal state is reported

[x] CompanionService supports SYSTEM_METRICS requests

[x] completed requests have explicit operation ownership

[x] HostControlService cannot consume telemetry completions

[x] MacStatusService cannot consume APP_ACTIVATE completions

[x] MAC CONTROL behavior remains intact

[x] MacStatusService polls only while MAC STATUS is active

[x] polling is approximately once per second

[x] only one metrics request may be in flight

[x] stale telemetry is not shown as live data

[x] MAC STATUS appears only when SYSTEM_METRICS is available

[x] MAC STATUS uses the approved full-screen Overview layout

[x] MAC STATUS has no internal title/header

[x] MAC STATUS has no LIVE/CONNECTED/COMPANION indicator

[x] CPU is displayed

[x] RAM used / total is displayed

[x] memory pressure is displayed

[x] SSD percentage is displayed

[x] battery percentage is displayed

[x] download/upload rates are displayed

[x] thermal state is displayed

[x] unavailable individual metrics render "--"

[x] Active app is not displayed or consumed by MAC STATUS

[x] Companion loss closes MAC STATUS through MiniAppRuntime

[x] reconnect does not reopen MAC STATUS automatically

[x] unchanged UI does not continuously redraw

[x] native firmware tests pass

[x] macOS Companion tests pass

[x] host-check passes

[x] companion-check passes

[x] firmware-check passes

[x] firmware-size passes

[ ] CI passes

[ ] physical Cardputer-Adv telemetry acceptance passes
```

---

# 57. End State

After Plan 037:

```text
                 Mac
                  │
        native system metrics
                  │
                  ▼
       Cardputer Companion.app
                  │
         SYSTEM_METRICS v2
                  │
                  ▼
          CompanionService
                  │
                  ▼
          MacStatusService
                  │
                  ▼
┌────────────────────────────────────────┐
│                                        │
│ CPU  34%          RAM   11.4 / 16G     │
│ █████░░░░          ███████░░            │
│                                        │
│ SSD  63%          BAT   82%            │
│ ██████░░░          ████████░            │
│                                        │
│ ↓ 12.4 MB/s       ↑ 1.8 MB/s           │
│                                        │
│ PRESS NORMAL      THERM NORMAL         │
│                                        │
└────────────────────────────────────────┘
```

The capability system communicates whether the Mini App can be used.

The Mini App itself communicates only the Mac telemetry the user opened it to see.

`MAC STATUS` remains a thin presentation layer.

macOS sampling belongs to the macOS collector.

Monitoring state belongs to `MacStatusService`.

Protocol/session ownership remains in `CompanionService`.

Existing `MAC CONTROL` remains independent and continues using the same Companion connection.
