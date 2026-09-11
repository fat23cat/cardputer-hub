# Cardputer Hub Architecture

## 1. Purpose

Cardputer Hub is an extensible software platform for the M5Stack Cardputer-Adv.

The device should provide a reusable system core, connectivity infrastructure, reusable Services, and independent Mini Apps.

The primary dependency direction is:

```text
Mini Apps
    ↓
Services
    ↓
Connectivity
    ↓
Hardware Adapters
```

System Core provides cross-cutting infrastructure used through stable
interfaces; it is not an additional application layer in this dependency chain.

The responsibilities are:

* **System Core** — lifecycle, application shell, navigation, configuration and storage interfaces, Actions, input routing, and common infrastructure.
* **Connectivity** — communication mechanisms such as Wi-Fi and Bluetooth.
* **Services** — reusable logic, state, integrations, and background behavior.
* **Mini Apps** — user-facing interfaces built on top of Services.

Core principle:

> A Mini App primarily provides UI and interaction. Reusable logic, data, state, and integrations belong in Services.

---

## 2. High-Level Architecture

```text
┌──────────────────────────────────────┐
│              MINI APPS               │
│                                      │
│ Device Manager                       │
│ Weather                              │
│ VPS Monitor                          │
│ Media                                │
│ Telegram                             │
│ LED Control                          │
│ Settings                             │
│ ...                                  │
└──────────────────┬───────────────────┘
                   │
                   ▼
┌──────────────────────────────────────┐
│               SERVICES               │
│                                      │
│ HostService                          │
│ WeatherService                       │
│ VpsService                           │
│ TelegramService                      │
│ MediaService                         │
│ IndicatorService                     │
│ ConfigurationService                 │
│ RemoteControlService                 │
│ ...                                  │
└──────────────────┬───────────────────┘
                   │
                   ▼
┌──────────────────────────────────────┐
│            CONNECTIVITY              │
│                                      │
│ WiFiService                          │
│ BluetoothService                     │
│                                      │
│ Future: USB / MQTT / LoRa / etc.     │
└──────────────────┬───────────────────┘
                   │
                   ▼
┌──────────────────────────────────────┐
│          HARDWARE ADAPTERS           │
│                                      │
│ Cardputer Keyboard                   │
│ Display                              │
│ ESP32 BLE                            │
│ ESP32 Wi-Fi                          │
│ Puzzle RGB 8×8                       │
│ Battery                              │
│ Internal NVS                         │
│ microSD                              │
└──────────────────────────────────────┘
```

System Core provides cross-cutting facilities to these layers:

```text
System Core
├── Boot / Lifecycle
├── Application Shell
├── Launcher
├── App Registry
├── Navigation
├── Input Routing
├── Action Bus
├── Configuration interfaces
├── Record and file-storage primitives
├── Logging
└── Capability Registry
```

---

## 3. System Core

System Core should remain independent of specific Mini Apps.

Responsibilities:

```text
Boot
Application lifecycle
Launcher
Navigation
Input routing
Global controls
Action Bus
App Registry
Configuration interfaces
Persistence primitives
Logging
Capabilities
```

System Core must not contain application-specific logic such as:

```text
weather API implementation
Telegram Bot API implementation
VPS API calls
Mac-specific application mappings
WS2812 animation implementation
```

---

## 4. Application Shell

Application Shell provides the common device UI.

Responsibilities include:

```text
Home / Launcher
Status information
Global shortcuts
Opening and closing Mini Apps
Back navigation
System dialogs
```

The shell must not know the internal implementation of Mini Apps.

---

## 5. Home / Launcher

Home is both a lightweight system dashboard and the Mini App launcher.

Example:

```text
┌────────────────────────┐
│ 10:21      ☀ 27°  81%  │
│ WiFi ●     MacBook ●   │
├────────────────────────┤
│ > Device Manager       │
│   Weather              │
│   VPS Monitor          │
│   Media                │
│   LED Control          │
│   Settings             │
└────────────────────────┘
```

Home may consume state from Services:

```text
WeatherService    → temperature
HostService       → active host
WiFiService       → connectivity
IndicatorService  → status
```

Home must not implement these functions itself.

The initial Home dashboard is implemented under plan 017. It displays actual
HostService state, a compact Micro 5 host label, a single Wi-Fi status, and
battery telemetry. It does not act as the future AppRegistry-driven Launcher.
`ApplicationShell` owns a NavigationStack rooted at `home`; Tab (also G0 or Fn+Tab) routes
`ui.settings` through ActionBus to a general Settings list. Its Bluetooth entry
routes `ui.bluetooth` to the existing HostSettings view. `ui.back` dismisses a
local modal before returning Home; navigation never changes radio policy.
Full Launcher and Mini App lifecycle remain pending.

`BatteryService` owns a read-only optional estimated percentage and samples
`IBatteryAdapter` immediately on its first update, then at most once every five
seconds of injected monotonic elapsed time. Invalid/unavailable readings clear
the value; delayed updates never cause catch-up sampling bursts. The Cardputer
adapter obtains the estimate from the pinned M5Unified power driver. The main
composition passes the snapshot into the shell; drawing code does not read
hardware or manage polling. This does not add a BLE battery service.

The clock slot displays `--:--` pending a time source. Normal firmware does not
yet compose WiFiService, so its status is explicitly OFFLINE without an SSID.
Future clock and Wi-Fi indicators must consume their owning Services or
Connectivity state. Battery percentage is a voltage-derived estimate, especially
while externally powered, rather than a calibrated charge gauge.

Home's approved ambient dotted wave is presentation state owned by the shell:
a 28-second cycle, at most two updates per second, using injected elapsed time
and only the lower 36 rows. It pauses while Home is hidden. Static labels and
icons have separate dirty regions; menus and pairing do not animate this wave.

The display contract adds optional `beginFrame`/`endFrame` grouping. The Cardputer
adapter lazily creates a 240×135 RGB565 canvas after board initialization, tracks
drawing damage, and presents only the completed clipped region at frame end.
No drawing and no active transition means no transfer. `SlideTransition` in
System Core provides bounded, testable 220 ms cubic timing and pixel composition
for interruption. Views request forward/backward presentation through the
optional display transition contract, before replacing page content; the shell
supplies monotonic elapsed time each frame. UI navigation semantics remain in
the shell/HostSettings, and LCD snapshot memory and presentation remain in the
adapter. A transient second RGB565 buffer holds outgoing pixels. It is freed
when the slide finishes; allocation failure falls back to immediate completed
presentation. New navigation during a slide snapshots the currently visible
composition, keeping input responsive without jumping to a hidden destination.
Multiple navigation Actions in one input poll retain one source snapshot and
target the final page. Direct drawing remains available for startup,
validation harnesses, and allocation failure. UI objects retain ownership of
layout and view state; the adapter owns canvas memory and physical LCD I/O.

---

## 6. App Registry

Phase 1 provides System Core's hardware-independent, metadata-only
`AppRegistry`. Each `AppDescriptor` owns a non-empty exact ID, display name,
opaque entry route, an optional opaque icon ID, and an ordered collection of
required capability IDs. An empty icon ID selects a future text-only fallback.
Capability requirements must be non-empty and unique within a descriptor.

Registration validates and copies descriptors, rejects exact duplicate app
IDs without replacing the original, and preserves successful registration
order as the default future Launcher order. App IDs and required capability
IDs are case-sensitive. Static composition is the initial model, so the
registry has no unregister operation.

The registry does not contain Mini App instances, factories, lifecycle
callbacks, views, enabled-app configuration, rendering behavior, or Launcher
policy. It also does not inspect `CapabilityRegistry`. Phase 4 Launcher code
will enumerate metadata without hardcoded knowledge of individual apps, and
Phase 5 will integrate descriptors with Mini App instances and capability
eligibility. Registering metadata alone does not make an app operational.

Future Mini Apps register metadata through the shared `AppRegistry`.

Example:

```text
AppRegistry
├── DeviceManagerApp
├── WeatherApp
├── VpsMonitorApp
├── MediaApp
├── TelegramApp
├── LedControlApp
└── SettingsApp
```

The Launcher discovers available apps through this registry.

Adding a new Mini App should not require modifications to Launcher logic.

---

## 7. Mini App Model

Each Mini App should declare metadata such as:

```text
id
name
icon
required capabilities
entry view
```

Phase 1 represents this declaration with `AppDescriptor` only. Its entry route
remains opaque until Application Shell and Mini App integration exist, and
its icon may be absent without requiring an icon asset or rendering contract.
`IMiniApp`, instances, lifecycle, and view behavior remain Phase 5 work.

Example:

```text
WeatherApp

id: weather

requires:
  - WIFI
  - WEATHER_SERVICE
```

Another example:

```text
DeviceManagerApp

id: devices

requires:
  - BLUETOOTH
  - HOST_SERVICE
```

---

## 8. Thin Mini Apps

Mini Apps should primarily:

```text
read Service state
render UI
handle user input
invoke Actions
invoke Service operations
```

Mini Apps should not directly:

```text
open network sockets
manage Wi-Fi
perform BLE pairing
call Telegram APIs
implement VPS protocols
write WS2812 pixels
own global configuration
```

Correct dependency:

```text
WeatherApp
    ↓
WeatherService
    ↓
WiFiService
    ↓
network adapter
```

---

## 9. Views and Navigation

A Mini App may contain multiple internal Views.

Examples:

```text
WeatherApp
├── Current
└── Forecast
```

```text
VpsMonitorApp
├── Servers
├── Server Details
└── Services
```

```text
DeviceManagerApp
├── Hosts
├── Add Device
├── Host Details
└── Host Configuration
```

Navigation should support a stack.

Example:

```text
Launcher
   ↓
VPS Monitor
   ↓
Server
   ↓
Service Details
```

Back navigation reverses that path.

`View` is internal to a Mini App. Global navigation infrastructure belongs to System Core.

Phase 1 provides System Core's hardware-independent `NavigationStack` history
primitive. It owns opaque, case-sensitive route identifiers and supports
reset, push, current-route inspection, and root-preserving Back traversal. It
does not interpret route syntax, carry route parameters, render a destination,
activate application lifecycle, or restore view state.

The initial application shell now integrates this history with Home, a general Settings menu,
Bluetooth settings, and navigation Actions. Tab (also G0 or Fn+Tab) dispatches `ui.settings` and
the Settings Bluetooth entry dispatches `ui.bluetooth`; opening either menu has
no host-control side effects. The existing BLE list keeps Esc Home; Tab, G0 or Fn+Tab can
return from that list to Settings. Host submenus and editing/pairing modals consume
the shortcut without abandoning their state. Ordinary Enter/B on Home do
nothing. The hardware layout exposes Tab on the Fn layer while the shell owns
its meaning. The Cardputer input adapter additionally emits a local
`NamedKey::SystemMenu` on the debounced G0/BtnA press edge, independently of
keyboard-matrix initialization. Shell routes it to the same Settings Action;
it never becomes host input or repeats while held. The ROM download behavior
of G0 at boot/reset is unchanged. Plain Tab is scoped to built-in system
screens; future text-entry Mini Apps must retain their normal Tab behavior. Launcher and broader global-shortcut integration remain Phase 4 work. Phase 5 will define Mini
App view objects and lifecycle; those view implementations remain outside the
navigation history primitive.

All future System UI and Mini App views must follow the shared visual,
interaction, sound, display-power, and input-routing rules in
[`UI_REQUIREMENTS.md`](UI_REQUIREMENTS.md). Those presentation requirements do
not override the layer ownership defined here: UI renders state and expresses
user intent, while Services, Connectivity, and hardware adapters retain their
respective behavior and device responsibilities.

---

## 10. Connectivity Layer

Initial connectivity consists of:

```text
Connectivity
├── WiFiService
├── BluetoothService
└── BLE HID transport
```

Additional mechanisms may later include:

```text
MQTT
WebSocket
LoRa
```

when needed.

Phase 2 delivers Wi-Fi connectivity and BLE as the only implemented
host-control HID transport. USB is used for power, firmware installation, and
diagnostics through the ESP32-S3 fixed USB Serial/JTAG peripheral. It is not a
host-control transport, and attaching or removing a cable never selects a
host, switches HID output, restarts Bluetooth, or changes a bond.

There is no transport selector, automatic fallback, channel preference, or
USB HID implementation. `BluetoothService` exposes its selected-host HID view
through `IHidTransport`. Future Services receive this hardware-neutral boundary
rather than depending on BLE hardware. `HostService` owns the active
`HostProfile` and supply its opaque BLE bond; device names and platforms remain
data, not Connectivity policy.

Future transport support remains an extension point: add an implementation of
`IHidTransport` under Connectivity with hardware behavior behind an adapter.
Introduce selection, session-bound transaction delivery, persistence, and UI
only when a second transport is actually required and has its own approved
plan and physical validation. Do not keep speculative routers, channel enums,
configuration fields, or dormant USB descriptors in the current firmware.
The cancelled USB and routing work is recorded in plans 016 and 017.

The shared HID boundary is `IHidTransport` under `connectivity/hid`. It accepts
only hardware-neutral six-key keyboard reports or one Consumer Page usage,
reports explicit `Unavailable`, `Starting`, `Ready`, `Busy`, and `Error`
states, and provides non-blocking checked send and release operations. Report
validation rejects keyboard rollover-error usages and duplicate non-zero keys
before an adapter is called. Text encoding, shortcut interpretation, logical
Action resolution, host selection, and mouse or NKRO reports remain above or
outside this boundary.

---

## 11. WiFiService

Phase 2 provides the hardware-independent `WiFiService` and `IWifiAdapter`
contract. `WifiNetworkConfig` owns the requested SSID and passphrase, and the
Service holds those credentials only while connection intent is active.

Responsibilities:

```text
connect
disconnect
reconnect
connection state
signal strength
network configuration
```

The public connection state is one of `Idle`, `Connecting`, `Connected`,
`RetryWaiting`, or `Error`. A request starts synchronously without waiting for
association or DHCP. Invalid input returns `InvalidConfig` without reaching the
adapter or replacing active intent; an adapter operation that cannot start
returns `AdapterError`. Initialization or connection-launch failure clears the
unstarted intent and its credentials, allowing a later request to retry without
a replacement disconnect. A valid replacement disconnects the previous target,
resets retry backoff, and starts immediately. If the previous target cannot be
disconnected, replacement stops with `AdapterError` and enters `Error` without
starting the new target. Explicit disconnect returns `Disconnected`, clears
future intent and Service-owned credentials, and returns to `Idle`. A hardware
disconnect failure instead returns `AdapterError` and enters `Error`, while
still clearing future Service retry intent and credentials.

SSIDs contain 1-32 bytes with no embedded NUL. Passphrases are empty for open
networks, 8-63 bytes for personal WPA, or exactly 64 hexadecimal digits for a
raw PSK; embedded NUL is always invalid.

`update(elapsed)` accepts monotonic elapsed duration from its future composition
owner and performs bounded polling without sleeping. An incomplete attempt
times out after 15 seconds. Ordinary failure or loss waits 1, 2, 4, 8, 16, and
then 30 seconds between attempts, remaining capped at 30 seconds. Success and
valid target replacement reset the next delay to one second. Initialization
and fatal adapter failures enter `Error`; ordinary connection failures remain
retryable. Signal strength in dBm is available only in `Connected` state and
only while the adapter can still retrieve a live access-point record; a link
loss between Service updates returns no value rather than a sentinel RSSI.

Calls are synchronous and single-threaded. `WiFiService` owns configuration
copies but holds a non-owning adapter reference and, when supplied, a non-owning
logger pointer; both injected objects must outlive the Service. The adapter must
not retain caller references. Fixed state and retry logs may be emitted through
the shared logger, but network identity, credentials, addresses, and traffic
contents must never be logged.

The initial foundation does not provide scanning, provisioning, captive-portal
handling, access-point mode, enterprise Wi-Fi, or automatic startup. It may
later expose shared networking infrastructure such as:

```text
HTTP client
DNS
future WebSocket support
```

Higher-level Services must not use ESP32 Wi-Fi APIs directly where this abstraction is sufficient.

---

## 12. BluetoothService

Phase 2 provides the hardware-independent `BluetoothService` and
`IBluetoothAdapter` lifecycle contract. `BluetoothDeviceConfig` owns the local
device name, and connection events expose only opaque adapter peer handles.

Construction has no hardware side effects. An explicit `enable` initializes
the adapter once for that enabled lifecycle and requests advertising without
blocking. Repeated enable calls are idempotent. An explicit `disable` cancels
all retry intent, requests that pending or active advertising stop, requests
disconnection of the current peer, and then shuts down the owned Bluetooth
host and disables the controller as a synchronous completion barrier. Shutdown
also closes connections whose callbacks are still queued. The adapter retains
exclusive ownership of the initialized controller: logical disable stops and
deinitializes the ESP-NimBLE host and disables the controller, while leaving
the controller initialized for repeatable re-enable. Re-enable creates a fresh
NimBLE host lifecycle. A
stop, disconnect, or shutdown failure is reported rather than claiming
successful disablement, and cleanup ownership is retained so a later disable
or enable can retry it. Calls are synchronous and single-threaded from the
caller's perspective, while adapter callbacks only copy bounded events for
later processing by `update(elapsed)`.

The public lifecycle state is one of `Disabled`, `Idle`, `Advertising`,
`Connected`, `RetryWaiting`, or `Error`. Only one peer may be current. Outside
an explicit pairing window, a newly connected peer must already have a bond;
unbonded and additional peers are disconnected without replacing the current
connection. For an admitted bonded peer, the Service explicitly requests
`IBluetoothAdapter.restoreBondSecurity(peer)`. The adapter verifies that the
bond exists and asks the central to restore encryption using its saved keys;
it accepts an already protected link or an already-running security procedure.
This operation is independent of new-pairing capacity and never opens a pairing
window or deletes bonds. Initiation failure enters Error with normal adapter
cleanup. HID remains Starting until security and both subscriptions are
confirmed by current-peer events. Advertising remains blocked while any
requested peer rejection is in flight; the one-second reconnect delay starts
only after the final rejected peer reports disconnection. An unexpected
disconnection likewise waits one second before advertising again. Retryable advertising failures wait 1, 2, 4,
8, 16, and then 30 seconds,
remaining capped at 30 seconds; successful advertising or connection resets
that backoff. Documented NimBLE resource-pressure, busy, and timeout advertising
results are retryable; invalid argument, invalid state, and other permanent
results are fatal. Fatal adapter failures shut down owned Bluetooth
resources and require a later explicit enable. Each asynchronous advertising
operation retains the non-identifying lifecycle generation that issued it.
Combined with definitive shutdown on disable, this prevents queued callbacks
from an older attempt from being relabeled as or reactivating a new attempt.

The ESP32 adapter normalizes NimBLE peer-event ordering: security, identity,
pairing challenges, and restored subscriptions may precede NimBLE's CONNECT
callback. It announces the live peer before forwarding that peer's first event
and ignores a later duplicate CONNECT without resetting HID state. Disconnect
or definitive shutdown clears this ordering state before a handle is reused.
A failed delayed CONNECT closes an already-announced peer. This normalization
does not bypass the Service's bond, target, pairing, or HID admission checks.

`BluetoothService` owns the hardware-independent authenticated-pairing policy.
`openPairing` creates one explicit 120-second Add Device window; its timer
starts only when advertising actually begins. Opening while connected first
disconnects the current peer without deleting its bond. During the window the
Service admits exactly one unbonded peer, rejects bonded and additional peers,
and publishes at most one generation-tagged `DisplayPasskey`, `EnterPasskey`,
or `ConfirmComparison` challenge. Responses must match the generation and
challenge kind; entered passkeys are exactly six decimal digits. Cancellation,
timeout, malformed security completion, and failed finalization reject the
incomplete peer before normal reconnect advertising resumes. A successful
completion must be encrypted, authenticated, bonded LE Secure Connections and
publishes exactly one opaque bond reference while retaining the connection.

If an admitted pairing peer disconnects before completion, its challenge is
cleared and advertising resumes within the original pairing deadline. This
does not remove bonds or open a fresh window. A cancelled or expired window
stays closed; reconnection then follows the normal bonded-peer policy.

Bond references are stable, ordered, opaque 128-bit values. The Service can
enumerate up to 16 bonds, select or clear a reconnect target, remove one bond,
or remove all. A selected target causes every other bonded peer to be rejected.
Changing targets disconnects the previous peer without deleting either bond.
Removing an active bond disconnects before deletion, and remove-all visits
every known bond and reports partial failure. Pairing, target replacement, and
active-bond deletion advance only through `update(elapsed)` callback events.
`HostService` owns the mapping from these references to `HostProfile`
data; Connectivity never stores host names, platforms, or user-specific cases.

The Service exposes its BLE `IHidTransport` implementation through
`hidTransport()` so the existing Bluetooth lifecycle `state()` remains
source-compatible. The HID view is unavailable without the current selected
bond. It becomes ready only when that link is encrypted, authenticated, bonded,
uses report protocol, and has subscriptions for both keyboard and consumer
input reports. Every send rechecks adapter readiness and targets only that
peer. Busy is retryable; adapter failure enters Bluetooth and HID error.
Changing the selected bond, opening pairing, removing an active bond, or
disabling Bluetooth requests both neutral reports before disconnect when the
selected link remains usable. A definite disconnect is already neutral and
does not queue reports for replay. Connection, subscription, and report state
is cleared for every new lifecycle, and stale-generation or wrong-peer
callbacks cannot restore it.

The Service holds non-owning adapter and optional logger references, which must
outlive it. Logs describe fixed lifecycle outcomes and never include device
names, peer handles, addresses, passkeys, comparison values, bond references,
or other identity data. Normal firmware composes Bluetooth through HostService.
It first initializes without advertising to reconcile saved bonds, then applies
the persisted active host and BLE enabled setting. Missing configuration defaults
to BLE Off. The local Hosts settings screen supports pairing and host selection;
Action-to-HID mapping remains Phase 7 work.

Outside explicit pairing, the production composition advertises only for its
selected bond. The ESP32 controller connection accept list contains that peer's
resolved identity; other peers are also rejected at the Service boundary.
Changing an advertising target or opening/closing pairing stops and restarts
advertising with the new filter. A filter setup error never falls back to an
unrestricted advertisement. `BluetoothStartup::Idle` and `advertise()` allow
HostService to install policy before the first advertisement.

Responsibilities:

```text
BLE initialization
advertising
pairing
bond management
connection
disconnection
reconnection
BLE HID transport
```

It must not contain user-specific concepts such as:

```text
Personal MacBook
Work MacBook
Telegram
VS Code
```

These belong to higher layers.

---

## 13. Services

Services contain reusable domain behavior and state.

Initial/future examples:

```text
Services
├── HostService
├── WeatherService
├── VpsService
├── TelegramService
├── MediaService
├── IndicatorService
├── ConfigurationService
└── RemoteControlService
```

A Service may depend on another Service or on lower-level Connectivity when the
relationship follows the documented dependency direction.

Examples:

```text
WeatherService
    ↓
WiFiService
```

```text
HostService
    ↓
BluetoothService
```

---

## 14. Service Lifecycle

Services should have a predictable lifecycle conceptually similar to:

```text
initialize
start
update
stop
```

The exact C++ API may differ.

Services may operate in the background independently of whether their Mini App is currently open.

Examples:

```text
WeatherService
    periodic refresh

HostService
    connection monitoring

TelegramService
    polling / event handling

IndicatorService
    LED animation updates
```

---

## 15. Service State

Each Service owns its state.

Examples:

```text
WeatherService
├── LOADING
├── READY
├── STALE
└── ERROR
```

```text
HostService
├── DISCONNECTED
├── CONNECTING
└── CONNECTED
```

Mini Apps render Service state rather than reimplementing state detection.

---

## 16. HostService

`HostService` provides the application-level representation of paired host devices.

`BluetoothService` knows about Bluetooth bonds and connections.

`HostService` knows about:

```text
Host Profiles
active host
display names
platform
capabilities
host configuration
host switching
```

Conceptually:

```text
Bluetooth bond
     ↓
HostProfile
```

### Implemented HostService contract

The initial implementation provides `start`, `update`, `selectHost`,
`setEnabled`, `renameHost`, `startPairing`, `cancelPairing`, and `deleteHost`. UI intentions
use the shared ActionBus: `host.select` (`id`), `host.bluetooth` (`enabled`),
`host.rename` (`id`, `name`), `host.pair`, `host.cancel-pairing`, and
`host.pair-response` (`generation`, `accepted`, optional six-digit `passkey`),
and `host.delete` (`id`).
IDs are positive 32-bit Action integers; names are printable ASCII, 1–24
characters, and cannot be entirely spaces. Unknown/invalid Actions are rejected.
Pairing responses must match the current challenge generation. Secrets and
pairing codes are displayed locally, never logged.

At startup, existing opaque bonds are imported once as `Host N` profiles without
advertising or deleting pairs. Profile IDs stay stable across rename/reboot.
New authenticated pairs are added and selected automatically. Cancelling or
expiring the two-minute pairing window restores the previous saved selection
and enabled state. Pairing temporarily permits a new peer; ordinary operation
permits only the selected bond.

Selection means persisted user intent, not proof of a live connection. Changing
host releases reports and shuts down the previous BLE lifecycle before saving
and advertising for the new target. BLE Off shuts down advertising and active
connections while retaining profiles and selection. The setting survives reboot.
An unavailable host never causes fallback to another laptop. A missing bond or
storage/adapter failure closes BLE and exposes a failure result; saved data is
not erased. Shutdown uses the adapter's bounded synchronous lifecycle barrier;
connection and pairing progress are driven by `update(elapsed)`. An immediate
Off after Idle initialization waits for the adapter's initial NimBLE sync before
stopping the stack. If that bounded wait expires, cleanup retains ownership and
reports failure; startup resources must not be freed while stage-2 initialization
still uses them.
The adapter also waits for that first synchronization before returning successful
initialization: with NimBLE STATIC_TO_DYNAMIC enabled, stage-2 privacy startup
reloads the bond store. Immediate registry queries must not observe its transient
empty state. This uses the completion signal and a bounded deadline, not a fixed
sleep. Host activation failures can emit non-identifying stage diagnostics through
the shared Logger; addresses, references, names, keys, and codes are excluded.
The UI labels persisted intent SELECTED, including while Off or after a fault;
READY alone confirms secured HID connectivity.

Enabling without an active host returns `HostSelectionRequired`, with no adapter
calls or configuration writes. The Bluetooth panel moves focus to the first saved
host, or to Add device when the list is empty, and asks for Enter to confirm.
It never selects a laptop or opens pairing merely by moving focus. Home keeps
showing OFF for this prerequisite and for invalid input; ERROR is reserved for
storage, Bluetooth, and missing-bond failures or an actual Bluetooth Error state.

`deleteHost(id)` removes only the named profile and its Bluetooth bond through
BluetoothService. The local host menu exposes Connect, Rename, and Delete;
opening it has no radio side effects and deletion requires confirmation.
Deleting the selected host first releases/disconnects it and persists Off, then
clears selection without fallback. Deleting an unselected host leaves the live
selected connection intact; BluetoothService completes that unrelated bond
removal without waiting for the current peer to disconnect. A missing bond can
still have its stale profile removed. Failures retain the profile for retry and
stop BLE; other profiles and the monotonic next-host ID are preserved. There is
no global delete UI or `host.forget-all` Action.

The accepted interaction is Cardputer-side host selection and BLE On/Off.
A computer's Disconnect button is not the persistent Off setting: macOS may
reconnect its system HID client while that host remains selected and BLE is On.
A future companion can reuse these logical operations through an authenticated
control API, but that API and its network/security lifecycle are not implemented.

## 17. Host Profiles

The delivered `HostProfile` contains `id`, `name`, and an opaque BLE `bond`.
Platform, capabilities, application configuration, and Action mappings remain
future profile extensions; no personal laptop is encoded in application logic.
The broader profile model should eventually contain information such as:

```text
id
display name
platform
Bluetooth bond reference
capabilities
application configuration
action mappings
```

Names such as:

```text
Personal MacBook
Work MacBook
iPad
Windows PC
```

are user data, not firmware concepts.

They must not be hardcoded as special cases.

---

## 18. Adding a Host

The current Hosts settings screen creates a default-named profile after
successful pairing and allows renaming with R. Platform and template editing
belong to the later full Device Manager flow:


```text
Device Manager
      ↓
Add Device
      ↓
HostService
      ↓
BluetoothService pairing mode
      ↓
Bluetooth pairing
      ↓
HostProfile created
      ↓
User configures:
  name
  platform
  optional template
      ↓
Save
```

Normal host switching must not require repeated pairing.

---

## 19. Multiple Hosts

The first supported real-world scenario requires at least:

```text
Personal MacBook
Work MacBook
```

The architecture must remain compatible with future support for:

```text
Windows PC
iPad
iPhone
Generic BLE HID device
```

---

## 20. Active Host

`HostService` maintains a global `activeHost`.

Example:

```text
activeHost = host-002
```

Services and Mini Apps may use the active host as a default target.

The Cardputer platform itself must remain useful when no host is connected.

For example:

```text
Weather       → no host required
VPS Monitor   → no host required
LED Control   → no host required
Device Control → host required
```

---

## 21. Device Manager Mini App

The full Device Manager is planned as a Mini App. The delivered minimal
`HostSettings` screen opens from Home and handles list navigation, BLE On/Off,
pairing prompts, selection, and renaming. It renders Service state and routes
logical Actions; it does not own pairing, persistence, or connection policy.
Its keys remain local and are not forwarded as HID input. The list maps plain
`;`/`.` input to Up/Down Actions without changing the shared keyboard translator;
text-entry views retain punctuation. A cached list view limits painting to changed
rows and status/footer/error regions, while a modal transition invalidates that
cache. Hardware adapters retain ownership of actual display calls. Launcher integration,
platform editing, and templates remain later work.

Example:

```text
DEVICES

● Personal MacBook
  Work MacBook
  iPad

[A] Add
[E] Edit
[D] Delete
```

The dependency chain should be:

```text
DeviceManagerApp
       ↓
HostService
       ↓
BluetoothService
       ↓
BLE adapter
```

The Mini App must not implement Bluetooth logic itself.

---

## 22. Host Switching

Host switching should eventually be callable from:

```text
Device Manager
Home
Global shortcut
Web UI
Telegram
Host Companion
```

All sources must invoke the same HostService operation.

Conceptually:

```text
HostService.selectHost(hostId)
```

---

## 23. Global Controls

Certain physical key combinations should work independently of the active Mini App.

Potential examples:

```text
Home
Back
Host switch
Mute
Launcher
```

Input flow:

```text
Keyboard
   ↓
InputService
   ↓
Global shortcut?
   ├── yes → Action Bus
   └── no  → Active Mini App
```

Global shortcuts should be configuration data rather than hardcoded behavior.

---

## 24. WeatherService

`WeatherService` owns weather integration.

Responsibilities may include:

```text
weather API access
request scheduling
response parsing
cache
current conditions
forecast
error state
refresh
```

It does not render UI.

Dependency:

```text
WeatherApp
      ↓
WeatherService
      ↓
WiFiService
```

The Home screen may also consume the same Service.

---

## 25. VpsService

`VpsService` owns VPS-related logic.

Potential responsibilities:

```text
server configuration
API communication
status retrieval
CPU / RAM state
service status
commands
cache
errors
```

Dependency:

```text
VpsMonitorApp
      ↓
VpsService
      ↓
WiFiService
```

The Mini App must not know VPS protocol details.

---

## 26. TelegramService

Telegram integration should be isolated in `TelegramService`.

Potential responsibilities:

```text
Telegram Bot API
authentication/token
polling
sending messages
receiving bot commands
connection state
Telegram events
```

Dependency:

```text
TelegramService
      ↓
WiFiService
```

A Telegram Mini App is optional.

`TelegramService` may exist purely for background integration or remote control.

---

## 27. MediaService

Media-specific behavior may be owned by `MediaService`.

Examples:

```text
play
pause
next
previous
volume
mute
```

Execution may depend on the active host.

Conceptually:

```text
MediaApp
    ↓
MediaService
    ↓
HostService
    ↓
BluetoothService
```

---

## 28. RGB Indicator

The external M5Stack Unit Puzzle 8×8 WS2812E RGB LED matrix is part of the
planned system.

`IndicatorService` owns LED behavior.

Responsibilities:

```text
state
patterns
brightness
static colors
blink
pulse
breathing
simple animations
priority
```

Mini Apps and other Services must not manipulate WS2812 hardware directly.

Dependency:

```text
System / Service / Mini App
           ↓
    IndicatorService
           ↓
       ILEDAdapter
           ↓
 PuzzleWs2812Adapter
```

---

## 29. Indicator Priority

Multiple states may compete for the indicator.

The architecture should support priorities such as:

```text
CRITICAL
   ↓
WARNING
   ↓
NOTIFICATION
   ↓
APPLICATION
   ↓
CONNECTION
   ↓
IDLE
```

Exact visual policy is an implementation/configuration decision.

---

## 30. Notifications

RGB indication for events is part of the architecture.

For example:

```text
Telegram event
      ↓
TelegramService
      ↓
IndicatorService
      ↓
Puzzle LED
```

Screen notification overlays are explicitly outside the initial scope.

Initially not required:

```text
notification popups
message previews
notification history
interactive notification UI
```

The UI architecture should simply avoid making future overlays impossible.

---

## 31. Action Model

User intent should be represented using logical Actions.

Examples:

```text
host.select
host.application.focus
media.play_pause
indicator.pattern.set
vps.refresh
weather.refresh
```

An Action owns:

```text
id
source
ordered named parameters
```

IDs and sources are non-empty, exact, case-sensitive strings. Namespaced IDs
allow future Services to define Actions without adding application-specific
enumerators to System Core. Parameters have non-empty unique names and support
owned strings, signed 32-bit integers, and booleans initially.

Example:

```text
Action
  id: host.select
  source: app.device_manager
  parameters:
    host_id: host-002
```

or:

```text
Action
  id: host.application.focus
  source: input.keyboard
  parameters:
    application: telegram
```

---

## 32. Action Bus

The Action Bus allows multiple control surfaces to invoke the same behavior.

```text
Keyboard ───────┐
Mini App ───────┤
Web UI ─────────┼──→ Action Bus
Telegram ───────┤
Companion ──────┘
```

Actions are routed to the appropriate Service.

Examples:

```text
host.select
    ↓
HostService
```

```text
media.play_pause
    ↓
MediaService
```

```text
vps.refresh
    ↓
VpsService
```

The initial Action Bus is synchronous and registers exactly one non-owning
handler per Action ID. Duplicate registration is rejected without replacing
the original handler. Dispatch validates the Action, routes only by its ID, and
reports handled, rejected, invalid, or unsupported outcomes. Source metadata
and parameters are delivered unchanged to the handler and do not affect route
selection.

UI should not embed low-level implementation commands when a logical Action is
appropriate.

---

## 33. Application-Level Host Control

The architecture must allow future control of applications running on a host.

Examples:

```text
Telegram
├── focus
├── start voice recording
├── send
└── cancel
```

```text
VS Code
├── focus
├── open terminal
└── run command
```

Not all actions need to be implemented initially.

---

## 34. Host HID Actions

Simple host actions may be executed using the Phase 2 HID transport boundary.
BLE is the only current HID implementation and sends only to the selected
host's authenticated BLE connection. An unavailable target rejects output;
there is no alternate transport or implicit host fallback.

Example:

```text
FOCUS_APPLICATION
      ↓
Action resolution
      ↓
configured shortcut
      ↓
IHidTransport
      ↓
BLE HID
      ↓
selected BLE host
```

Logical Actions must remain independent of their HID representation and the
transport selected to deliver it.

---

## 35. Future Host Companion

A future macOS/Windows companion application may provide deeper integration.

```text
Cardputer
    ⇅
Wi-Fi
    ⇅
Host Companion
    ↓
Operating System
    ↓
Applications
```

Potential functionality:

```text
application activation
AppleScript
macOS Shortcuts
Accessibility automation
shell commands
system state
application state
```

Host Companion is not required initially.

---

## 36. RemoteControlService

External control should eventually pass through `RemoteControlService`.

Potential clients:

```text
Local Web UI
Telegram Bot
VPS backend
Host Companion
future mobile application
```

Architecture:

```text
Remote Client
      ↓
RemoteControlService
      ↓
Authentication / Authorization
      ↓
Action Bus
      ↓
Services
```

Remote clients must not directly manipulate internal hardware or Service implementation details.

---

## 37. Local Web UI

A future local Web UI may run directly from Cardputer.

```text
Browser
   ↓
Cardputer HTTP server
   ↓
RemoteControlService
   ↓
Action Bus
```

Potential features:

```text
device status
host selection
Wi-Fi configuration
Mini App configuration
LED control
action/macro configuration
enabled apps
```

Local Web UI is not required initially.

---

## 38. Telegram Remote Control

Telegram may act as a remote control adapter.

Conceptually:

```text
Telegram
    ↓
TelegramService
    ↓
RemoteControlService
    ↓
Action Bus
```

Potential commands:

```text
/status
/host
/weather
/led
/vps
```

Telegram-specific behavior must remain outside System Core.

---

## 39. VPS Backend

A VPS must not be required for normal Cardputer operation.

A future VPS backend may provide:

```text
remote Web UI
Telegram integration
remote access
history
persistent external configuration
multi-device coordination
```

The device must preserve local functionality when VPS infrastructure is unavailable.

---

## 40. Offline Independence

Functions that do not inherently require internet access should continue working offline.

Examples:

```text
Launcher
Bluetooth
Host switching
BLE HID
local configuration
RGB indicator
local Mini Apps
```

Failures in WeatherService, TelegramService, or VpsService must not break Bluetooth or the application shell.

---

## 41. Remote Security Boundary

Remote access must have an authentication and authorization boundary.

Conceptually:

```text
Remote Request
      ↓
Authentication
      ↓
Authorization
      ↓
Validation
      ↓
Action Bus
```

The exact security mechanism may be decided later.

Remote APIs must not be designed around unrestricted arbitrary command execution.

---

## 42. Capabilities

The platform should expose capabilities.

Examples:

```text
WIFI
BLUETOOTH
BLE_HID
HOST_AVAILABLE
WEATHER_SERVICE
VPS_SERVICE
TELEGRAM_SERVICE
RGB_PANEL
REMOTE_CONTROL
COMPANION
REMOVABLE_FILE_STORAGE
```

Mini Apps can declare requirements.

Example:

```text
WeatherApp
requires:
  WIFI
  WEATHER_SERVICE
```

Apps whose required capabilities are unavailable may be hidden or displayed as unavailable according to configuration.

Phase 1 provides System Core's hardware-independent `CapabilityRegistry`. It
owns non-empty, opaque capability identifiers, answers exact case-sensitive
availability queries, and enumerates currently available capabilities in
successful registration order. Availability is dynamic: removing an ID makes
it unavailable, and registering it again appends a new final entry. Uppercase
names are a convention rather than a closed enum or validation rule.

The capability registry records declared availability only. It does not
discover hardware, infer dependency state, identify providers, count multiple
providers, persist state, publish observers, or decide which applications are
eligible. Owning Connectivity components and Services may update logical
capabilities when those layers are implemented. Phase 5 will combine
AppRegistry metadata with registry queries for application eligibility and
presentation policy.

`AppDescriptor::requiredCapabilities` records ordered, non-empty, unique
capability IDs but registration does not verify that those capabilities are
currently available or even known. The AppRegistry and CapabilityRegistry
remain independent foundations until Phase 5 performs explicit eligibility
checks.

`REMOVABLE_FILE_STORAGE` means that a microSD card is mounted and usable; it
does not merely mean that the device has a physical card slot. Its availability
may change when media is inserted, removed, or fails.

The Phase 1 microSD adapter is not composed into the firmware runtime and does
not publish this capability. A future concrete file-storage owner must refresh
media state and explicitly register or remove `REMOVABLE_FILE_STORAGE` as that
state changes.

---

## 43. ConfigurationService

`ConfigurationService` owns the system and user configuration model, including
defaults, validation, migrations, and application-level configuration
operations. It uses the configuration interfaces and persistence primitives
provided by System Core; System Core must not duplicate this domain behavior.

The initial delivered schema contains Host Profiles, `activeHost`, monotonic
`nextHostId`, and `bluetoothEnabled`. One versioned binary record at
`StorageAddress{"hosts", "configuration"}` lives in internal `hub_config` NVS.
Its `HUBH`/version-1 header, bounded lengths/count (up to 16 hosts), unique IDs
and bonds, valid names and selected ID are checked before acceptance. Missing
records default to an empty list and BLE Off. Invalid, unknown-version, or
unreadable records are preserved and reported as errors, never reset silently.
Writes publish the new in-memory value only after successful storage. Schema
migration must be added explicitly when a later version is introduced.

Broader configuration remains planned and includes:

```text
Wi-Fi settings
HostProfile platform/capability extensions
enabled Mini Apps
Mini App settings
Service configuration
global shortcuts
indicator settings
remote-control settings
```

Configuration must be treated as data rather than scattered conditional logic.

The authoritative copy of configuration required for normal boot must use
internal persistent storage. A removable microSD card may be used for explicit
import, export, backup, and restore operations, but its absence must not make
the current configuration unavailable. Secrets must not be copied to removable
media unless a later feature defines an explicit user flow and security model.

## 44. Persistence

The following information should survive reboot:

```text
Wi-Fi configuration
Bluetooth bonds
Host Profiles
active host
enabled Mini Apps
Service settings
Mini App settings
global shortcuts
remote settings
```

Cardputer Hub has two distinct persistence roles:

```text
Internal NVS
    small authoritative configuration records needed for normal operation

microSD
    optional removable files, exports, backups, assets, and larger app data
```

System Core exposes separate hardware-neutral contracts for these roles:

```text
ConfigurationService / Services
            │
            ├── Record Storage ──→ ESP32 NVS adapter
            │
            └── File Storage ────→ Cardputer microSD adapter
```

Record Storage, exposed initially by the `Storage` facade, persists opaque
records addressed by logical scope and key. It does not know configuration
schemas, defaults, migrations, or serialization; those remain owned by
`ConfigurationService`.

`Storage` is System Core's configuration-facing record interface. It validates
portable logical addresses and non-empty records before delegating synchronous
operations to the hardware-independent `IStorageAdapter` persistence
primitive. Missing records, invalid caller input, capacity exhaustion, and
backend failures remain distinct outcomes so future configuration behavior can
make explicit policy decisions instead of silently substituting defaults.

The ESP32 adapter stores each record as an opaque blob in the dedicated
`hub_config` NVS partition. The framework's default `nvs` partition remains
separate because Arduino startup may erase that partition while recovering
from incompatible or exhausted NVS metadata. The adapter explicitly
initializes `hub_config`, never erases or reinitializes it as recovery, and
reports initialization failure as `BackendError`. This dedicated NVS
partition now stores the authoritative HostConfiguration record.
`ConfigurationService` owns its schema, serialization, defaults, and domain
validation. Bluetooth bond keys remain in the separate NimBLE store; profiles
contain only opaque references. Wi-Fi credentials are not persisted yet.

The Phase 2 Wi-Fi foundation does not persist credentials. Connection
configuration is supplied in memory to `WiFiService`, and the ESP32 adapter
selects RAM-backed ESP-IDF driver storage before applying station
configuration. The adapter is the exclusive Wi-Fi station-interface and driver
owner and fails initialization if another component has already created or
initialized either resource, which prevents unknown persistence, event, or
retry policy from being inherited. A future `ConfigurationService` owns the
persistent Wi-Fi schema, validation beyond the connectivity boundary, defaults,
migrations, and storage policy.

ESP-NimBLE persists its bond keys in its non-destructively initialized store.
The Bluetooth adapter separately keeps one random 256-bit bond-reference key as
Bluetooth-internal metadata in the authoritative `hub_config` partition. The
key is created with the ESP-IDF cryptographic random source before pairing can
be admitted, is never exported or logged, and is not regenerated merely because
bonds already exist. Stable public references are the first 128 bits of
HMAC-SHA-256 over each adapter-private identity address. Enumeration rejects a
reference collision instead of exposing an ambiguous target. A future
`ConfigurationService` may migrate the metadata storage mechanics without
changing this public reference contract.

Firmware distribution must pair the application image with its partition
table. Installation from the earlier 8 MiB layout requires one explicit,
one-time provisioning of the flash range repurposed from SPIFFS. Normal uploads
and later upgrades must never erase `hub_config`; an initialization failure
after provisioning remains a backend error rather than destructive recovery.

Phase 1 provides the hardware-neutral `FileStorage` facade and
`IFileStorageAdapter`. File Storage exposes known-path files below a Cardputer
Hub-owned root on the card. Paths are logical and relative to that root, and
callers must not depend on FAT, SPI, mount points, or vendor APIs. Services and
Mini Apps must not access the microSD hardware directly.

Logical paths are non-empty `/`-separated strings. Absolute paths,
backslashes, embedded NUL, empty segments, and `.` or `..` segments are
rejected before reaching an adapter. Callers must not create sibling paths
that differ only by case because matching depends on the mounted filesystem.
Adapters report their own filename rules or total-path limits as `InvalidPath`
without exposing those backend details in the core contract.

Reads require a non-zero maximum size. The adapter checks the file size before
allocating its owned result, returns `TooLarge` without bytes when that bound
would be exceeded, and returns empty data for every unsuccessful read. A
successful empty file is distinct from a missing file. Writes replace the
known path, permit empty files, create parents only within the owned root, and
flush before success. Removal never recursively removes directories.

The Cardputer adapter uses the pinned framework's SD and SPI interfaces and
keeps every managed path below `/cardputer-hub`. Production FAT configuration
enables filenames up to 255 characters with the work buffer allocated on the
heap, and the Cardputer-Adv SDSPI adapter uses the physically validated 10 MHz
clock for media compatibility. Its initial state is `Uninitialized`; an
explicit refresh produces `Ready`, `NotPresent`, or `MountError`. It never
formats, repairs, erases, or repartitions media. It is compiled but not
constructed by `main.cpp`, so Phase 1 performs no automatic mount and writes no
product data.

The microSD card is optional and removable. Missing media, mount failure,
read-only media, capacity exhaustion, and ordinary I/O failure must remain
distinguishable where relevant. Card removal or corruption must not erase,
format, or reinitialize the card automatically, and must not break boot,
Launcher, connectivity, host control, or configuration stored in NVS. When a
mounted card is available, the owning integration may publish the logical
`REMOVABLE_FILE_STORAGE` capability.

Boot must not block indefinitely while waiting for:

```text
Wi-Fi
Bluetooth host
Internet
Telegram
VPS
microSD
```

---

## 45. Hardware Abstraction

Hardware-specific code should remain isolated where practical.

Examples:

```text
IndicatorService
      ↓
ILEDAdapter
      ↓
PuzzleWs2812Adapter
```

```text
BluetoothService
      ↓
IBluetoothAdapter
      ↓
Esp32BleAdapter
```

```text
WiFiService
      ↓
IWifiAdapter
      ↓
Esp32WifiAdapter
```

```text
InputService
      ↓
IKeyboardAdapter
      ↓
M5CardputerKeyboardAdapter
```

```text
ConfigurationService
      ↓
Record Storage
      ↓
Esp32NvsStorageAdapter
```

```text
Service
      ↓
File Storage
      ↓
CardputerMicroSdFileStorageAdapter
```

This is necessary for automated testing and safe refactoring.

The `FileStorage` facade and its adapter interface contain no Arduino, SPI,
filesystem, or board-library types. Those types and the Cardputer-Adv microSD
pin mapping remain inside `CardputerMicroSdFileStorageAdapter`. The adapter is
not part of `SystemRuntime`; a future Service or composition owner will decide
when media is refreshed and which logical files are used.

Likewise, `WiFiService` and `IWifiAdapter` contain no Arduino or ESP32 types.
`Esp32WifiAdapter` contains the ESP-IDF Wi-Fi and network-interface APIs bundled
with the pinned Arduino-ESP32 toolchain. It non-destructively initializes the
framework default NVS partition before Wi-Fi so startup does not depend on
Bluetooth having run first. It exclusively initializes the driver, constructs
its default station interface through checked allocation, attachment, and
handler-registration operations, rolls back the interface and driver after any
partial initialization failure, selects station mode and RAM-backed
configuration storage, begins exactly one association attempt per Service
request, and propagates connect and disconnect operation failures. It does not
initialize Arduino's Wi-Fi facade, whose event handler has independent
reconnect behavior and emits network identity at high framework log levels. The
Cardputer build caps Arduino core logging at Info, and adapter compilation
rejects Debug or Verbose levels.

Adapter input is bounds-checked before copying into fixed ESP-IDF buffers.
Polling confirms both current station association and an assigned IPv4 address
before reporting `Connected`; an unassociated or DHCP-pending attempt remains
`Connecting` until the Service observes connection or applies its timeout.
Only ESP-IDF's ordinary not-connected result represents that in-progress state;
other access-point query failures are fatal adapter errors. RSSI is optional and
comes from the current ESP-IDF access-point record, so an asynchronous link loss
cannot be mistaken for a valid measurement. The adapter is compiled but not
constructed by `main.cpp`.

`BluetoothService` and `IBluetoothAdapter` likewise contain no ESP32 types.
`Esp32BluetoothAdapter` uses the pinned framework's direct ESP-IDF Bluetooth
controller and ESP-NimBLE host, GAP, GATT-server, and persistent-store APIs as a
single-connection BLE peripheral. It does not use an Arduino BLE facade,
NimBLE-Arduino, Bluedroid, Classic Bluetooth, or central scanning. It
exclusively owns the controller and host resources it initializes, rejects
incompatible pre-initialized state, and quiesces completed host/controller
stages in reverse order after failure. Logical shutdown stops advertising,
terminates every adapter-known connection, stops and deinitializes the NimBLE
host, and disables the controller. It deliberately keeps the controller
initialized and exclusively owned for the process lifetime so logical
enable/disable remains repeatable. Failed teardown stages retain their
ownership flags and must be retried before another lifecycle can initialize.

The same adapter registers one project-owned HID-over-GATT service inside that
host lifecycle. Its report map uses report ID 1 for an eight-byte keyboard
input report and report ID 2 for a 16-bit consumer-control input report. It
also exposes HID Information, Report Map, Protocol Mode, Control Point, an
ignored keyboard LED output report, encrypted and authenticated report access,
the HID service UUID, and keyboard appearance. No battery service or invented
battery value is exposed. The ESP-IDF `esp_hid` component is part of the pinned
build graph, but its lifecycle-owning convenience layer is not used because it
would initialize unrelated services and conflict with the adapter's existing
host, advertising, callback, and teardown ownership. Direct ESP-NimBLE GATT
registration keeps those responsibilities explicit.

The runtime console uses the ESP32-S3 fixed USB Serial/JTAG peripheral, as
configured by ESP-IDF. No TinyUSB driver or software USB HID device is installed.
The validation harness's USB serial input is non-blocking and independent of
BLE control. Cable removal cannot change HID readiness through routing policy;
BLE readiness depends only on the selected peer's connection and security.

NimBLE is configured for bonding, MITM protection, Secure Connections-only
security, identity-key distribution, `KeyboardDisplay` I/O, one connection,
and at most 16 bonds. The adapter initiates security only after Service policy
admits the peer, translates the three checked passkey actions, verifies the
stored bond was authenticated with Secure Connections, and provides checked
enumeration and deletion through NimBLE's store APIs. Public, random, and
identity addresses remain adapter-private.

NimBLE callbacks copy only bounded owned values into a fixed event queue; they
do not publish Service state or retain framework-owned pointers. The direct
adapter has one callback context and one physical peer slot, and lifecycle
generations plus definitive host shutdown prevent events from an older host
instance from affecting a later enable. A bounded queue latches overflow as a
fatal polling error. Peer identity addresses stay inside the adapter solely for
bond lookup and are never exposed or logged. Framework Debug and Verbose
logging are compile-time rejected, NimBLE logging is disabled in production,
and audited controller, host, GAP, ATT, SMP, and storage tags are suppressed
before activation. The adapter is compiled but not constructed by `main.cpp`.

---

## 46. Failure Isolation

A failure in one Service must not make unrelated functionality unavailable.

Examples:

```text
WeatherService ERROR
```

must not break:

```text
HostService
BluetoothService
IndicatorService
Launcher
```

Similarly:

```text
TelegramService ERROR
```

must not break:

```text
Weather
VPS Monitor
Device Manager
```

Bluetooth lifecycle, pairing, reference persistence, bond-management, and HID
failures are contained within `BluetoothService` and its owned adapter
resources. A fatal initialization, callback-queue, polling, advertising,
reference, bond-query, finalization, HID registration/send/teardown, cleanup,
or peer-rejection failure enters
Bluetooth `Error` and shuts down that adapter; ordinary insecure pairing is
rejected without affecting unrelated systems. Remove-all reports partial
deletion instead of claiming success. These outcomes do not alter Wi-Fi or
System Core state. Because
the Bluetooth foundation is not part of runtime composition yet, it also cannot
delay or fail normal boot.

Unavailable USB diagnostics must not block local display/input polling or
Bluetooth operation. Wi-Fi and BLE do not depend on a USB data connection.

---

## 47. Initial Development Order

Reviewed against the implementation and verification records on **2026-09-11**.
A checked implementation item is delivered and covered by the applicable local
checks; it is not a claim that every physical acceptance case has passed.
The current device evidence and remaining checks are in
[plan 017](plans/017-hid-transport-arbitration.md#0-current-closeout-status).
Earlier plan records retain their historical test counts and toolchains.

| Phase | Current status |
| --- | --- |
| 1 — System Core | Complete |
| 2 — Connectivity | Software scope complete; physical acceptance partial |
| 3 — Core Services | Partial: host/configuration Services delivered |
| 4 — Application Shell | Partial: Home, Settings, navigation and page transitions delivered |
| 5 — Mini App Infrastructure | Not implemented; Phase 1 registry prerequisites exist |
| 6 — Device Manager | Partial: built-in Bluetooth/host UI delivered |
| 7 — Host Control | Not implemented; Phase 2 HID transport prerequisite exists |
| 8 — Weather | Not implemented |
| 9 — RGB Indicator | Not implemented; Unit Puzzle hardware required |
| 10 — Remote Boundary | Not implemented |
| 11 — Extensions | Future scope |

### Phase 1 — System Core

**Complete** — plans 001–009 establish the foundations.

- [x] Boot, build identity, and structured logging.
- [x] Configuration interfaces and record Storage with ESP32 NVS adapter.
- [x] File Storage facade and Cardputer microSD adapter.
- [x] Semantic keyboard input and display boundary.
- [x] NavigationStack history primitive.
- [x] Capability Registry and metadata-only AppRegistry.
- [x] Action model and ActionBus routing.

microSD remains optional and is not mounted by normal boot. These foundations
do not imply a file browser, backup/import flow, or running Mini Apps.

### Phase 2 — Connectivity

**Software scope complete; physical acceptance partial** — plans 010–015 and
017. Plan 016 and transport arbitration are cancelled, not outstanding work.

- [x] WiFiService state machine and ESP32 station adapter.
- [x] BluetoothService lifecycle and direct ESP-NimBLE adapter.
- [x] Authenticated pairing, interruption recovery, cancellation and timeout.
- [x] Opaque bond references, persistence, selection and removal.
- [x] Selected-bond reconnection, restored security and HID subscriptions.
- [x] Shared IHidTransport/report contract and BLE keyboard/consumer transport.
- [x] USB HID removal; fixed USB Serial/JTAG diagnostics and build guards.
- [x] Native unit/integration checks and Cardputer-Adv production compilation.
- [ ] Final physical acceptance matrix in plan 017, including
  report/cable/interruption reruns, long Off/reboot-Off, and the
  explicitly retained duration/equipment gaps from plans 012/014.

The operator confirmed adding two computers, switching between them, and
reconnection to the last selected host on power-on on 2026-09-11.

Wi-Fi is compiled but uncomposed in normal firmware; configuration and Wi-Fi UI
are later work. BLE is composed by HostService. USB serial hotplug is an open
observation independent of the BLE transport, not a reason to restore routing.
Transport expansion does not block the BLE-only software scope.

### Phase 3 — Core Services

**Partial** — the host/configuration slice was brought forward under plan 017.

- [x] HostService: import existing bonds, pair/add, select, rename and delete hosts.
- [x] Persisted active-host intent and Cardputer BLE On/Off.
- [x] Selection isolation, release-before-switch and failure results through Actions.
- [x] ConfigurationService: bounded version-1 host schema, validation, defaults,
  stable IDs and writes that publish state only after successful storage.
- [x] BatteryService: optional hardware estimate, bounded five-second sampling.
- [ ] Broader HostProfile platform/capability data and mapping templates.
- [ ] Wi-Fi, Mini App, Service, shortcut, indicator and remote configuration.
- [ ] Explicit schema migrations when a later schema version is introduced.
- [ ] IndicatorService abstraction.

### Phase 4 — Application Shell

**Partial** — the delivered built-in shell is not the full Launcher.

- [x] Home with selected host, real BLE state, estimated battery and ambient wave.
- [x] General Settings menu and Bluetooth entry.
- [x] NavigationStack/ActionBus integration and modal-aware Back behavior.
- [x] Plain Tab for built-in settings; Fn+Tab and G0 alternatives.
- [x] Shared palette, bitmap typography, buffered dirty-region presentation.
- [x] Non-blocking 220 ms page slides and interruption from the visible frame.
- [ ] AppRegistry-driven Launcher and navigation into arbitrary Mini Apps.
- [ ] Configurable global shortcuts and broader shell controls.
- [ ] Live clock and Wi-Fi status composition; current placeholders are explicit.
- [ ] Spring focus motion, synthesized sound and persistent sound settings.
- [ ] Idle dim/off, brightness policy and wake-input consumption.

### Phase 5 — Mini App Infrastructure

**Not implemented** — AppRegistry and Capability Registry already exist as
Phase 1 primitives, but their runtime integration remains here.

- [ ] MiniApp interface and Mini App view model.
- [ ] AppRegistry integration with Launcher.
- [ ] Mini App lifecycle and Service lifecycle composition.
- [ ] Runtime capability checks for launching/running Mini Apps.

### Phase 6 — Device Manager

**Partial** — built-in HostSettings is delivered; full Device Manager integration
with the Phase 5 Mini App contract remains open.

- [x] Host list and separate SELECTED versus connection status.
- [x] Add device and authenticated pairing prompts.
- [x] Rename a host while preserving its identity.
- [x] Delete one host and bond with confirmation; preserve other profiles.
- [x] Select/connect a host and control persisted BLE On/Off through HostService.
- [ ] Platform/capability editing and templates beyond the display name.
- [ ] Full Device Manager Mini App registration, lifecycle and capability checks.

### Phase 7 — Host Control

**Not implemented** — Phase 2 provides the HID transport and diagnostic report
commands; normal firmware does not map Cardputer typing or Actions to host input.

- [ ] Basic host HID Actions through IHidTransport.
- [ ] Application focus Actions.
- [ ] Host-specific mappings and templates.

### Phase 8 — Weather

**Not implemented.** This will validate one Service consumed by several UI views.

- [ ] WeatherService.
- [ ] WeatherApp.
- [ ] Home weather summary.

### Phase 9 — RGB Indicator

**Not implemented; requires Unit Puzzle hardware.**

- [ ] Puzzle hardware adapter.
- [ ] IndicatorService states.
- [ ] Animations and priority arbitration.

### Phase 10 — Remote Boundary

**Not implemented.** A full Web UI or Telegram integration is not required yet.

- [ ] RemoteControlService.
- [ ] Authentication boundary.
- [ ] ActionBus integration.

### Phase 11 — Extensions

**Future scope; none of these integrations is implemented.**

- [ ] VpsService and VPS Monitor.
- [ ] TelegramService and Telegram integration.
- [ ] MediaService.
- [ ] Home Assistant.
- [ ] Web UI.
- [ ] Host companion/CLI and authenticated control protocol.

---

## 48. Initial Scope

The first complete platform version should include:

```text
System Core

WiFiService
BluetoothService

HostService
ConfigurationService
IndicatorService

Launcher
AppRegistry
Navigation

multiple host pairing
Host Profiles
host switching
BLE HID

Device Manager Mini App
basic host control

WeatherService
Weather Mini App

Puzzle RGB indicator

Action Bus

RemoteControlService abstraction

microSD file-storage foundation
```

The initial-scope microSD item means the hardware-neutral boundary and adapter,
not a file browser, automatic backup, configuration import or export, or
application-specific card contents. Those require explicit consumers and
user-facing designs in later work.

---

## 49. Not Required Initially

The initial implementation does not need to include:

```text
TelegramService
Telegram Bot
VpsService
VPS Monitor
Local Web UI
VPS backend
Host Companion
advanced Telegram actions
screen notification overlays
Windows-specific automation
advanced iPad/iPhone integration
Home Assistant
OTA
```

The architecture must allow these capabilities to be added without restructuring System Core.

---

## 50. Core Architectural Principles

1. Cardputer is a platform, not a specialized remote control.

2. Mini Apps primarily own UI and interaction.

3. Reusable logic, state, and integrations belong in Services.

4. Connectivity is a separate lower-level layer.

5. Mini Apps must not directly control Wi-Fi, Bluetooth, or hardware.

6. One Service may be consumed by multiple Mini Apps and System UI components.

7. Services may perform background work independently of whether their Mini App is open.

8. Hosts are system entities managed by HostService.

9. Actions are the common representation of user intent.

10. Keyboard, Web UI, Telegram, and Companion are control surfaces, not separate business logic implementations.

11. Configuration is data.

12. Boot-critical configuration must not depend on removable storage.

13. Remote functionality is an optional extension.

14. Failure of one Service must not break unrelated Services.

15. New Mini Apps and Services should be addable without restructuring System Core.

16. Removable file storage is optional, bounded, and confined to a
    Cardputer Hub-owned root; boot-critical configuration remains in internal
    storage.

---

## 51. Target Repository Structure

Conceptually:

```text
src/
├── core/
│   ├── app_registry/
│   ├── navigation/
│   ├── actions/
│   ├── input/
│   ├── lifecycle/
│   ├── logging/
│   └── storage/
│       ├── storage.h / storage.cpp
│       └── files/
│           └── file_storage.h / file_storage.cpp
│
├── connectivity/
│   ├── wifi/
│   └── bluetooth/
│
├── services/
│   ├── hosts/
│   ├── weather/
│   ├── vps/
│   ├── telegram/
│   ├── media/
│   ├── indicator/
│   ├── configuration/
│   └── remote_control/
│
├── apps/
│   ├── device_manager/
│   ├── weather/
│   ├── vps_monitor/
│   ├── telegram/
│   ├── media/
│   ├── led_control/
│   └── settings/
│
├── hardware/
│   ├── display/
│   ├── keyboard/
│   ├── led/
│   ├── bluetooth/
│   ├── wifi/
│   └── storage/
│       ├── nvs/
│       └── microsd/
│           └── cardputer_microsd_file_storage_adapter.*
│
└── main.cpp
```

Exact directories may evolve during implementation.

The dependency direction should remain:

```text
Mini Apps
    ↓
Services
    ↓
Connectivity / Hardware abstractions
```

and not the reverse.

---

## 52. Mental Model

```text
                     MINI APPS
          ┌──────────────┼──────────────┐
          │              │              │
       Weather       VPS Monitor    Device Manager
          │              │              │
          ▼              ▼              ▼

                      SERVICES
          ┌──────────────┼──────────────┐
          │              │              │
   WeatherService    VpsService     HostService
          │              │              │
          └──────────────┼──────────────┘
                         ▼

                    CONNECTIVITY
                  ┌──────┴──────┐
                  │             │
               Wi-Fi        Bluetooth
                  │             │
                  ▼             ▼

                     HARDWARE
```

System Core supplies lifecycle, shell, navigation, Action, capability,
configuration-interface, storage-primitive, and logging facilities across this
flow without reversing the application dependency direction.

In short:

> Mini App shows information and receives input.
> Service knows what to do.
> Connectivity knows how to communicate.
> Hardware Adapter knows how to operate the physical device.
