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

Phase 4 application-shell code will own and integrate this history with Home,
Launcher, global shortcuts, and navigation Actions. Phase 5 will define Mini
App view objects and lifecycle; those view implementations remain outside the
navigation history primitive.

---

## 10. Connectivity Layer

Initial connectivity consists of:

```text
Connectivity
├── WiFiService
└── BluetoothService
```

Additional mechanisms may later include:

```text
USB
MQTT
WebSocket
LoRa
```

when needed.

---

## 11. WiFiService

`WiFiService` owns Wi-Fi connectivity.

Responsibilities:

```text
connect
disconnect
reconnect
connection state
signal strength
network configuration
```

It may also expose shared networking infrastructure such as:

```text
HTTP client
DNS
future WebSocket support
```

Higher-level Services must not use ESP32 Wi-Fi APIs directly where this abstraction is sufficient.

---

## 12. BluetoothService

`BluetoothService` owns low-level Bluetooth lifecycle and transport.

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

---

## 17. Host Profiles

A `HostProfile` should contain information such as:

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

Expected pairing flow:

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

Device management UI is implemented as a Mini App.

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

## 34. BLE HID Actions

Simple host actions may be executed using BLE HID.

Example:

```text
FOCUS_APPLICATION
      ↓
Action resolution
      ↓
configured shortcut
      ↓
BluetoothService
      ↓
BLE HID
      ↓
MacBook
```

Logical Actions must remain independent of their HID representation.

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

Configuration includes:

```text
Wi-Fi settings
Host Profiles
active host
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

---

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
partition is authoritative for future boot-critical configuration, but no
configuration value, credential, or other product data is currently persisted
by the firmware. `ConfigurationService`, when introduced, will own schemas,
serialization, defaults, domain validation, and migrations.

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
keeps every managed path below `/cardputer-hub`. Its initial state is
`Uninitialized`; an explicit refresh produces `Ready`, `NotPresent`, or
`MountError`. It never formats, repairs, erases, or repartitions media. It is
compiled but not constructed by `main.cpp`, so Phase 1 performs no automatic
mount and writes no product data.

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

---

## 47. Initial Development Order

### Phase 1 — System Core

Implement:

```text
Boot
Logging
Configuration interfaces
Record Storage facade and ESP32 NVS adapter
File Storage facade and Cardputer microSD adapter
Input
Display
Navigation history primitive
Capability Registry
AppRegistry
Action model
Action Bus
```

Phase 1 establishes the microSD boundary and verifies the adapter, but does not
make removable media a boot dependency or introduce a file browser, automatic
backup, configuration import, or application-specific card contents.

The verified microSD facade and Cardputer adapter complete the Phase 1
foundations. This does not make later-phase behavior operational: connectivity
begins in Phase 2, Launcher behavior remains Phase 4, and AppRegistry and
capability integration remain Phase 5.

### Phase 2 — Connectivity

Implement:

```text
WiFiService
BluetoothService
BLE HID
pairing
bond persistence
reconnection
```

### Phase 3 — Core Services

Implement:

```text
HostService
ConfigurationService
```

Prepare:

```text
IndicatorService abstraction
```

### Phase 4 — Application Shell

Implement:

```text
Home
Launcher
Navigation history integration
Global shortcuts
Settings foundation
```

### Phase 5 — Mini App Infrastructure

Implement:

```text
MiniApp interface
AppRegistry integration
App lifecycle
Mini App views
Capability checks
```

### Phase 6 — Device Manager

Implement:

```text
host list
pair device
edit profile
delete device
switch active host
```

### Phase 7 — Host Control

Implement:

```text
basic BLE HID actions
application focus actions
host-specific mappings
```

### Phase 8 — Weather

Implement:

```text
WeatherService
WeatherApp
Home weather summary
```

This validates that one Service can be consumed by several UI surfaces.

### Phase 9 — RGB Indicator

When Unit Puzzle hardware is available, implement:

```text
Puzzle adapter
IndicatorService states
animations
priority
```

### Phase 10 — Remote Boundary

Implement the architectural boundary:

```text
RemoteControlService
authentication boundary
Action Bus integration
```

A complete Web UI or Telegram integration is not required yet.

### Phase 11 — Extensions

Possible future work:

```text
VpsService
VPS Monitor
TelegramService
Telegram integration
MediaService
Home Assistant
Web UI
Host Companion
```

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
