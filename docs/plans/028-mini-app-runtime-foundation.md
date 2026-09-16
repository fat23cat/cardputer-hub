# 028 — Mini App Runtime Foundation

Status: **Software complete; physical smoke pending**

## 0. Current Closeout Status

Software implementation completed on **2026-09-16**.

Delivered:

* application-layer `IMiniApp` and `MiniAppRuntime`;
* exact AppRegistry ID binding with non-owning instance registration;
* CapabilityRegistry eligibility at activation and while active;
* deterministic activate / switch / deactivate / update semantics;
* `ApplicationShell` dispatch to an already-active Mini App, with Home recovery on explicit deactivation and required-capability loss;
* normal-firmware composition of empty `AppRegistry`, `CapabilityRegistry`, and `MiniAppRuntime`;
* architecture, README, and plan-index updates.

No Launcher, production Mini App, schema v5, or generic `IService` abstraction was added.

Final software evidence: `make host-check` passed (321 native tests plus 72 Python tests, architecture, format, and lint); `make firmware-check` passed; `git diff --check` passed; and `make firmware-size` recorded application image size `0x11a360` bytes (1,156,448), with 2,185,888 bytes (65%) remaining in the 3,342,336-byte app partition. That is a 1,328-byte increase from the Plan 027 closeout image.

Physical Cardputer-Adv focused shell smoke from section 34 remains operator-pending.

## 0. Planning Status

Plan 027 completed the device-facing Wi-Fi slice and leaves the firmware with a stable built-in Application Shell, persisted Wi-Fi configuration, HostService, NetworkService, AudioService, BatteryService, ActionBus, NavigationStack, CapabilityRegistry, and the metadata-only AppRegistry.

The next architectural step is to begin Phase 5 without prematurely implementing Launcher UI or a product Mini App.

This plan introduces the runtime contract required to host Mini Apps and connects that runtime to the normal Application Shell.

Suggested branch:

```text
feat/028-mini-app-runtime-foundation
```

Suggested PR title:

```text
[028] Add Mini App runtime foundation
```

The intended result is:

```text
AppRegistry
    metadata
       │
       ▼
MiniAppRuntime
    live instances
       │
       ▼
ApplicationShell
    active-app dispatch
```

while preserving the existing dependency direction:

```text
Apps
    ↓
Services
    ↓
Connectivity
    ↓
Hardware
```

There must be no visible Launcher or production Mini App in this plan.

---

## 1. Goal

Introduce the minimum runtime model required to execute independent Mini Apps without making `ApplicationShell` aware of individual application implementations.

After this plan, the firmware should have:

```text
IMiniApp
MiniAppRuntime
AppRegistry metadata binding
CapabilityRegistry eligibility checks
deterministic activation/deactivation
ApplicationShell dispatch to an active Mini App
```

The runtime must support the future sequence:

```text
Launcher
   ↓
select app
   ↓
MiniAppRuntime::activate(appId)
   ↓
IMiniApp::onActivate()
   ↓
IMiniApp::update(...)
   ↓
MiniAppRuntime::deactivate()
   ↓
IMiniApp::onDeactivate()
   ↓
Launcher
```

Plan 028 does **not** implement the Launcher itself.

Its purpose is to make Plan 029 a UI/integration change rather than forcing Plan 029 to invent application lifecycle, ownership, eligibility, and dispatch semantics at the same time.

---

## 2. Current State

The repository already contains a metadata-only `core::AppRegistry`.

An `AppDescriptor` currently contains:

```text
id
displayName
iconId
entryRoute
requiredCapabilities
```

The registry:

```text
owns descriptor metadata
validates descriptors
rejects duplicate IDs
preserves registration order
supports exact lookup
```

It intentionally does not contain:

```text
Mini App instances
factories
lifecycle callbacks
view objects
Launcher policy
capability filtering
runtime ownership
```

`CapabilityRegistry` already provides dynamic exact capability availability:

```cpp
registerCapability(...)
removeCapability(...)
isAvailable(...)
availableCapabilities()
```

The current normal firmware does not yet compose either registry into an application runtime.

`ApplicationShell` directly owns the current built-in system UI flow:

```text
Home
Settings
Bluetooth / HostSettings
Wi-Fi / WiFiSettings
Sound volume
```

and therefore has no generic active-application boundary.

The current composition is conceptually:

```text
main.cpp
├── Services
├── ActionBus
├── HostSettings
├── WiFiSettings
└── ApplicationShell
```

Future Weather, VPS Monitor, Device Manager, Media, Telegram, LED Control, and similar applications must not be added by continually extending `ApplicationShell` with application-specific branches.

The missing boundary is:

```text
AppRegistry + CapabilityRegistry
              ↓
        MiniAppRuntime
              ↓
       ApplicationShell
              ↓
           IMiniApp
```

---

## 3. Scope

Included:

* introduce a hardware-independent `IMiniApp` application-layer contract;
* introduce a `MiniAppRuntime`;
* bind runtime instances to existing `AppRegistry` descriptors by exact app ID;
* preserve `AppRegistry` as metadata-only System Core infrastructure;
* use `CapabilityRegistry` to determine runtime eligibility;
* reject activation when required capabilities are unavailable;
* re-check required capabilities while an app is running;
* deterministically activate, switch, update, and deactivate Mini Apps;
* define exact behavior for repeated activation and failed switching;
* make runtime instance ownership explicitly non-owning;
* compose `AppRegistry`, `CapabilityRegistry`, and `MiniAppRuntime` in normal firmware;
* wire `MiniAppRuntime` into `ApplicationShell`;
* allow `ApplicationShell` to dispatch UI updates to an already-active Mini App;
* preserve the current Home and built-in Settings behavior while no Mini App is active;
* native tests for the runtime contract;
* shell-level regression tests for active-app dispatch;
* architecture dependency enforcement;
* production firmware compilation;
* firmware-size observation;
* focused physical regression smoke test.

Not included:

* Launcher UI;
* Launcher list rendering;
* icons in Launcher;
* registering any production Mini App;
* converting HostSettings into DeviceManagerApp;
* converting WiFiSettings or Settings into Mini Apps;
* WeatherService;
* WeatherApp;
* VPS Monitor;
* HostControlService;
* CompanionService;
* application-specific Services;
* Mini App enable/disable configuration;
* user-configurable application ordering;
* schema version 5;
* persisted Mini App settings;
* persisted Mini App enabled state;
* Service lifecycle abstraction;
* generic `IService` interface;
* service dependency injection container;
* service locator;
* dynamic heap-owned application factories;
* application plugins loaded from microSD;
* runtime unloading;
* hot registration or unregistering;
* global shortcuts for launching applications;
* ActionBus `app.open` / `app.close` Actions;
* internal multi-view Mini App navigation;
* route parameters;
* notifications;
* background Mini Apps;
* concurrent active Mini Apps;
* app suspension/resume;
* capability publication by existing Services;
* changes to Wi-Fi, Bluetooth, HID, or hardware adapters.

Static process-lifetime composition remains the initial model.

---

## 4. Architectural Boundary

Add the runtime under the Application layer rather than System Core.

Suggested structure:

```text
src/apps/runtime/
├── mini_app.h
├── mini_app_runtime.h
└── mini_app_runtime.cpp
```

The dependency direction should be:

```text
apps/runtime
    ↓
core/app_registry
core/capabilities
core/input
```

`MiniAppRuntime` must not depend on:

```text
HostService
NetworkService
BluetoothService
WiFiService
ESP32
Cardputer hardware
M5Unified
specific Mini Apps
```

A concrete Mini App may depend on its required Services through normal constructor injection.

Example future composition:

```text
WeatherApp
├── WeatherService&
├── ActionBus&
└── IDisplayAdapter&
```

The runtime must not inject arbitrary Services into applications.

Do not introduce:

```text
ServiceLocator
ApplicationContext with every Service
global singleton access
std::any dependency bags
string-based service lookup
```

Concrete application dependencies remain explicit constructor dependencies.

---

## 5. Preserve AppRegistry Ownership

Do not turn `core::AppRegistry` into a runtime container.

It must remain responsible only for application metadata.

Keep the separation:

```text
AppRegistry
    owns metadata

MiniAppRuntime
    references executable instances
```

Do not add any of the following to `AppDescriptor`:

```text
IMiniApp*
factory
std::function creator
void*
lifecycle callback
display reference
Service reference
```

Do not make `core/app_registry` depend on `apps/`.

That would invert the architectural direction:

```text
Core → Apps
```

which is forbidden.

Runtime binding occurs using the existing exact `AppDescriptor::id`.

Example:

```text
AppRegistry
    "weather" → metadata

MiniAppRuntime
    "weather" → WeatherApp&
```

The ID is the join key between metadata and executable instance.

---

## 6. IMiniApp Contract

Introduce a minimal runtime-facing interface.

Conceptually:

```cpp
namespace cardputer_hub::apps {

class IMiniApp {
  public:
    virtual ~IMiniApp() = default;

    virtual void onActivate() = 0;
    virtual void onDeactivate() = 0;

    virtual void update(
        const core::InputEvents& input,
        std::chrono::milliseconds elapsed) = 0;
};

}
```

Exact formatting may follow repository conventions, but the semantic contract should remain this small.

### `onActivate()`

Called exactly once when an inactive application successfully becomes active.

It may:

```text
invalidate its local frame cache
reset transient UI state
prepare its initial View
```

It must not:

```text
start global Connectivity
reconfigure Wi-Fi
take ownership of global Services
mutate AppRegistry
mutate CapabilityRegistry
```

Long-lived Service startup remains independent from view activation.

### `update(...)`

Called only for the currently active Mini App.

It receives:

```text
semantic InputEvents
UI elapsed time
```

The elapsed value comes from the existing Application Shell / UiScheduler cadence.

A Mini App may:

```text
read its Services
update local presentation state
render through its injected display abstraction
dispatch logical Actions
```

The runtime itself does not render.

### `onDeactivate()`

Called exactly once before the application stops being active.

It may discard transient application UI state.

It must not imply destruction.

Application objects remain alive because composition is static.

### Lifecycle object ownership

`IMiniApp` objects are owned by the normal composition root or another explicit process-lifetime owner.

`MiniAppRuntime` keeps non-owning references/pointers only.

The runtime must never:

```text
new/delete a Mini App
own unique_ptr<IMiniApp>
construct application-specific classes
destroy a Mini App during switching
```

This keeps lifetime deterministic on the embedded target.

---

## 7. MiniAppRuntime Public Contract

Introduce a runtime with an API conceptually equivalent to:

```cpp
class MiniAppRuntime {
  public:
    MiniAppRuntime(
        const core::AppRegistry& apps,
        const core::CapabilityRegistry& capabilities);

    MiniAppInstanceRegistrationResult registerInstance(
        std::string appId,
        IMiniApp& app);

    MiniAppEligibility eligibility(
        const std::string& appId) const;

    MiniAppActivationResult activate(
        const std::string& appId);

    MiniAppDeactivationResult deactivate();

    MiniAppUpdateResult update(
        const core::InputEvents& input,
        std::chrono::milliseconds elapsed);

    [[nodiscard]] bool hasActiveApp() const noexcept;

    [[nodiscard]] std::optional<std::string> activeAppId() const;
};
```

Names may be adjusted slightly if implementation discovers a clearer repository-consistent naming scheme, but behavior must remain explicit and typed.

Do not use bool-only APIs for operations with meaningful failure modes.

---

## 8. Runtime Instance Registration

Add an explicit result enum conceptually equivalent to:

```cpp
enum class MiniAppInstanceRegistrationResult : std::uint8_t {
    Registered,
    InvalidId,
    UnknownDescriptor,
    DuplicateId,
};
```

### Registration requirements

`registerInstance(id, app)` succeeds only when:

```text
id is non-empty
AppRegistry contains the exact ID
no runtime instance is already registered for the exact ID
```

IDs remain:

```text
case-sensitive
exact
opaque
```

Examples:

```text
weather != Weather
devices != DEVICES
```

### Unknown metadata

Runtime registration must reject:

```text
runtime instance exists
but no AppDescriptor exists
```

Do not implicitly create metadata.

Correct order:

```text
AppRegistry::registerApp(descriptor)
MiniAppRuntime::registerInstance(descriptor.id, instance)
```

This prevents executable applications from existing outside the metadata model.

### Duplicate registration

A duplicate runtime registration:

```text
must be rejected
must preserve the original instance
must not alter registration order
must not alter active state
```

### Registration does not activate

This must remain true:

```text
registerInstance(...)
    ≠
activate(...)
```

Application construction, registration, and user-visible activation are separate concepts.

---

## 9. Runtime Eligibility

Introduce typed application eligibility.

Conceptually:

```cpp
enum class MiniAppEligibility : std::uint8_t {
    Eligible,
    UnknownApp,
    MissingInstance,
    MissingCapability,
};
```

Eligibility is determined from:

```text
AppRegistry descriptor existence
        +
MiniAppRuntime instance existence
        +
CapabilityRegistry current state
```

Algorithm:

```text
descriptor missing
    → UnknownApp

descriptor exists but instance missing
    → MissingInstance

one or more required capabilities unavailable
    → MissingCapability

otherwise
    → Eligible
```

Empty `requiredCapabilities` means no capability restriction.

Do not cache eligibility permanently.

Capability availability is dynamic.

---

## 10. Capability Checks

Plan 028 must connect `AppRegistry::requiredCapabilities` to `CapabilityRegistry`.

This is the first point where the two Phase 1 foundations become operationally related.

For every required capability:

```cpp
capabilities.isAvailable(capabilityId)
```

must be true before activation succeeds.

Example:

```text
WeatherApp
requires:
    WIFI
    WEATHER_SERVICE
```

If:

```text
WIFI = available
WEATHER_SERVICE = unavailable
```

then:

```text
WeatherApp = MissingCapability
```

### No capability inference

`MiniAppRuntime` must not infer availability from concrete Service state.

Forbidden examples:

```text
NetworkService Connected → assume WIFI
HostService has selected host → assume HOST_SERVICE
Bluetooth On → assume BLUETOOTH
```

Only `CapabilityRegistry` answers the generic capability question.

Publication of actual production capabilities remains owned by appropriate Services or future composition work.

Plan 028 should not invent that publication policy merely to make test capabilities available.

Native tests can explicitly register capabilities in `CapabilityRegistry`.

---

## 11. Activation Semantics

Introduce an activation result conceptually equivalent to:

```cpp
enum class MiniAppActivationResult : std::uint8_t {
    Activated,
    AlreadyActive,
    UnknownApp,
    MissingInstance,
    MissingCapability,
};
```

### Successful activation from idle

```text
runtime idle
activate(A)
A eligible
    ↓
A.onActivate()
    ↓
A becomes active
    ↓
Activated
```

### Re-activating the same application

```text
A active
activate(A)
```

returns:

```text
AlreadyActive
```

The runtime decides this before re-checking eligibility, so a lost required
capability does not change the result until the next `update()`. It must not
call:

```text
A.onDeactivate()
A.onActivate()
```

again.

Transient app state is preserved.

### Switching applications

For:

```text
A active
activate(B)
```

the runtime must first validate **B** completely.

Only after B is known to be eligible:

```text
A.onDeactivate()
B becomes selected
B.onActivate()
```

This ordering is important.

A failed attempt to open B must not destroy A's current state.

Correct:

```text
A active
activate(B)
B missing capability
    ↓
A remains active
```

Incorrect:

```text
deactivate A
discover B cannot run
end with no active app
```

### Switch ordering

On a valid switch the lifecycle order is:

```text
A.onDeactivate()
B.onActivate()
```

Never:

```text
B.onActivate()
A.onDeactivate()
```

Only one Mini App may be active at once.

---

## 12. Deactivation Semantics

Introduce:

```cpp
enum class MiniAppDeactivationResult : std::uint8_t {
    Deactivated,
    AlreadyInactive,
};
```

When an application is active:

```text
deactivate()
    ↓
active.onDeactivate()
    ↓
clear active identity
    ↓
Deactivated
```

Calling `deactivate()` while idle is safe and returns:

```text
AlreadyInactive
```

It must not invoke any lifecycle callback.

Deactivation must not:

```text
delete the instance
unregister metadata
remove capabilities
stop unrelated Services
change configuration
```

---

## 13. Runtime Update Semantics

Introduce:

```cpp
enum class MiniAppUpdateResult : std::uint8_t {
    Idle,
    Updated,
    DeactivatedMissingCapability,
};
```

### Idle runtime

If no application is active:

```cpp
update(...)
```

returns:

```text
Idle
```

and forwards no input.

### Active runtime

Before forwarding an update, re-evaluate the active application's required capabilities.

If still eligible:

```text
MiniAppRuntime::update
        ↓
activeApp.update(input, elapsed)
        ↓
Updated
```

### Runtime capability loss

Capability requirements must also be enforced **while running**, not only at launch.

Example:

```text
WeatherApp active
WIFI capability disappears
```

On the next runtime update:

```text
detect missing required capability
        ↓
WeatherApp.onDeactivate()
        ↓
clear active app
        ↓
do not forward the current input
        ↓
DeactivatedMissingCapability
```

Do not automatically reactivate an application when the capability later returns.

The user must explicitly launch it again.

This avoids hidden application state changes.

### Capability additions

If an active application's existing requirements remain satisfied, addition or removal of unrelated capabilities must have no effect.

---

## 14. ApplicationShell Integration

Plan 028 must make the runtime part of the real UI execution path, even though no production Mini App is registered yet.

Extend `ApplicationShell` with a `MiniAppRuntime&` dependency.

Conceptually:

```cpp
ApplicationShell(
    services::HostService& hosts,
    services::NetworkService& network,
    core::ActionBus& actions,
    core::IDisplayAdapter& display,
    HostSettings& settings,
    WiFiSettings& wifiSettings,
    services::AudioService& audio,
    MiniAppRuntime& miniApps);
```

Exact ordering should follow current constructor conventions.

### Existing behavior

When:

```text
miniApps.hasActiveApp() == false
```

the current shell behavior must remain unchanged.

That includes:

```text
Home
Tab → Settings
Bluetooth
Wi-Fi
Sound volume
Home wave
battery
host status
Wi-Fi status
page transitions
```

### Active application behavior

When a Mini App is already active, `ApplicationShell::update(...)` should route the scheduled UI update to the Mini App runtime instead of processing Home/Settings input.

Conceptually:

```text
ApplicationShell::update
        │
        ├── Mini App active
        │      ↓
        │   miniApps.update(...)
        │
        └── no Mini App active
               ↓
           existing shell flow
```

ApplicationShell must not inspect the concrete active application type.

Forbidden:

```cpp
if (active == "weather") ...
if (active == "devices") ...
```

No concrete Mini App headers belong in `application_shell.cpp`.

### Capability-loss return

If:

```text
miniApps.update(...)
    → DeactivatedMissingCapability
```

the shell must recover to a known built-in state.

For Plan 028 use:

```text
Home
```

as that state.

Reset/invalidate shell presentation state as necessary so Home renders correctly on the same or next scheduled UI frame.

Do not leave the screen displaying stale Mini App pixels while the runtime has no active application.

### No activation UI yet

Plan 028 does not add a keyboard shortcut, Settings row, Home item, or Action for activating a Mini App.

Native shell tests may pre-activate a fake Mini App directly before calling `ApplicationShell::update()`.

Plan 029 will add the normal Launcher activation path.

---

## 15. Navigation Boundary

Do not overload the current global `NavigationStack` with premature Mini App internal-view semantics.

Plan 028 deals only with:

```text
application activation
application deactivation
active-app dispatch
```

It does not introduce:

```text
Weather/current
Weather/forecast
VPS/server/123
DeviceManager/host/4
```

The existing `AppDescriptor::entryRoute` remains opaque and unchanged.

`MiniAppRuntime` binds instances by:

```text
AppDescriptor::id
```

not by:

```text
entryRoute
```

The future Launcher may use `entryRoute` as part of shell navigation integration, but Plan 028 must not invent route syntax or parameters.

A concrete Mini App remains free to own its future internal view model, with System Core navigation primitives used where appropriate.

Do not introduce a generic polymorphic `View` hierarchy simply because the Phase 5 roadmap mentions Mini App views.

Only add abstractions that are required by the runtime delivered in this plan.

---

## 16. Rendering Boundary

`MiniAppRuntime` must not depend on `IDisplayAdapter`.

It must not:

```text
clear screen
draw headers
draw footers
manage transitions
render errors
render missing-capability UI
```

Rendering belongs to:

```text
ApplicationShell
or
the active Mini App
```

A future concrete app receives its display dependency explicitly.

Example:

```cpp
WeatherApp(
    WeatherService& weather,
    core::ActionBus& actions,
    core::IDisplayAdapter& display);
```

The runtime should remain usable in a native host test without display hardware or a fake display.

---

## 17. Input Boundary

The active Mini App receives the already-translated semantic `InputEvents`.

Do not give Mini Apps access to:

```text
Cardputer keyboard matrix
M5Cardputer keyboard objects
raw key scan state
ESP32 GPIO
```

The pipeline remains:

```text
Cardputer keyboard
        ↓
KeyboardAdapter
        ↓
semantic InputEvents
        ↓
UiScheduler
        ↓
ApplicationShell
        ↓
MiniAppRuntime
        ↓
active IMiniApp
```

Plan 028 does not introduce global shortcut interception for active Mini Apps.

That belongs to a later shell-control plan because shortcut precedence must be designed together with text-entry behavior and Launcher navigation.

---

## 18. Service Lifecycle Is Explicitly Deferred

Do **not** create a generic `IService` abstraction in this plan.

Existing Services have intentionally different lifecycle contracts.

Examples include:

```text
HostService
NetworkService
AudioService
BatteryService
```

Some:

```text
start once and update continuously
```

while others:

```text
sample periodically
or
have no generic start/stop contract
```

Forcing them into:

```cpp
IService::start()
IService::stop()
IService::update()
```

before there is a concrete application-scoped requirement would create an artificial abstraction.

Plan 028 therefore covers:

```text
Mini App lifecycle
```

but not:

```text
generic Service lifecycle management
```

Current long-lived Services continue to be composed and updated from the normal firmware composition root.

A later Phase 5/9 plan may introduce lifecycle ownership for a concrete Service if Weather or another feature proves that such a contract is required.

Do not solve hypothetical Service lifecycle problems in advance.

---

## 19. Normal Firmware Composition

Normal firmware should now construct:

```text
core::AppRegistry
core::CapabilityRegistry
apps::MiniAppRuntime
```

Conceptually:

```cpp
core::AppRegistry appRegistry;
core::CapabilityRegistry capabilities;
apps::MiniAppRuntime miniApps(appRegistry, capabilities);
```

and inject `miniApps` into `ApplicationShell`.

The relationship becomes:

```text
main.cpp
├── ConfigurationService
├── NetworkService
├── BluetoothService
├── HostService
├── AudioService
├── BatteryService
├── ActionBus
├── AppRegistry
├── CapabilityRegistry
├── MiniAppRuntime
├── HostSettings
├── WiFiSettings
└── ApplicationShell
```

Do not register a fake production application.

Do not register placeholder descriptors such as:

```text
test
demo
hello
example
```

Production `AppRegistry` may therefore be empty after Plan 028.

That is intentional.

The runtime exists and is part of the actual shell path, ready for Plan 029/030.

---

## 20. Existing Built-In Screens Remain System UI

Do not convert existing built-in views into Mini Apps in this plan.

These remain built-in shell/system surfaces:

```text
Home
Settings
Wi-Fi Settings
Bluetooth / HostSettings
```

In particular, do not opportunistically create:

```text
SettingsApp
WifiApp
BluetoothApp
```

Doing so would combine runtime foundation with UI migration and make regressions substantially harder to isolate.

The full Device Manager can become a Mini App in a later plan once the runtime and Launcher boundaries are stable.

---

## 21. Runtime Failure Isolation

A Mini App runtime failure must not mutate unrelated global systems.

These runtime-level failures:

```text
unknown app ID
missing runtime instance
missing capability
duplicate instance registration
```

must not:

```text
disable Bluetooth
disconnect Wi-Fi
change selected host
modify configuration
change sound volume
alter another AppDescriptor
remove capabilities
restart Services
```

A failed switch preserves the currently active application.

Example:

```text
WeatherApp active
activate("vps")
VPS_SERVICE missing
```

must result in:

```text
WeatherApp remains active
WeatherApp receives no lifecycle callback
VpsMonitorApp receives no lifecycle callback
```

Capability disappearance from the currently active application is the one case where the runtime deliberately deactivates that app.

That deactivation affects only application presentation/lifecycle.

---

## 22. Invalid Elapsed Time

Follow the repository's existing UI scheduling conventions.

`MiniAppRuntime` must not implement its own timing loop or frame scheduler.

It receives the elapsed duration already selected by the Application Shell / UiScheduler path.

The runtime forwards that value to the active app unchanged.

Do not:

```text
sleep
busy wait
schedule catch-up updates
invent FPS
accumulate separate app time
```

Any future Mini App animation should follow the same injected elapsed-time model used by current shell presentation.

---

## 23. Static Composition and Allocation Policy

Initial Mini App composition is static.

Plan 028 must not implement:

```text
dynamic library loading
filesystem plugins
application binaries on SD
runtime construction by string
reflection
RTTI-based factory lookup
```

Application objects should be ordinary process-lifetime C++ objects.

Example future composition:

```cpp
WeatherApp weather(...);

appRegistry.registerApp({...});
miniApps.registerInstance("weather", weather);
```

No heap allocation is required by the runtime design beyond containers already consistent with existing System Core foundations.

Keep the implementation simple and deterministic.

---

## 24. Architecture Enforcement

Update `scripts/check_architecture.py` and its tests only if necessary so the new layer is explicitly understood.

The intended allowed dependency is:

```text
apps/runtime
    → core
```

Future concrete Mini Apps may depend on:

```text
apps
    → services
    → core
```

but runtime infrastructure itself should remain independent of concrete Services.

Architecture checks should prevent accidental dependencies such as:

```text
core/app_registry → apps/runtime
apps/runtime → hardware
apps/runtime → connectivity
apps/runtime → specific application
```

Do not weaken existing architecture rules merely to make new includes pass.

---

## 25. Source and Build Integration

Expected new production files:

```text
src/apps/runtime/mini_app.h
src/apps/runtime/mini_app_runtime.h
src/apps/runtime/mini_app_runtime.cpp
```

Expected new test suite:

```text
test/test_mini_app_runtime/test_main.cpp
```

Add or extend shell-level runtime-dispatch tests in the most appropriate existing/new native suite.

If no focused shell suite currently exists, adding:

```text
test/test_application_shell/test_main.cpp
```

is acceptable rather than hiding generic shell-runtime behavior inside HostSettings or WiFiSettings tests.

Update:

```text
main/CMakeLists.txt
```

to include the runtime implementation in the production firmware source list.

Update native PlatformIO source filters/configuration as required by the existing test model.

Do not introduce a new build system or runtime-specific build path.

---

## 26. Native Runtime Test Matrix

The focused `test_mini_app_runtime` suite must cover at least:

### Registration

* empty app ID rejected;
* runtime registration for unknown AppDescriptor rejected;
* successful instance registration;
* exact ID matching;
* case-sensitive IDs;
* duplicate instance registration rejected;
* duplicate rejection preserves original instance;
* registration does not activate the instance;
* registration invokes no lifecycle callbacks.

### Eligibility

* unknown descriptor → `UnknownApp`;
* known descriptor without runtime instance → `MissingInstance`;
* descriptor with zero capability requirements → eligible;
* all required capabilities available → eligible;
* one missing required capability → `MissingCapability`;
* multiple requirements with one missing → `MissingCapability`;
* unrelated unavailable capability has no effect;
* capability registration changes eligibility dynamically;
* capability removal changes eligibility dynamically.

### Activation

* eligible application activates;
* activation calls `onActivate()` exactly once;
* `activeAppId()` reflects activation;
* activating the same app again → `AlreadyActive`;
* `AlreadyActive` is decided before eligibility, including after required-capability loss;
* repeated activation does not call lifecycle callbacks again;
* unknown app activation does not mutate active state;
* missing-instance activation does not mutate active state;
* missing-capability activation does not mutate active state.

### Switching

* A → B calls `A.onDeactivate()` before `B.onActivate()`;
* successful switch changes active ID;
* failed B eligibility preserves A;
* failed switch calls neither A deactivate nor B activate;
* switch A → B → A preserves deterministic callback counts.

### Deactivation

* active application deactivates exactly once;
* active ID clears;
* idle deactivation returns `AlreadyInactive`;
* idle deactivation calls no callback.

### Update

* idle runtime returns `Idle`;
* idle runtime does not forward input;
* active app receives exact semantic input;
* active app receives exact elapsed duration;
* only active application receives updates;
* inactive registered apps receive no updates.

### Capability loss while active

* removal of required capability deactivates before app update;
* the input from the invalidating frame is not forwarded;
* result is `DeactivatedMissingCapability`;
* active ID clears;
* `onDeactivate()` called once;
* unrelated capability removal does not deactivate;
* capability restoration does not automatically reactivate.

---

## 27. ApplicationShell Regression Tests

Add coverage proving the runtime is part of the real shell path.

Use test-only fake Mini Apps; do not add production demo applications.

Required scenarios:

### No active app

When no Mini App is active:

```text
Home behavior remains unchanged
Tab still opens Settings
existing Settings rows still work
```

### Active app

Pre-activate a fake app directly through `MiniAppRuntime`.

Then:

```text
ApplicationShell::update(...)
```

must forward scheduled input/elapsed time to that application.

The shell must not simultaneously process the same input as Home/Settings input.

One semantic input event must have one active application-level consumer.

### Runtime deactivation

After explicit runtime deactivation, the shell invalidates Home caches and
resumes built-in UI processing on the next idle update, without requiring a
navigation key to force a redraw.

### Capability loss

When an active fake app loses a required capability:

```text
runtime deactivates it
shell returns to Home
Home is invalidated/redrawn correctly
```

No stale app frame should remain as the authoritative presentation state.

### Existing system behavior

Preserve regression coverage for:

```text
Home wave
battery updates
Host status
Wi-Fi status
Settings navigation
Bluetooth route
Wi-Fi route
volume adjustment
modal input ownership
```

Plan 028 should not require changing their semantics.

---

## 28. TDD Implementation Sequence

Follow focused RED-GREEN-REFACTOR rather than implementing the complete runtime first.

Recommended sequence:

1. Add `IMiniApp` and the initial focused runtime tests that fail because `MiniAppRuntime` does not exist.

2. Add runtime instance registration tests:

   * valid registration;
   * unknown descriptor;
   * duplicate instance;
   * exact/case-sensitive IDs.

3. Implement only runtime registration and lookup required to make those tests green.

4. Add eligibility tests against `CapabilityRegistry`.

5. Implement descriptor/instance/capability eligibility evaluation.

6. Add idle and first-activation tests.

7. Implement activation from idle.

8. Add same-app repeated activation behavior.

9. Add application-switch ordering tests.

10. Implement pre-validation of the target followed by:

    ```text
    old deactivate
    new activate
    ```

11. Add deactivation tests.

12. Implement explicit deactivation.

13. Add active update/input/elapsed forwarding tests.

14. Implement update dispatch.

15. Add required-capability-loss tests while active.

16. Implement runtime revalidation and automatic deactivation on required-capability loss.

17. Add ApplicationShell fake-app dispatch tests.

18. Inject `MiniAppRuntime` into `ApplicationShell` and preserve the existing no-active-app path.

19. Add capability-loss-to-Home shell regression.

20. Compose:

    ```text
    AppRegistry
    CapabilityRegistry
    MiniAppRuntime
    ```

    in `main.cpp`.

21. Update production build sources.

22. Update architecture checker/tests if required.

23. Run focused tests throughout iteration.

24. After the final material code change, run the complete repository gates once.

25. Record actual results and firmware size in the plan completion section.

Do not repeatedly clean the build during normal iteration.

Preserve existing build caches.

---

## 29. Documentation Updates

Update `docs/ARCHITECTURE.md`.

### App Registry section

Change the Phase 5 wording from purely future runtime integration to the delivered boundary:

```text
AppRegistry remains metadata-only.
MiniAppRuntime binds executable instances by exact AppDescriptor ID.
```

### Mini App Model section

Document:

```text
IMiniApp
onActivate
update
onDeactivate
static process-lifetime instances
```

Clarify that Mini Apps own application-specific view state but not shared Services.

### Views and Navigation section

Record that Plan 028 adds application activation/lifecycle only.

Launcher routing and internal Mini App view navigation remain pending.

### Capabilities section

Document that `MiniAppRuntime` now evaluates `AppDescriptor::requiredCapabilities` using `CapabilityRegistry` at activation and while active.

Make clear that:

```text
CapabilityRegistry availability is authoritative
runtime does not infer capabilities from Services
```

### Initial Development Order

Update Phase 5 from:

```text
Not implemented
```

to a partial state.

Expected Phase 5 checklist after Plan 028:

```text
[x] MiniApp runtime interface and lifecycle foundation.
[ ] AppRegistry-driven Launcher integration.
[ ] Service lifecycle composition where required by concrete Services.
[x] Runtime capability checks for launching/running Mini Apps.
```

If the roadmap keeps the wording "Mini App view model", document that per-app view state remains application-owned and that Plan 028 intentionally does not introduce a universal polymorphic View hierarchy.

### README

Update the phase/status table so Phase 5 is:

```text
Partial
```

and mention:

```text
Mini App runtime/lifecycle foundation delivered;
Launcher integration pending.
```

Do not claim any production Mini Apps exist.

### plans/README.md

Add:

```text
028 — Mini App Runtime Foundation
```

with the final disposition.

### UI requirements

Only update `docs/UI_REQUIREMENTS.md` if wording currently states that no Mini App lifecycle/runtime exists.

Do not describe Launcher visuals as delivered.

### Device manual

The user-facing device guide should require either no change or only wording confirming that no user-visible Mini App Launcher exists yet.

Internal runtime infrastructure is not a supported user control.

---

## 30. Configuration and Persistence

Plan 028 requires no persistent configuration change.

Do not introduce schema version 5.

The runtime state:

```text
registered instances
active app
capability eligibility
```

is process-local and transient.

After reboot:

```text
no Mini App is active
```

unless a later plan explicitly defines startup application restoration.

Do not persist:

```text
active app ID
runtime registration
runtime lifecycle state
```

Plan 028 also does not add:

```text
enabled Mini Apps
app ordering
app settings
```

Those belong to later configuration work.

---

## 31. ActionBus

Do not add application-launch Actions in Plan 028.

Specifically defer:

```text
app.open
app.close
ui.launcher
ui.home
```

or similarly named new application-control Actions.

Reason:

The correct global navigation semantics should be introduced together with the Launcher so:

```text
Home
Launcher
Back
global shortcuts
text-entry ownership
active Mini App
```

can be tested as one coherent user-facing flow.

Plan 028 provides typed C++ runtime operations only.

This keeps the foundation independent from premature input policy.

---

## 32. Firmware Size

Plan 026 established size observability and Plan 027 retained substantial free application space.

Plan 028 must run:

```text
make firmware-size
```

after the production build.

Record:

```text
application image size
delta from Plan 027 closeout
free bytes
free percentage
```

No hard size budget should be introduced solely for this runtime foundation.

The expected increase should be small because:

```text
no Launcher assets
no production Mini Apps
no new Service protocols
no additional hardware libraries
```

are included.

Investigate an unexpectedly large binary increase before merge.

Do not change the partition layout.

---

## 33. Verification Commands

During focused development use the smallest relevant suites.

Conceptually:

```text
uv run --frozen pio test -e native -f test_mini_app_runtime
```

plus the focused ApplicationShell suite.

Before completion run the repository's applicable final gates:

```text
make host-check
make firmware-check
make firmware-size
```

Also run:

```text
git diff --check
```

Record the actual:

```text
Python test count
native test count
architecture result
format/lint result
firmware result
firmware size
```

in the completion record.

Do not copy Plan 027 counts into Plan 028.

Use the actual results from the implementation branch.

---

## 34. Physical Cardputer-Adv Regression Smoke

A full physical acceptance matrix is not required because Plan 028 introduces no user-visible application.

However, because `ApplicationShell` is modified, perform one focused regression smoke on real hardware after all automated checks pass.

Verify:

1. device boots normally to Home;

2. Home still shows:

   ```text
   selected host
   BLE state
   Wi-Fi status
   battery
   ambient wave
   ```

3. plain Tab opens Settings;

4. Bluetooth Settings still opens and returns correctly;

5. Wi-Fi Settings still opens and returns correctly;

6. Sound volume still changes;

7. ordinary Home input is not swallowed by the dormant Mini App runtime;

8. existing BLE/Wi-Fi background state continues updating while navigating built-in screens.

No hidden debug Mini App needs to be exposed on the physical device.

If all production registries are empty as designed, device behavior should be indistinguishable from Plan 027.

Record:

```text
physical regression smoke: PASS / FAIL
```

in the completion section.

---

## 35. Acceptance Criteria

Plan 028 is complete only when all of the following are true.

### Runtime architecture

* `IMiniApp` exists under the Application layer;
* `MiniAppRuntime` exists under the Application layer;
* System Core does not depend on Mini App runtime types;
* `AppRegistry` remains metadata-only;
* `CapabilityRegistry` remains a generic capability store;
* runtime instances are bound to descriptors by exact app ID;
* runtime does not own Mini App object lifetime.

### Registration

* runtime registration requires an existing descriptor;
* invalid and duplicate registrations are rejected;
* registration never activates an application;
* exact case-sensitive identity is preserved.

### Eligibility

* required capabilities are evaluated through `CapabilityRegistry`;
* activation fails safely when capabilities are missing;
* unrelated capability state has no effect;
* capability eligibility is dynamic rather than permanently cached.

### Lifecycle

* exactly one Mini App may be active;
* first activation calls `onActivate()` once;
* reactivating the same app is a no-op result;
* switching validates the target before deactivating the source;
* valid switching calls old deactivate before new activate;
* explicit deactivation calls `onDeactivate()` once;
* idle deactivation is safe.

### Running capability checks

* required capabilities are revalidated while active;
* capability loss deactivates the app before its next update;
* invalidating-frame input is not forwarded;
* capability restoration does not silently reactivate the app.

### Application Shell

* an active Mini App receives scheduled input and elapsed time;
* the built-in shell does not process the same input simultaneously;
* no active app preserves all existing Home/Settings behavior;
* runtime capability-loss recovery returns to Home safely;
* ApplicationShell contains no application-specific type checks.

### Composition

* AppRegistry, CapabilityRegistry, and MiniAppRuntime exist in normal firmware composition;
* no production demo Mini App is added;
* existing system screens remain built-in;
* no schema migration is introduced;
* no new Service lifecycle abstraction is introduced.

### Quality

* focused runtime tests pass;
* shell regression tests pass;
* architecture checks pass;
* formatting/lint pass;
* complete host gate passes;
* production firmware build passes;
* firmware size is recorded;
* focused physical shell regression passes;
* architecture, README, plan index, and applicable docs are updated.

---

## 36. Explicitly Deferred Follow-Up

The expected next step after Plan 028 is:

```text
029 — AppRegistry-Driven Launcher
```

Plan 029 should consume the runtime created here and add the first real user-facing activation path:

```text
Home / Launcher
      ↓
AppRegistry descriptors
      ↓
Capability eligibility
      ↓
select application
      ↓
MiniAppRuntime::activate(...)
```

Plan 029 should likely include:

```text
Launcher list
AppRegistry registration order
eligible/unavailable presentation
opening a Mini App
returning from a Mini App
Back/Home semantics
entryRoute integration
generic active-app navigation
```

but it should **not** need to redesign Mini App ownership or lifecycle.

After that, a suitable first real Mini App can validate the architecture.

A likely sequence is:

```text
028 Mini App Runtime Foundation
        ↓
029 AppRegistry-Driven Launcher
        ↓
030 Device Manager Mini App integration
        ↓
031 WeatherService
        ↓
032 WeatherApp + Home weather summary
```

The exact numbering after 028 may be adjusted based on implementation findings, but Plan 028 must leave a clean boundary for those changes.

---

## 37. Completion Record

Fill this section during implementation.

### Delivered Behavior

```text
IMiniApp + MiniAppRuntime under apps/runtime
registration, eligibility, activate/switch/deactivate/update
ApplicationShell active-app dispatch and Home recovery
normal firmware composition with empty production registries
```

### TDD Evidence

Record meaningful RED failures observed during the incremental implementation.

```text
test_mini_app_runtime first failed to compile: mini_app_runtime.h missing
registration helper then failed RED after the header existed:
  AppRegistry rejected descriptors because the fixture moved the ID
  before copying displayName/entryRoute (Expected Registered, was InvalidDescriptor)
18 runtime tests then passed
4 ApplicationShell dispatch tests passed after MiniAppRuntime injection
later review coverage added AlreadyActive-before-eligibility and
explicit-deactivation Home invalidation; current suites are 19 runtime
tests and 5 ApplicationShell tests
```

### Automated Verification

```text
make host-check      PASS (72 Python, 321 native, architecture, format, lint)
make firmware-check  PASS
make firmware-size   PASS (0x11a360 / 1,156,448 bytes; +1,328 from Plan 027;
                           2,185,888 bytes / 65% free in 3,342,336-byte app partition)
git diff --check     PASS
```

### Physical Regression

```text
Cardputer-Adv focused shell smoke: PENDING operator confirmation
```

### Final Scope Audit

Confirm explicitly:

```text
[x] no Launcher added
[x] no production Mini App added
[x] no schema v5
[x] no generic IService abstraction
[x] AppRegistry remains metadata-only
[x] no core → apps dependency
[x] existing built-in UI behavior preserved
```
