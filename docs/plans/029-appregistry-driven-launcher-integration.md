# 029 — AppRegistry-Driven Launcher and System Mini App

Status: **Software implemented; physical validation pending**

## 0. Planning Status

Current software evidence: `make host-check` passed (367 native tests plus 72
Python tests, architecture, format, and lint). Launcher presentation is
invalidated on Mini App open and fully redrawn on every return path
(`app.close`, capability loss, and external deactivation), all with a backward
page transition. `ApplicationShell` routes `InputEvents` strictly in order and
re-evaluates presentation after every event, including Mini App → Launcher
inside the same batch. Event delivery uses 0 ms; the active Mini App then
receives one scheduled tick with the frame elapsed time. `app.close` from
inside `IMiniApp::update()` is deferred until that callback returns. `app.open`
is accepted only on the Launcher route while no Mini App is active. Generic
key-click feedback is owned by `ApplicationShell` and applies to Mini App input
as well as System UI; Settings volume steps keep directional cues instead of
`KeyPress`. SYSTEM reports enabled Wi-Fi without a configured network as `ON`,
not `OFF`. Launcher unavailable overlay reasons are truncated to the 240 px
display width. `make firmware-check`,
`make firmware-size`, and physical Home → Launcher → SYSTEM validation remain
pending because ESP-IDF 5.5.5 is not active in this environment.

Plan 028 delivered the Mini App runtime foundation:

* `IMiniApp`;
* `MiniAppRuntime`;
* exact `AppRegistry` ID binding;
* runtime eligibility through `CapabilityRegistry`;
* deterministic activation, update, and deactivation;
* `ApplicationShell` dispatch to an active Mini App;
* automatic deactivation when a required capability is lost.

The missing user-facing path is now:

```text
Home
  ↓
Launcher
  ↓
MiniAppRuntime
  ↓
Mini App
```

Plan 029 implements that path and proves it on real hardware with one deliberately small production Mini App:

```text
SYSTEM
```

`SYSTEM` is not intended to become a large diagnostics feature in this plan.

Its purpose is to demonstrate that:

```text
AppRegistry metadata
        ↓
Launcher
        ↓
app.open
        ↓
MiniAppRuntime
        ↓
IMiniApp lifecycle
        ↓
real Service data
        ↓
display
```

works end to end.

Suggested branch:

```text
feat/029-launcher-and-system-app
```

Suggested PR title:

```text
[029] Add Mini App Launcher and System app
```

---

## 1. Goal

Deliver the first complete Mini App workflow.

After this plan:

```text
Home
  │ Enter
  ▼
Launcher
  │
  │ 01 [icon] SYSTEM ●
  │
  │ Enter
  ▼
SystemApp
  │
  │ Escape
  ▼
Launcher
  │
  │ Escape
  ▼
Home
```

The Launcher must remain generic.

`SystemApp` is only the first application registered through the generic mechanism.

Future applications must not require application-specific changes to Launcher logic.

---

## 2. Scope

Included:

* AppRegistry-driven Launcher;
* `Enter` from Home opens Launcher;
* Home remains visually unchanged;
* application list in AppRegistry registration order;
* three-row scrolling Launcher window;
* remembered Launcher selection;
* 14×14 monochrome bitmap application icons;
* available/unavailable status indicators;
* unavailable reason overlay;
* animated selection plate;
* existing horizontal page transitions;
* `ui.launcher`;
* `app.open`;
* `app.close`;
* return from Mini App to Launcher;
* return from capability loss to Launcher;
* empty-registry `NO APPS` support;
* first production Mini App: `SYSTEM`;
* real on-device end-to-end validation.

Not included:

* Device Manager conversion;
* Weather;
* VPS Monitor;
* Media;
* Telegram;
* LED Control;
* new Service lifecycle abstraction;
* new generic `IService`;
* new system metrics infrastructure;
* heap/RAM diagnostics;
* flash diagnostics;
* charging detection;
* battery-voltage additions;
* new hardware APIs for SystemApp;
* configurable app ordering;
* enabled/disabled app persistence;
* application installation;
* microSD plugins;
* global application shortcuts;
* Mini App internal navigation framework;
* background applications;
* app suspend/resume;
* notification framework;
* generic modal framework.

---

## 3. Navigation Contract

The primary navigation model is:

```text
HOME
  │
  │ Enter
  ▼
LAUNCHER
  │
  │ Enter
  ▼
MINI APP
  │
  │ Escape
  ▼
LAUNCHER
  │
  │ Escape
  ▼
HOME
```

### Home

Home remains visually unchanged.

Do not add:

```text
APPS
launcher icon
arrow
Enter hint
footer
button
```

to the Home screen.

Controls remain:

```text
Enter → Launcher
Tab   → Settings
```

Settings remains separate System UI.

---

## 4. Launcher Visual Model

Launcher is a dedicated system screen.

Conceptually:

```text
┌────────────────────────────────────────┐
│ APPS                              1/3  │
├────────────────────────────────────────┤
│                                        │
│████████████████████████████████████████│
│ 01  [▣] SYSTEM                       ● │
│████████████████████████████████████████│
│                                        │
│ 02  [☀] WEATHER                      ○ │
│                                        │
│ 03  [▤] VPS MONITOR                  ● │
│                                        │
└────────────────────────────────────────┘
```

Only `SYSTEM` exists in normal firmware in Plan 029.

The other rows above illustrate future behavior only.

Each row contains:

```text
ordinal
14×14 bitmap icon
display name
availability indicator
```

Launcher normally exposes three large rows at once.

---

## 5. Application Ordering

Launcher must enumerate:

```cpp
AppRegistry::apps()
```

in registration order.

Do not:

```text
alphabetically sort
hardcode priorities
special-case System
special-case future apps
```

Registration order is the initial Launcher order.

---

## 6. Application Icons

Application icons are:

```text
14 × 14 px
1-bit bitmap masks
```

Normal row:

```text
Ink icon on Bone
```

Selected row:

```text
Bone icon on Ink
```

`AppDescriptor::iconId` is the lookup key.

Conceptually:

```text
iconId
  ↓
AppIconCatalog
  ↓
14×14 bitmap
```

Launcher must not contain application-specific code such as:

```cpp
if (id == "system")
if (id == "weather")
```

An unknown or empty `iconId` uses a generic fallback icon.

Do not introduce a new display-adapter bitmap primitive solely for this feature.

The bitmap may be rendered through the existing drawing abstraction.

---

## 7. Availability Indicator

Every registered application remains visible even when unavailable.

### Available

```text
●
```

Rendering:

```text
filled circle
Leaf
```

### Unavailable

```text
○
```

Rendering:

```text
hollow circle
Vermilion
```

The difference must remain visible independently of color:

```text
● = available
○ = unavailable
```

Availability comes exclusively from `MiniAppRuntime`.

Launcher must not infer availability from concrete Services.

---

## 8. Unavailable Application Behavior

Pressing `Enter` on an unavailable app:

```text
does not navigate
does not activate the app
keeps the current selection
shows the reason in a top overlay
```

Example:

```text
┌────────────────────────────────────────┐
│ REQUIRES WIFI                          │
├────────────────────────────────────────┤
│                                        │
│████████████████████████████████████████│
│ 02  [☀] WEATHER                      ○ │
│████████████████████████████████████████│
│                                        │
└────────────────────────────────────────┘
```

The reason overlay is a transient annunciator, not a separate page or modal.

---

## 9. Availability Reason

Runtime remains the source of truth.

At minimum distinguish:

```text
MissingInstance
MissingCapability
UnknownApp
```

Suggested presentation:

```text
MissingInstance
    → APP NOT READY

MissingCapability
    → REQUIRES <CAPABILITY>

UnknownApp
    → APP NOT FOUND
```

Extend `MiniAppRuntime` only enough to expose the reason without duplicating capability logic in Launcher.

Conceptually:

```cpp
struct MiniAppAvailability {
    MiniAppEligibility eligibility;
    std::optional<std::string> missingCapability;
};
```

Only the first missing required capability is needed.

---

## 10. Availability Overlay Animation

The overlay:

```text
slides down
stays visible briefly
slides back up
```

Suggested timing:

```text
enter    ~120 ms
visible  ~1600 ms
exit     ~120 ms
```

Requirements:

* driven by injected monotonic elapsed time;
* non-blocking;
* no `delay()`;
* does not block Services or Connectivity;
* settled state requests no animation frames.

Repeated `Enter` restarts the visible duration instead of queueing overlays.

A new unavailable reason replaces the previous one.

Leaving Launcher clears the overlay.

---

## 11. Selection and Scrolling

Launcher remembers its selection during the current boot.

Example:

```text
01 System
02 Weather   ← selected
03 VPS
```

Open Weather:

```text
Launcher
   ↓
Weather
```

Return:

```text
01 System
02 Weather   ← still selected
03 VPS
```

No persistence across reboot is required.

After reboot, selection starts at the first registered application.

### Scrolling

Launcher shows at most three applications.

For:

```text
01 System
02 Weather
03 VPS
04 Media
05 Telegram
```

initial viewport:

```text
01 System
02 Weather
03 VPS
```

moving lower:

```text
02 Weather
03 VPS
04 Media
```

Maintain:

```text
selectedIndex
windowStart
```

Selection must always remain visible.

No wrap-around is required.

---

## 12. Selection Animation

The Ink selection plate moves between visible rows using a bounded spring animation.

Rows stay stationary.

Requirements:

* movement begins immediately after accepted input;
* driven by injected elapsed time;
* retargetable during motion;
* deterministic settling;
* no animation once settled.

The motion should feel consistent with the existing UI, roughly within:

```text
160–220 ms
```

Do not add excessive bounce.

When the scrolling window itself changes, row content changes while the focus plate remains at the corresponding visible slot.

---

## 13. Page Transitions

Use the existing horizontal slide transition.

Directions:

```text
Home      → Launcher   Forward
Launcher  → Mini App   Forward

Mini App  → Launcher   Backward
Launcher  → Home       Backward
```

Reuse the existing approximately 220 ms cubic transition.

Do not create a new page-transition implementation.

---

## 14. Actions

Introduce:

```text
ui.launcher
app.open
app.close
```

### Open Launcher

```text
Home Enter
    ↓
ui.launcher
    ↓
ApplicationShell
    ↓
Launcher
```

### Open App

```text
Launcher Enter
    ↓
app.open
    ↓
appId parameter
    ↓
MiniAppRuntime::activate(appId)
```

Example:

```text
app.open
appId = "system"
```

### Close App

```text
Mini App Escape
    ↓
app.close
    ↓
MiniAppRuntime::deactivate()
    ↓
Launcher
```

Malformed `app.open` Actions must not mutate runtime state.

---

## 15. Capability Loss

If an active Mini App loses a required capability:

```text
MiniAppRuntime::update()
    ↓
DeactivatedMissingCapability
```

the shell returns to:

```text
Launcher
```

not Home.

The same application stays selected.

Its availability updates:

```text
● → ○
```

The top overlay displays the reason.

`SYSTEM` itself has no required capabilities, so this behavior is primarily for future applications but must be supported by the generic Launcher integration.

---

# SYSTEM MINI APP

## 16. Purpose

Plan 029 includes one small real Mini App:

```text
SYSTEM
```

Its only purpose is to prove the production Mini App path on a real Cardputer.

It must remain:

```text
read-only
small
boring
easy to test
based only on existing data
```

Do not turn it into a general diagnostic framework.

---

## 17. System App Registration

Register:

```text
id: system
displayName: SYSTEM
iconId: system
requiredCapabilities: []
```

No required capabilities means the app should always be eligible once its runtime instance is registered.

Normal firmware therefore contains:

```text
AppRegistry
└── system

MiniAppRuntime
└── system → SystemApp
```

Launcher after Plan 029:

```text
┌────────────────────────────────────────┐
│ APPS                              1/1  │
├────────────────────────────────────────┤
│                                        │
│████████████████████████████████████████│
│ 01  [system icon] SYSTEM             ● │
│████████████████████████████████████████│
│                                        │
└────────────────────────────────────────┘
```

---

## 18. System App Data

Use only data already available in the current architecture.

Do not introduce new hardware telemetry solely for this app.

The initial screen may display:

```text
Battery percentage
Bluetooth / host-control enabled state
selected host name
host connection state
Wi-Fi enabled state
Wi-Fi connection state
configured / connected SSID
Wi-Fi signal strength when already available
firmware version
build/commit information if it fits cleanly
```

### Existing data sources

Use:

```text
BatteryService
HostService
NetworkService
firmwareBuildInfo()
```

Do not access hardware directly from `SystemApp`.

Forbidden:

```text
M5.Power
ESP heap APIs
ESP flash APIs
NimBLE internals
Wi-Fi driver internals
hardware adapter headers
```

---

## 19. Minimum System App Content

The minimum required content is intentionally small.

Example:

```text
┌────────────────────────────────────────┐
│ SYSTEM                                 │
├────────────────────────────────────────┤
│ 01  BATTERY                       81%  │
│ 02  BLUETOOTH                       ON │
│ 03  HOST                 WORK MACBOOK │
│ 04  HOST STATUS                  READY │
│ 05  WI-FI                   CONNECTED │
│ 06  NETWORK                  MY-WIFI  │
└────────────────────────────────────────┘
```

Exact labels may be shortened to fit the 240×135 display.

If firmware version fits naturally:

```text
07  VERSION                   0.x.y
```

may also be included.

Do not add fields merely to make the screen look fuller.

---

## 20. System App Data Semantics

### Battery

Read:

```cpp
BatteryService::percent()
```

Presentation:

```text
81%
```

or:

```text
--%
```

when unavailable.

No charging state.

No voltage.

No current.

### Bluetooth

Use existing system state.

A simple initial presentation may derive Bluetooth enabled/disabled from the already exposed host-control state:

```text
ON
OFF
```

Do not expose Bluetooth implementation-state-machine details unless already available through an appropriate public boundary and useful to the screen.

### Host

Use the existing HostService snapshot.

Show:

```text
activeHostName
connection status
```

Examples:

```text
WORK MACBOOK
READY
CONNECTING
OFF
PAIRING
ERROR
```

If no host is selected:

```text
NONE
```

### Wi-Fi

Use `NetworkService::status()`.

Show:

```text
enabled
connection
ssid
```

`OFF` means Wi-Fi is disabled. Enabled Wi-Fi without a configured network is
`ON`. NETWORK remains `NONE` when no SSID is configured.

Examples:

```text
OFF
ON
CONNECTING
CONNECTED
ERROR
```

For network name:

```text
MY-WIFI
```

If not configured:

```text
NONE
```

RSSI may be shown only if it can be added without complicating the layout, because it is already present in the existing snapshot.

### Firmware

Use the existing build information.

At minimum, firmware version may be displayed if there is room.

Commit/build type are optional for Plan 029.

Do not create additional build metadata.

---

## 21. System App UI

`SystemApp` uses a simple read-only vertical list.

There is no selection because nothing is actionable.

Input:

```text
Up / Down → scroll when content exceeds viewport
Escape    → Launcher
```

`Enter` has no action.

No editing.

No dialogs.

No Actions other than leaving the application.

No menus.

No sub-pages.

### Scrolling

If all useful information fits:

```text
do not add scrolling unnecessarily
```

If it does not fit:

```text
Up / Down scrolls the information list
```

Keep the implementation simple.

---

## 22. System App Refresh

The app reads current Service snapshots during `update()`.

It should redraw only when visible values change.

Examples:

```text
battery percentage changes
Wi-Fi connection changes
SSID changes
selected host changes
host status changes
```

Do not continuously repaint unchanged values.

Do not introduce its own polling timers for Services that already own their polling cadence.

SystemApp reads state; it does not drive the underlying Services.

---

## 23. System App Architecture

Suggested structure:

```text
src/apps/system/
├── system_app.h
└── system_app.cpp
```

Optional rendering helper only if useful:

```text
system_app_graphics.*
```

Constructor dependencies should remain explicit.

Conceptually:

```cpp
SystemApp(
    BatteryService& battery,
    HostService& hosts,
    NetworkService& network,
    IDisplayAdapter& display);
```

Build information may be obtained through the existing Core build-info API.

Do not inject:

```text
BluetoothService
WiFiService
hardware adapters
global application context
service locator
```

unless an existing higher-level Service cannot provide the required value.

Prefer fewer fields over breaking the architecture.

---

## 24. Launcher Source Structure

Suggested structure:

```text
src/apps/launcher/
├── launcher.h
├── launcher.cpp
├── launcher_graphics.h
├── launcher_graphics.cpp
└── assets/
    └── app_icons.h
```

Responsibilities:

```text
Launcher
├── AppRegistry enumeration
├── selection
├── scrolling
├── eligibility presentation
├── Action dispatch
├── overlay state
└── render invalidation
```

SystemApp remains completely separate from Launcher.

---

## 25. Dependency Direction

Preserve:

```text
Mini Apps
    ↓
Services
    ↓
Connectivity
    ↓
Hardware
```

Launcher may depend on:

```text
AppRegistry
MiniAppRuntime
ActionBus
display/input Core contracts
```

SystemApp may depend on:

```text
BatteryService
HostService
NetworkService
Core display/build-info contracts
```

SystemApp must not include hardware implementation headers.

---

## 26. Empty Registry Support

Even though production firmware now registers `SYSTEM`, Launcher must still correctly handle an empty registry.

Expected state:

```text
┌────────────────────────────────────────┐
│ APPS                                   │
├────────────────────────────────────────┤
│                                        │
│                NO APPS                 │
│                                        │
└────────────────────────────────────────┘
```

This behavior is covered by host tests.

Normal production firmware should not normally reach it after Plan 029 because `SYSTEM` is registered.

---

## 27. Rendering

Launcher and SystemApp must preserve incremental rendering.

A settled screen with unchanged data must not continuously redraw.

Redraw when:

```text
Launcher selection changes
Launcher window changes
availability changes
overlay changes
animation advances
SystemApp visible values change
```

Page transitions remain handled by the existing display transition mechanism.

---

## 28. Native Launcher Tests

Cover:

### Empty registry

```text
NO APPS
Escape → Home
```

### Registration order

Apps appear exactly in AppRegistry order.

### Selection

```text
initial selection
Up at first
Down
Down at last
selection remembered
```

### Scrolling

Selection stays visible in a three-row window.

### Icons

```text
known 14×14 icon
fallback icon
selected icon inversion
```

### Availability

```text
Eligible          → ● Leaf
MissingInstance   → ○ Vermilion
MissingCapability → ○ Vermilion
```

### Unavailable launch

```text
does not activate
keeps selection
shows reason
```

### Overlay animation

Use injected elapsed time.

### Successful activation

Use fake `IMiniApp`.

Verify:

```text
app.open
exact ID
onActivate once
```

### Close

Verify:

```text
Escape
onDeactivate once
Launcher restored
selection preserved
```

### Capability loss

Verify return to Launcher.

### Home regression

```text
Enter → Launcher
Tab   → Settings
```

---

## 29. System App Tests

Use fake existing Services/state sources where applicable.

Verify rendering behavior for:

### Battery

```text
81% → "81%"
missing → "--%"
```

### Host

```text
selected host name
no selected host
connection status changes
```

### Wi-Fi

```text
disabled
connecting
connected
error
configured SSID
no configured network
```

### Update behavior

When snapshots do not change:

```text
no unnecessary full repaint
```

When one value changes:

```text
corresponding presentation updates
```

### Lifecycle

Verify:

```text
onActivate
update
onDeactivate
```

through the normal MiniAppRuntime path.

---

## 30. Firmware Composition

Normal firmware composes:

```text
AppRegistry
CapabilityRegistry
MiniAppRuntime
Launcher
SystemApp
ApplicationShell
```

Then:

```text
register SYSTEM AppDescriptor
register SYSTEM runtime instance
```

The normal production workflow becomes:

```text
boot
  ↓
Home
  ↓ Enter
Launcher
  ↓
SYSTEM ●
  ↓ Enter
SystemApp
  ↓ Escape
Launcher
  ↓ Escape
Home
```

No fake or validation-only application is required.

---

## 31. Physical Cardputer-Adv Validation

Plan 029 must now be physically validated end to end.

### Home

Confirm:

```text
Home appearance unchanged
Tab opens Settings
Enter opens Launcher
```

### Launcher

Confirm:

```text
SYSTEM appears
14×14 icon appears
availability indicator is green/filled
selection plate renders correctly
```

### Launch

Press Enter.

Confirm:

```text
forward page slide
SYSTEM activates
SystemApp renders real data
```

### Real data

Confirm that the app displays current values from the running device, such as:

```text
battery percentage
Bluetooth enabled state
selected host name/status
Wi-Fi state
network name
firmware version if included
```

No demo/fake telemetry may appear in production.

### Live updates

Where practical, change existing system state and verify SystemApp updates:

```text
connect/disconnect Wi-Fi
change selected host
change Bluetooth enabled state
battery percentage if it naturally changes
```

Do not require destructive configuration changes solely for validation.

### Return

Press Escape.

Confirm:

```text
SystemApp deactivates
backward slide
Launcher returns
SYSTEM remains selected
```

Press Escape again:

```text
Launcher → Home
```

### Re-entry

Repeat several times:

```text
Home
Launcher
System
Launcher
Home
```

Confirm no:

```text
stuck transitions
lost input
corrupt rendering
unexpected network changes
unexpected host changes
```

---

## 32. Firmware Size

Run:

```bash
make firmware-size
```

Record:

```text
application size
partition capacity
remaining bytes
delta from Plan 028
```

Expected additions:

```text
Launcher
bitmap icon assets
overlay/selection animation
SystemApp
small runtime availability-detail changes
```

Do not introduce a size budget.

---

## 33. Documentation

Update:

```text
docs/ARCHITECTURE.md
docs/UI_REQUIREMENTS.md
docs/plans/README.md
README.md
docs/manuals/device-guide.md
```

Document the real user flow:

```text
Enter on Home → Apps
SYSTEM → system information
Escape → Apps
Escape → Home
```

Do not describe future Mini Apps as available.

---

## 34. Implementation Order

### Step 1 — Runtime availability detail

Expose enough typed information for Launcher to explain unavailable apps.

### Step 2 — Launcher state model

Implement:

```text
registry enumeration
selection
scrolling window
empty state
availability state
```

Host-test before rendering.

### Step 3 — 14×14 icon catalog

Add:

```text
SYSTEM icon
generic fallback icon
```

Only icons actually required now need to ship.

Do not create the full future application icon set prematurely.

### Step 4 — Launcher rendering

Implement:

```text
header
rows
ordinal
icon
name
availability indicator
selection plate
NO APPS
```

### Step 5 — Launcher animations

Add:

```text
selection spring
availability overlay
existing horizontal page transitions
```

### Step 6 — Actions and shell integration

Add:

```text
ui.launcher
app.open
app.close
```

Wire:

```text
Home → Launcher
Launcher → Runtime
Runtime → Launcher
Launcher → Home
```

### Step 7 — Minimal SystemApp

Create read-only Mini App using only:

```text
BatteryService
HostService
NetworkService
existing build info
```

Keep the field list minimal.

### Step 8 — Register SystemApp

Normal firmware registers:

```text
AppDescriptor("system", ...)
SystemApp instance
```

### Step 9 — Automated verification

Run:

```bash
make host-check
```

### Step 10 — Firmware verification

Run:

```bash
make firmware-check
make firmware-size
git diff --check
```

### Step 11 — Documentation

Update project docs and user guide.

### Step 12 — Physical validation

Validate the complete:

```text
Home → Launcher → SYSTEM → Launcher → Home
```

flow on Cardputer-Adv.

---

## 35. Acceptance Criteria

Plan 029 is complete when:

```text
Home remains visually unchanged.

Enter on Home opens Launcher.

Launcher reads AppRegistry.

Applications appear in registration order.

Launcher supports a three-row scrolling window.

Each row contains:
    ordinal
    14×14 bitmap icon
    name
    availability indicator

Available:
    filled Leaf circle

Unavailable:
    hollow Vermilion circle

Unavailable apps remain visible.

Enter on unavailable app shows the animated reason overlay.

Launcher selection is remembered.

Selection movement is animated.

Page navigation uses existing horizontal slide transitions.

app.open activates the selected Mini App from idle Launcher only.

An active Mini App may request app.close and must not dispatch app.open.

app.close deactivates the active Mini App.

Escape from Mini App returns to Launcher.

Escape from Launcher returns Home.

Capability loss returns to Launcher.

Empty registry still renders NO APPS.

SYSTEM is registered as the first production Mini App.

SYSTEM has no required capabilities.

SYSTEM is visible and available in normal firmware.

SYSTEM opens through the real Launcher/runtime path.

SYSTEM displays only existing real device/application state.

SYSTEM does not access hardware directly.

SYSTEM does not introduce new metrics infrastructure.

SYSTEM does not introduce charging detection.

SYSTEM does not introduce RAM/flash diagnostics.

SYSTEM is read-only.

SYSTEM exits back to Launcher.

Settings remains separate and still opens through Tab.

Host tests pass.

Architecture checks pass.

Production firmware builds.

Firmware size is recorded.

Physical Home → Launcher → SYSTEM → Launcher → Home validation passes.
```

---

## 36. Architectural Outcome

After Plan 029:

```text
                  AppRegistry
                      │
                      ▼
Home ──Enter──►   Launcher
                      │
                  app.open
                      │
                      ▼
               MiniAppRuntime
                      │
                      ▼
                  SystemApp
                  /   |   \
                 /    |    \
        BatteryService
        HostService
        NetworkService
```

This provides the first real proof that the Mini App architecture works in production.

A future Mini App should need only:

```text
AppDescriptor
concrete IMiniApp
explicit Service dependencies
runtime registration
icon asset
```

and should appear in the same Launcher without application-specific Launcher changes.
