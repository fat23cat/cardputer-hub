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
│ Storage                              │
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
├── Storage primitives
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

Mini Apps register through a shared `AppRegistry`.

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
SELECT_HOST
FOCUS_APPLICATION
MEDIA_PLAY_PAUSE
LED_SET_PATTERN
VPS_REFRESH
WEATHER_REFRESH
```

Actions may carry parameters.

Example:

```text
Action
  type: SELECT_HOST
  hostId: host-002
```

or:

```text
Action
  type: FOCUS_APPLICATION
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
SELECT_HOST
    ↓
HostService
```

```text
MEDIA_PLAY_PAUSE
    ↓
MediaService
```

```text
VPS_REFRESH
    ↓
VpsService
```

UI should not embed low-level implementation commands when a logical Action is appropriate.

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

Boot must not block indefinitely while waiting for:

```text
Wi-Fi
Bluetooth host
Internet
Telegram
VPS
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

This is necessary for automated testing and safe refactoring.

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
Storage primitives
Input
Display
Navigation
AppRegistry
Action model
Action Bus
```

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
Navigation stack
Global shortcuts
Settings foundation
```

### Phase 5 — Mini App Infrastructure

Implement:

```text
MiniApp interface
AppRegistry integration
App lifecycle
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
```

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

12. Remote functionality is an optional extension.

13. Failure of one Service must not break unrelated Services.

14. New Mini Apps and Services should be addable without restructuring System Core.

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
│   └── logging/
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
