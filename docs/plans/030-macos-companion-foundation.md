030 — macOS Companion Foundation

Status: Software implemented; physical acceptance pending

0. Planning Status

Plan 029 delivered the first complete production Mini App workflow:

Home
  ↓
Launcher
  ↓
MiniAppRuntime
  ↓
SYSTEM

Plan 030 introduces the host-side Companion foundation required for semantic macOS integration that cannot be represented reliably through BLE HID alone.

The Companion is optional.

Normal Cardputer functionality must continue working when the Companion is not installed, not running, disconnected, incompatible, or unavailable.

The target architecture is:

future host-facing feature
        ↓
HostControlService
        ↓
 CompanionService
        ↓
ICompanionTransport
        ↓
existing BluetoothService
        ↓
ESP-NimBLE custom GATT service
        ⇅
Cardputer Companion.app
        ↓
CoreBluetooth / AppKit
        ↓
      macOS

Plan 030 does not implement MAC CONTROL.

It establishes the full transport, protocol, session, lifecycle, capability, active-application event, and macOS application-control foundation required by Plan 031.

Suggested branch:

feat/030-macos-companion-foundation

Suggested PR title:

[030] Add macOS Companion foundation

⸻

1. Goal

Deliver a reliable, versioned and authenticated communication channel between Cardputer Hub and a native headless macOS Companion.

After Plan 030:

Cardputer Hub
    │
    │ one existing BLE connection
    │
    ├── HID Service
    │      ├── keyboard
    │      └── consumer control
    │
    └── Companion GATT Service
              ⇅
       Cardputer Companion.app
              ↓
       semantic macOS APIs

The first protocol supports:

ping
capabilities
app.active
app.activate
app.active.changed EVENT

A healthy compatible Companion session exposes:

COMPANION

as a live runtime capability on Cardputer.

Home displays a compact passive Companion-ready indicator:

◆

The indicator means:

a compatible authenticated Companion session for the currently selected host is alive and capable of accepting semantic requests now.

⸻

2. Scope

Included

Firmware:

custom Companion GATT service
ICompanionTransport
BLE Companion transport view
bounded fragmentation/reassembly
Companion protocol v1
CompanionService
session generation
request IDs
request correlation
request timeout
heartbeat/liveness
live capability negotiation
EVENT delivery
COMPANION runtime capability
Home ◆ indicator
selected-host/session invalidation
shared binary protocol fixtures
native tests
physical BLE/HID regression tests

macOS:

native Swift headless .app
CoreBluetooth
AppKit
ServiceManagement / SMAppService
automatic launch at login
automatic re-attach
automatic session recovery
foreground application observation
application activation
protocol v1 implementation
Swift tests
macOS CI

Initial semantic operations:

ping
capabilities
app.active
app.activate
app.active.changed

Not included

MAC CONTROL Mini App
hotkey UI
application profiles
hotkey layers
configurable mappings
mapping editor
menu-bar UI
Dock UI
settings window
general-purpose companion UI
media metadata
media state
window management
clipboard
Accessibility APIs
Automation permission
AppleScript
arbitrary shell execution
arbitrary executable execution
filesystem access
URL opening
notifications
file transfer
Telegram-specific behavior
Codex-specific behavior
Wi-Fi Companion transport
USB Companion transport
transport arbitration
remote control of Cardputer from Mac
multi-Cardputer support
generic RPC framework
generic IService abstraction

⸻

3. Core Architectural Rule: One BLE Owner

The existing BluetoothService remains the single logical owner of Bluetooth lifecycle and Bluetooth adapter events.

Do not introduce:

BluetoothService
    ↓ pollEvent()
CompanionBleTransport
    ↓ pollEvent()

Two consumers must never compete for IBluetoothAdapter events.

Instead:

              IBluetoothAdapter
                     ↓
              BluetoothService
              ↙             ↘
      hidTransport()    companionTransport()

BluetoothService continues to own:

connection lifecycle
peer identity
bond identity
selected bond
security state
pairing
advertising
reconnection
adapter event polling
single-connection policy

The Companion transport is a non-owning view over the already-selected secure Bluetooth peer, analogous to the existing HID transport view.

This avoids creating a second Bluetooth state machine.

⸻

4. Dependency Direction

Firmware dependency direction remains:

apps
 ↓
services
 ↓
connectivity
 ↓
hardware

CompanionService belongs under:

src/services/companion/

Transport-facing definitions belong under:

src/connectivity/companion/

or the existing Bluetooth connectivity subtree if implementation coupling requires it.

ESP-NimBLE GATT implementation belongs under:

src/hardware/esp32/bluetooth/

The macOS program remains completely outside firmware layering.

⸻

5. Repository Layout

Recommended structure:

cardputer-hub/
├── companion/
│   └── macos/
│       ├── Cardputer Companion.xcodeproj
│       ├── Cardputer Companion/
│       │   ├── App/
│       │   ├── Bluetooth/
│       │   ├── Protocol/
│       │   └── Applications/
│       └── Cardputer CompanionTests/
│
├── protocol/
│   └── companion/
│       ├── README.md
│       └── fixtures/
│
└── src/
    ├── connectivity/
    │   └── companion/
    └── services/
        └── companion/

Exact Swift source decomposition may vary.

The following boundary must remain:

shared:
    wire contract
    UUID constants
    binary fixtures
not shared:
    implementation code

Firmware C++ and Swift do not share a compiled library.

⸻

6. macOS Companion Form

The first Companion is a native:

Cardputer Companion.app

implemented in Swift.

Frameworks:

CoreBluetooth
AppKit
Foundation
ServiceManagement

The application is headless:

no Dock icon
no menu-bar item
no main window
no settings window

Use agent-style application behavior such as:

LSUIElement

where appropriate.

The application still has a proper bundle identity, Info.plist and macOS privacy declaration for Bluetooth.

This is intentionally a real .app rather than a raw command-line process so that macOS permissions, identity and startup lifecycle are stable.

⸻

7. Automatic Startup

The Companion automatically starts for the logged-in user.

Use modern macOS ServiceManagement through:

SMAppService

Expected first-run flow:

user launches Cardputer Companion.app once
        ↓
application registers login startup
        ↓
macOS approval if required
        ↓
subsequent user login
        ↓
Cardputer Companion.app starts automatically

The Companion must expose registration failure explicitly in logs.

If macOS denies login-item approval, normal manual application launch remains possible.

No legacy direct manipulation of:

~/Library/LaunchAgents

is part of Plan 030.

⸻

8. Companion Runtime Lifecycle

Normal lifecycle:

macOS login
    ↓
Companion starts
    ↓
CoreBluetooth becomes poweredOn
    ↓
look for already-connected Cardputer
    ↓
attach locally
    ↓
discover Companion GATT
    ↓
subscribe
    ↓
HELLO
    ↓
HELLO_ACK
    ↓
capabilities
    ↓
initial active application
    ↓
session READY
    ↓
◆ appears on Cardputer

The process stays alive even if Cardputer is absent.

Absence of Cardputer is a normal state.

⸻

9. Re-Attach Strategy

The selected Mac is expected to already own the BLE connection because Cardputer is connected as a HID device.

The Companion does not perform general BLE discovery in Plan 030.

Primary lookup:

retrieveConnectedPeripherals(
    withServices: [COMPANION_SERVICE_UUID]
)

If the Cardputer is found, the application establishes its own local CoreBluetooth connection to that already system-connected peripheral and discovers the Companion service.

If none is found, sequentially probe already-connected BLE HID peripherals
(`0x1812`) and attach only after Companion GATT service discovery confirms a
Cardputer. Raw HID count is not a Cardputer count: other keyboards and
trackpads must not produce `ambiguous` or a blind attach.

If no confirmed Cardputer is found:

Companion remains running
        ↓
wait
        ↓
retry connected-peripheral lookup

CoreBluetooth callbacks are filtered by the current peripheral identifier,
attach phase, and attempt generation. A late callback from a cancelled HID
candidate must not cancel the next probe. A retry to the same peripheral waits
for `didDisconnect` / `didFailToConnect` before starting the next attempt. If
that callback never arrives, cancellation timeout discards the
`CBCentralManager` and looks up again instead of ignoring the next disconnect
on the new attempt. Delegate callbacks are accepted only from the current
central instance.

Recommended initial retry interval:

3 seconds

No aggressive busy loop.

No general scan.

No new pairing flow.

No second Cardputer identity.

⸻

10. Single-Device Contract

Plan 030 supports exactly one Cardputer Companion device per Mac.

Behavior:

0 confirmed Cardputers
    → Waiting
1 confirmed Cardputer
    → Attach
>1 confirmed Cardputers
    → Ambiguous
    → Do not choose automatically

Confirmed means the Companion GATT service is present. Discovery, notify, and
handshake failures clear the local peripheral and retry; they must not leave
`lookup()` stuck because a failed candidate is still held.

No device selector is implemented.

Multi-device support is explicitly outside scope.

⸻

11. Existing BLE Relationship

Expected state before Companion attaches:

Cardputer
    ↓
selected HostProfile = WORK MAC
    ↓
Mac already bonded
    ↓
BLE encrypted
    ↓
BLE authenticated
    ↓
HID READY

Then:

Cardputer Companion.app
    ↓
attaches to the same peripheral
    ↓
discovers custom Companion GATT service

The Companion must not require:

second pairing
new HostProfile
different bond
separate BLE physical connection

Existing behavior must remain intact:

host selection
bond persistence
pairing
BLE reconnect
HID subscriptions
keyboard HID
consumer HID

⸻

12. GATT Service

Register one project-owned 128-bit primary service.

Generate UUIDs once and commit them as protocol constants.

Service:

CARDPUTER_COMPANION_SERVICE

Characteristics:

HOST_TO_DEVICE
    Write With Response
DEVICE_TO_HOST
    Notify

Conceptually:

Cardputer                         macOS
DEVICE_TO_HOST ───── Notify ───────→
HOST_TO_DEVICE ←──── Write ─────────

Semantic operations do not receive dedicated characteristics.

Do not create:

PING_CHARACTERISTIC
APP_ACTIVE_CHARACTERISTIC
APP_ACTIVATE_CHARACTERISTIC

The GATT layer transports protocol messages only.

⸻

13. GATT Security

Companion traffic is accepted only when the current BLE peer is:

connected
bonded
encrypted
authenticated
selected

Specifically:

current bond == selected bond

Reject Companion access from:

unbonded peer
unselected peer
pairing-only peer
unauthenticated peer
stale peer

GATT characteristics must require the same strong authenticated encrypted BLE security already required by the firmware.

A valid BLE connection alone is insufficient to create COMPANION.

⸻

14. GATT Service Cache Validation

Plan 030 adds a new GATT service to devices that may already be paired with macOS.

Physical validation must explicitly test:

existing firmware
        ↓
Mac already paired
        ↓
flash Plan 030 firmware
        ↓
keep existing bond
        ↓
Mac reconnects
        ↓
Companion discovers new GATT service

Re-pairing is not accepted as the default solution.

If macOS fails to discover the new service because of GATT caching, Plan 030 must address Service Changed / GATT database-change behavior correctly before completion.

⸻

15. Transport Framing

Do not assume a large ATT MTU.

ICompanionTransport operates on complete logical protocol messages.

The BLE implementation handles fragmentation and reassembly underneath it.

Conceptual frame:

+------------+-------------+-------------+----------------+
| message ID | chunk index | chunk count | payload bytes  |
+------------+-------------+-------------+----------------+

Initial limits:

maximum logical message:   256 bytes
maximum chunks:             16
reassembly timeout:          2 seconds

Requirements:

bounded allocation
bounded queue
deterministic reassembly
duplicate chunk detection
invalid index rejection
inconsistent chunk-count rejection
oversized message rejection
abandoned frame timeout

Partial protocol payloads must never reach CompanionService.

⸻

16. ICompanionTransport

Introduce a transport-neutral boundary.

Conceptually:

enum class CompanionTransportState {
    Unavailable,
    Ready,
};
class ICompanionTransport {
public:
    CompanionTransportState state() const;
    CompanionSendResult send(...);
    std::optional<CompanionMessage> receive(...);
};

Exact names may follow existing project conventions.

Below this interface:

GATT
ATT
characteristics
BLE peer handles
chunking
reassembly
notification subscription

Above this interface:

protocol version
session
request IDs
operations
capabilities
events
timeouts
heartbeat

⸻

17. Protocol v1

Protocol v1 is:

binary
bounded
allowlisted
versioned
session-aware

Envelope contains:

version
kind
session generation
request ID
operation
payload

Supported kinds:

HELLO
HELLO_ACK
REQUEST
RESPONSE
EVENT

Do not use:

JSON
free-form dictionaries
reflection RPC
arbitrary command strings
arbitrary scripts

in the BLE wire format.

⸻

18. Session Handshake

The Mac initiates the application-level handshake after subscribing to DEVICE_TO_HOST.

Sequence:

Mac
 │
 │ HELLO
 │ supported protocol versions
 ▼
Cardputer
 │
 │ HELLO_ACK
 │ protocol = 1
 │ new session generation
 ▼
Mac
 │
 │ capabilities response
 ▼
Cardputer

A new handshake always creates a new session generation.

Old-session messages are invalid.

CompanionService becomes Ready only after:

secure selected BLE peer
+
GATT transport ready
+
compatible protocol
+
valid handshake
+
capability exchange

⸻

19. Session Generation

Every application-level connection has a generation.

Example:

session 41
    ↓ disconnect
session 42
    ↓ reconnect

Messages belonging to 41 must never affect 42.

On session replacement:

all old pending requests fail
old event stream is invalid
old responses are ignored
live capabilities are cleared

No request replay.

⸻

20. Request IDs

Each request receives an identifier scoped to the active session.

Initial limit:

maximum outstanding requests: 4

A RESPONSE must match:

current session
existing request ID
expected operation

Reject:

unknown ID
duplicate response
old-session response
wrong-operation response
malformed response

Initial semantic request timeout:

2 seconds

Timeout must not trigger:

automatic replay
HID fallback
another-host fallback

⸻

21. CompanionService State

Suggested internal states:

Unavailable
Attaching
Handshaking
Ready
ProtocolError

ProtocolError is local to the Companion session.

It must not transition the global Bluetooth subsystem into Error.

CompanionService owns:

handshake
protocol version
session generation
request IDs
pending requests
request timeout
heartbeat
live capabilities
EVENT validation
response validation

It does not own:

BLE hardware
bond persistence
host selection
GATT chunks
Home UI
macOS APIs

⸻

22. Heartbeat and Liveness

BLE/HID may remain connected even if the macOS Companion process crashes.

Therefore:

BLE connected != Companion alive

Use application-level heartbeat.

Initial timing:

heartbeat interval: 3 seconds
session dead after: 9 seconds without successful heartbeat

Normal:

Cardputer → ping
Mac       → pong

A successful heartbeat refreshes session liveness.

If liveness expires:

pending semantic requests fail
        ↓
live capabilities clear
        ↓
COMPANION removed
        ↓
◆ disappears

HID remains unaffected.

⸻

23. Immediate Disconnect Handling

Heartbeat is a fallback.

Where a definitive lower-level event exists, react immediately.

Examples:

BLE PeerDisconnected
selected host changed
Bluetooth disabled
security becomes invalid
GATT transport unavailable

These immediately invalidate the Companion session.

Do not wait nine seconds if the underlying BLE relationship is already known to be gone.

⸻

24. Companion Crash

Example:

BLE/HID still connected
        ↓
Companion process crashes
        ↓
no more protocol responses
        ↓
heartbeat expires
        ↓
session invalid
        ↓
◆ disappears

The HID transport remains usable.

If ServiceManagement relaunches the Companion:

Companion starts
    ↓
re-attaches
    ↓
new handshake
    ↓
new session
    ↓
◆ returns

⸻

25. Mac Sleep / Power Loss / Range Loss

Example:

Mac sleeps
or
Mac powers off
or
Mac leaves BLE range

Expected firmware sequence:

BLE disconnect
    ↓
Companion transport unavailable
    ↓
Companion session invalid immediately
    ↓
COMPANION removed
    ↓
◆ disappears

When Mac returns:

existing Bluetooth reconnect logic
        ↓
secure selected BLE connection
        ↓
HID READY
        ↓
running Companion discovers connected peripheral
        ↓
new handshake
        ↓
new session
        ↓
◆ returns

No manual Companion restart.

No manual Cardputer reconnect.

⸻

26. Selected Host Change

Example:

WORK MAC ● ◆

User switches Cardputer to:

PERSONAL MAC

Immediately:

WORK MAC Companion session invalid
pending requests fail
COMPANION removed
◆ disappears

Only a successful session from the newly selected host may restore:

◆

A Companion session is always scoped to the selected HostProfile.

⸻

27. Protocol Operations

27.1 ping

Purpose:

health check
heartbeat
request-response verification

Use a small bounded token.

No system details are exposed.

⸻

27.2 capabilities

Returns live semantic capabilities supported by this Companion session.

Initial capabilities:

APP_ACTIVE
APP_ACTIVATE
APP_ACTIVE_EVENTS

Use stable numeric protocol identifiers.

These are live capabilities.

Do not persist them into HostProfile.

⸻

27.3 app.active

Returns the current foreground application’s bundle identifier.

Example:

dev.zed.Zed

If there is no suitable bundle identifier:

NOT_AVAILABLE

Do not invent an identifier.

This request remains useful for:

initial synchronization
recovery
debug/testing
explicit refresh

⸻

27.4 app.activate

Input:

bundle identifier

Example:

org.telegram.desktop

Initial maximum:

128 UTF-8 bytes

Behavior:

if app is already running:
    activate it
else if installed app can be resolved:
    launch and activate it
else:
    return NOT_FOUND

Accepted input is only:

bundle identifier

Not accepted:

filesystem path
executable
shell command
AppleScript
URL
arguments
environment variables

⸻

28. Protocol EVENT Model

Plan 030 implements one outbound event:

APP_ACTIVE_CHANGED

Conceptual EVENT:

kind: EVENT
session: 42
operation: APP_ACTIVE_CHANGED
payload:
    bundle identifier

Events:

belong to the active session
are validated
are length-bounded
are not request-correlated
must not be replayed after reconnect

Old-session events are ignored.

No generic arbitrary event bus is introduced.

⸻

29. Foreground Application Observation

The macOS Companion observes application activation through AppKit.

On session establishment:

read current foreground application
        ↓
send initial active-application state

Then observe application changes:

macOS foreground changes
        ↓
NSWorkspace activation notification
        ↓
resolve bundle identifier
        ↓
APP_ACTIVE_CHANGED EVENT
        ↓
Cardputer

No polling loop is used for foreground application tracking.

Duplicate unchanged application identifiers should not generate repeated events.

This is specifically groundwork for Plan 031:

Telegram foreground
    ↓
Telegram Cardputer profile
Zed foreground
    ↓
Zed Cardputer profile

⸻

30. macOS Application Boundary

Introduce a Swift abstraction, conceptually:

ApplicationController
├── activeApplication()
├── activate(bundleIdentifier)
└── observeActiveApplication()

Implementation uses AppKit.

It must be injectable or mockable.

Protocol and Bluetooth tests must not need to launch real user applications.

Plan 030 must not require:

Accessibility
Automation
clipboard
screen recording

permissions.

⸻

31. Runtime COMPANION Capability

Use the existing CapabilityRegistry.

COMPANION exists only while all conditions are true:

selected host exists
+
selected peer is connected
+
peer is bonded
+
peer is encrypted/authenticated
+
Companion GATT is ready
+
protocol handshake succeeded
+
protocol version is compatible
+
live capability exchange succeeded
+
heartbeat is healthy

COMPANION is removed as soon as the condition becomes false.

It is runtime-only.

Never persist it.

⸻

32. Home Indicator

Add one passive Companion indicator next to selected-host status.

Conceptually:

WORK MAC                 ● ◆
READY

Meaning:

●  existing BLE/HID state
◆  live Companion session

Only two resting Companion states exist on Home:

Companion Ready
    → ◆ visible
Anything else
    → ◆ absent

Do not display:

COMPANION OFF
WAITING FOR COMPANION
COMPANION ERROR
COMPANION CONNECTING

on Home.

Absence is normal.

The indicator:

does not create a new row
does not create a footer
does not create a popup
does not animate continuously
does not disturb the Home ambient wave

Use existing dirty-region rendering.

⸻

33. Failure Isolation

These failures:

Companion absent
Companion crash
protocol mismatch
heartbeat timeout
malformed packet
unsupported operation
application not found
request timeout

must never:

disable Bluetooth
clear a bond
change selected host
break HID
navigate Home
open a blocking modal
reboot firmware
enter global firmware error state

They affect only:

specific request
or
Companion session

⸻

34. Security

The protocol is allowlisted.

Initial operations:

PING
CAPABILITIES
APP_ACTIVE
APP_ACTIVATE
APP_ACTIVE_CHANGED

There is no general execution primitive.

Explicitly prohibited:

SHELL_EXEC
SCRIPT_EXEC
OSASCRIPT
EVAL
EXECUTABLE_PATH
ARBITRARY_FILE
ARBITRARY_URL
ARBITRARY_COMMAND

Every message validates:

size
version
kind
session
request ID where applicable
operation
payload length
operation-specific payload

All queues and buffers are bounded.

Normal logs must not contain:

raw protocol payload
bond identity
BLE peer identity
authentication material
application bundle identifiers
private host data

Developer-only diagnostics must be explicitly separated from normal logging.

⸻

35. Protocol Fixtures

Store shared binary protocol fixtures in:

protocol/companion/fixtures/

At minimum:

hello-v1.bin
hello-ack-v1.bin
ping-request-v1.bin
ping-response-v1.bin
capabilities-request-v1.bin
capabilities-response-v1.bin
app-active-request-v1.bin
app-active-response-v1.bin
app-activate-request-v1.bin
app-activate-response-v1.bin
app-active-changed-event-v1.bin
malformed-length.bin
unsupported-version.bin
wrong-session.bin
unknown-operation.bin

Both Swift and C++ tests consume the same bytes.

Protocol drift between firmware and Mac must fail CI.

⸻

36. Firmware Tests

BLE transport

Test:

Companion GATT registration
secure characteristic requirements
selected peer accepted
unselected peer rejected
unbonded peer rejected
unauthenticated peer rejected
transport unavailable on disconnect
transport invalidated on host change

Fragmentation

Test:

single chunk
multiple chunks
maximum message
duplicate chunk
missing chunk
invalid chunk index
inconsistent chunk count
oversized message
reassembly timeout
message-ID rollover

Protocol codec

Test:

HELLO
HELLO_ACK
REQUEST
RESPONSE
EVENT
all v1 operations
fixture compatibility
unsupported version
unknown operation
malformed payload
invalid length
oversized bundle identifier

CompanionService

Test:

handshake
capability negotiation
Ready transition
request correlation
request timeout
duplicate response
stale response
stale event
new session invalidates old pending requests
heartbeat success
heartbeat expiry
immediate BLE disconnect
selected-host change
capability registration
capability removal

Regression

Existing:

keyboard HID
consumer HID
pairing
host switching
reconnect

tests must remain green.

⸻

37. macOS Tests

Test:

protocol fixtures
encoder / decoder
HELLO handshake
session replacement
request dispatch
response generation
EVENT generation
invalid session
invalid operation
invalid payload
heartbeat response
app.active
app.activate running app
app.activate resolvable app
app.activate missing app
foreground-app change
duplicate foreground app suppression
CoreBluetooth state transitions
attach retry state machine

macOS API access should sit behind mocks/adapters where possible.

⸻

38. CI

Existing Linux jobs remain.

Add:

companion-checks

running on:

macos-15

Validate at minimum:

build Cardputer Companion.app
run Swift/unit tests
validate protocol fixtures

Final CI aggregation requires:

host-checks
firmware-build
companion-checks

The Linux firmware/host jobs must not be made dependent on AppKit/CoreBluetooth tooling.

⸻

39. Physical Acceptance — Upgrade Existing Paired Device

Start with:

current pre-030 firmware
Mac already paired
HID working

Then:

flash 030 firmware
do not remove the bond

Verify:

Mac reconnects normally
HID still works
Companion discovers newly-added custom GATT service
no re-pair is required

If this fails because of cached GATT state, fix service-change handling before closing Plan 030.

⸻

40. Physical Acceptance — Normal Startup

1. Cardputer is paired with the Mac.
2. Selected host is the Mac.
3. BLE reconnects.
4. Keyboard HID works.
5. Consumer HID still works.
6. Launch Cardputer Companion.app.
7. Grant Bluetooth permission when macOS asks.
8. Companion finds the already-connected Cardputer.
9. No second pairing dialog appears.
10. Companion discovers the custom GATT service.
11. HELLO/HELLO_ACK succeeds.
12. Capability negotiation succeeds.
13. Cardputer receives current active application.
14. COMPANION becomes available.
15. ◆ appears on Home.

⸻

41. Physical Acceptance — Foreground App Events

With Companion ready:

open Zed

Verify Cardputer receives:

APP_ACTIVE_CHANGED(dev.zed.Zed)

Switch to another application.

Verify another event arrives.

Verify:

no polling is required
duplicate unchanged app state is suppressed
bundle IDs are not printed by normal firmware logs

⸻

42. Physical Acceptance — Companion Crash

Start from:

BLE ●
Companion ◆

Kill the Companion process unexpectedly.

Verify:

BLE remains connected
HID remains functional
heartbeat expires
◆ disappears within ~9 seconds

Verify ServiceManagement relaunch behavior as applicable.

After Companion returns:

automatic attach
new session
◆ returns

No Cardputer interaction is required.

⸻

43. Physical Acceptance — Mac Sleep / Disconnect

With Companion active:

put Mac to sleep

or disconnect BLE.

Verify immediately:

BLE state changes
Companion session invalidates
◆ disappears

Wake Mac.

Verify:

BLE reconnects
HID becomes ready
running Companion automatically re-attaches
new session is established
◆ returns

No manual Companion restart.

No re-pair.

⸻

44. Physical Acceptance — Host Switching

Starting:

WORK MAC ● ◆

Switch selected host.

Verify:

old session invalid immediately
pending old requests fail
old ◆ disappears

No old request may execute after another host becomes selected.

⸻

45. Physical Acceptance — Login Startup

Register Companion startup.

Log out and back in, or reboot as appropriate.

Verify:

Cardputer Companion.app starts automatically
no Dock icon appears
no UI opens
CoreBluetooth initializes
Cardputer is attached when available
session establishes automatically
◆ appears

If Cardputer is initially absent:

Companion remains alive

Connect/reconnect Cardputer later.

Verify:

Companion attaches without restart

⸻

46. Documentation Updates

Update:

docs/ARCHITECTURE.md
docs/UI_REQUIREMENTS.md
docs/plans/README.md
README.md

Document:

Companion architecture
BLE/GATT ownership
protocol location
session/liveness semantics
Home ◆ meaning
how to build Companion.app
how to launch it initially
Bluetooth permission
login startup
physical acceptance

Phase 8 should become partially delivered:

Companion foundation complete
MAC CONTROL pending

⸻

47. Explicit Plan 031 Boundary

Plan 031 implements:

MAC CONTROL

Already agreed UX:

3 × 2
6 visible hotkeys
large key letter
short label
no cursor
no scrolling
unmapped keys do nothing
Escape → Launcher

Plan 031 introduces application profiles:

foreground bundle ID
        ↓
matching profile
        ↓
current layer
        ↓
six active hotkeys

Example:

Telegram
├── Layer 1
│   └── 6 mappings
└── Layer 2
    └── 6 mappings
Zed
├── Layer 1
└── Layer 2

APP_ACTIVE_CHANGED, delivered by Plan 030, drives automatic profile switching.

Plan 031 also introduces the user-facing host-control action pipeline:

physical key
    ↓
MAC CONTROL
    ↓
profile + layer mapping
    ↓
logical Action
    ↓
HostControlService
   ↙              ↘
HID            Companion

None of that UI or mapping logic belongs in Plan 030.

⸻

48. Recommended Implementation Order

01. Finalize protocol v1 constants and UUIDs
02. Add shared binary fixtures
03. Extend Bluetooth adapter/GATT implementation
    without introducing a second BLE event owner
04. Add Companion transport view to BluetoothService
05. Implement BLE fragmentation/reassembly
06. Implement firmware protocol codec
07. Implement CompanionService handshake/session model
08. Implement request IDs / correlation / timeout
09. Implement heartbeat and immediate disconnect invalidation
10. Add runtime COMPANION capability
11. Add Home ◆ indicator
12. Create Cardputer Companion.app
13. Add CoreBluetooth attached-peripheral lookup
14. Add automatic re-attach state machine
15. Implement Swift protocol v1
16. Implement ping / capabilities
17. Implement ApplicationController
18. Implement app.active
19. Implement app.activate
20. Implement foreground-app observation
21. Implement APP_ACTIVE_CHANGED EVENT
22. Add login startup with SMAppService
23. Add firmware native tests
24. Add Swift tests
25. Add macOS CI
26. Run firmware-check / firmware-size
27. Validate GATT cache upgrade scenario
28. Run full physical lifecycle validation
29. Update architecture and documentation

⸻

49. Completion Criteria

Plan 030 is complete only when:

[ ] Companion custom GATT service is registered beside HID
[ ] BluetoothService remains the single owner of BLE lifecycle/events
[ ] existing HID works unchanged
[ ] secure selected-host filtering is enforced
[ ] existing paired Mac discovers Companion GATT after firmware upgrade
    without re-pairing
[ ] Cardputer Companion.app builds
[ ] Companion is headless
[ ] Companion has stable macOS Bluetooth permission identity
[ ] SMAppService startup is implemented
[ ] Companion launches automatically after login
[ ] Companion can attach to already system-connected Cardputer
[ ] no general BLE scan is required
[ ] automatic attach retry works
[ ] protocol v1 HELLO/HELLO_ACK works
[ ] session generation works
[ ] stale sessions are rejected
[ ] request IDs and response correlation work
[ ] request timeout works
[ ] heartbeat works
[ ] Companion crash is detected while HID remains connected
[ ] physical BLE disconnect invalidates session immediately
[ ] session automatically recovers after BLE reconnect
[ ] selected-host change invalidates old session immediately
[ ] old requests are never replayed
[ ] capabilities works
[ ] app.active works
[ ] app.activate works
[ ] APP_ACTIVE_CHANGED EVENT works
[ ] foreground application updates require no polling
[ ] live COMPANION runtime capability is correct
[ ] Home ◆ exactly reflects live Companion readiness
[ ] stopping Companion does not break HID
[ ] Swift and C++ consume common protocol fixtures
[ ] firmware native tests pass
[ ] Swift/macOS tests pass
[ ] host-check passes
[ ] firmware-check passes
[ ] firmware-size passes
[ ] companion-checks CI passes
[ ] physical acceptance passes
[ ] documentation is updated

After this plan, Plan 031 may implement MAC CONTROL almost entirely as a user-facing Mini App and action-routing feature, without introducing a new host transport, macOS protocol, foreground-app polling mechanism, or Companion lifecycle model.