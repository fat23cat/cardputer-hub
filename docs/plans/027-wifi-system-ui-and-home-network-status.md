# 027 — Wi-Fi System UI and Home Network Status

Status: **Implemented** — automated checks and production firmware build
passed; physical Cardputer-Adv acceptance remains pending.

## 0. Current Closeout Status

Software implementation completed on **2026-09-15**, with review follow-up
and device-driven UX changes the same day and on **2026-09-16**.

Delivered:

* `NetworkService` Action handlers `network.set-enabled`, `network.configure`,
  and `network.forget`;
* Home live Wi-Fi glyph and semantic status dot from `NetworkService`;
* three-row Settings with `ui.wifi` navigation;
* built-in Wi-Fi Settings for enable/disable, manual SSID/passphrase entry,
  change, and confirmed forget;
* logical row focus so Change/Forget stay selected when Signal appears or
  disappears;
* suffix editor viewport for maximum-length SSID and passphrase;
* unconfigured first-run focused on Configure network, with no dead enable
  toggle;
* right-aligned header status shared with Bluetooth;
* unmarked Escape (backtick) and Fn+backtick cancel Wi-Fi editors and Forget;
* contextual transactional footers after device validation: lists stay clean;
  editors, Forget, pairing, rename, and delete show quiet cancel/confirm hints;
* password editor shows only the newest character for about two seconds;
* architecture, UI, plan-index, and device-guide updates.

Final software evidence: `make host-check` passed (297 native tests plus
Python, architecture, format, and lint); `make firmware-check` passed; and
`make firmware-size` recorded application image size `0x119e30` bytes
(1,154,608), with 2,187,728 bytes (65%) remaining in the 3,342,336-byte app
partition. That is a small increase from the Plan 026 closeout image, as
expected for the Settings UI and contextual transactional footers.

Device validation on the Cardputer-Adv changed the original no-footer policy
for built-in system screens. Persistent lists and status views stay clean;
editors, Forget, pairing, rename, and delete now use compact contextual
footers when cancel or confirm is available.

Physical Cardputer-Adv acceptance from section 34 remains operator work.

Suggested branch:

```text
feat/027-wifi-system-ui-and-home-network-status
```

## 1. Goal

Turn the Wi-Fi runtime delivered by Plan 025 into a complete device-facing feature.

The firmware already has:

```text
ConfigurationService
        ↓
NetworkService
        ↓
connectivity::WiFiService
        ↓
IWifiAdapter
        ↓
Esp32WifiAdapter
```

Normal firmware starts and updates that stack, persists one station network, restores enabled intent after reboot, retries connections, and exposes application-facing Wi-Fi status.

However, the current Application Shell still renders a hardcoded `OFFLINE` placeholder and does not provide Wi-Fi configuration UI.

This plan should deliver:

```text
Home
    ↓
live compact Wi-Fi status

Settings
├── Bluetooth
├── Wi-Fi
└── Sound volume

Wi-Fi Settings
├── enable / disable
├── configured network information
├── signal strength while connected
├── configure / change network
└── forget network
```

The Home status must use a new compact Wi-Fi glyph plus a separate semantic status indicator.

The Home status must not display:

```text
OFFLINE
ONLINE
SSID
RSSI
```

The first Wi-Fi setup flow uses manual SSID and passphrase entry.

Network discovery/scanning is explicitly deferred to a separate future plan.

---

## 2. Current State

Plan 025 delivered persistent Wi-Fi configuration and normal runtime composition.

`NetworkService` currently owns the application-facing Wi-Fi domain boundary:

```cpp
NetworkResult start();
void update(std::chrono::milliseconds elapsed);

NetworkResult configure(std::string ssid, std::string passphrase);
NetworkResult setEnabled(bool enabled);
NetworkResult forget();

WifiStatusSnapshot status() const;
```

The domain snapshot contains:

```text
configured
enabled
connection
ssid
signalStrengthDbm
lastResult
```

and exposes only these application-level connection states:

```text
Off
Connecting
Connected
Error
```

Connectivity retry states are intentionally hidden behind `Connecting`.

Normal firmware already constructs and updates `NetworkService`.

The missing connection is:

```text
NetworkService
        ↓
ApplicationShell / WiFiSettings
```

The current Home screen instead contains a static:

```text
Wi-Fi icon
OFFLINE
```

placeholder.

The current Settings screen has only:

```text
01 Bluetooth
02 Sound volume
```

and the selection logic assumes two rows.

The current `IWifiAdapter` contract supports:

```text
initialize station
connect
disconnect
link state
RSSI
```

It does not support network discovery.

Therefore Wi-Fi scanning must not be implemented as an incidental part of this plan.

---

## 3. Scope

Included:

- wire `NetworkService` into the Application Shell;
- replace the Home `OFFLINE` placeholder with live Wi-Fi state;
- replace the existing Wi-Fi bitmap with a new compact geometric Wi-Fi glyph;
- add a separate status dot next to the glyph;
- add a Wi-Fi row to the built-in Settings menu;
- add a dedicated built-in Wi-Fi Settings screen;
- enable and disable persisted Wi-Fi intent;
- manually configure an SSID and passphrase;
- change the configured network without forgetting the previous configuration first;
- forget the configured network with explicit confirmation;
- display the configured SSID inside Wi-Fi Settings only;
- display RSSI inside Wi-Fi Settings while connected;
- display domain-level errors without exposing Connectivity implementation details;
- add logical network Actions;
- preserve incremental UI rendering;
- preserve non-blocking input, Bluetooth, host, battery, and Wi-Fi updates;
- automated tests;
- production firmware build;
- firmware-size observation using the tooling delivered by Plan 026;
- physical Cardputer-Adv acceptance.

Not included:

- Wi-Fi network scanning;
- access-point discovery;
- multiple remembered networks;
- network priority;
- roaming policy;
- captive portal support;
- enterprise Wi-Fi;
- Bluetooth-based Wi-Fi provisioning;
- companion-based Wi-Fi provisioning;
- Web UI configuration;
- remote configuration;
- NTP or clock synchronization;
- Home RSSI bars;
- Home SSID display;
- RSSI-based status-dot color;
- animated Wi-Fi glyphs;
- changes to retry/backoff behavior;
- changes to `connectivity::WiFiService`;
- changes to `IWifiAdapter`;
- changes to `Esp32WifiAdapter` unless required to preserve compilation;
- schema version 5 unless implementation proves a persistent model change is actually required.

The existing version-4 system configuration is sufficient for this feature.

---

## 4. Ownership and Architecture

Preserve the dependency direction:

```text
Apps
    ↓
Services
    ↓
Connectivity
    ↓
Hardware
```

The UI must consume:

```text
services::NetworkService
services::WifiStatusSnapshot
services::WifiConnectionStatus
services::NetworkResult
```

The UI must not consume or interpret:

```text
connectivity::WifiState
connectivity::WifiAdapterState
IWifiAdapter
Esp32WifiAdapter
ESP-IDF Wi-Fi events
esp_netif state
```

Home and Wi-Fi Settings express user intent and render Service state.

`NetworkService` continues to own:

```text
persisted enabled intent
configured SSID/passphrase
configuration validation boundary
application-facing status mapping
connect / disconnect request policy
last domain result
```

`connectivity::WiFiService` continues to own:

```text
station lifecycle
connection attempt timeout
retry waiting
retry backoff
adapter state
RSSI retrieval
```

The Application Shell must not duplicate any of this behavior.

---

## 5. Logical Network Actions

Follow the existing ActionBus model used by host and audio behavior.

Extend `NetworkService` to implement `core::IActionHandler`, consistent with other mutable Services.

Register these logical Actions:

```text
network.set-enabled
network.configure
network.forget
```

### `network.set-enabled`

Parameters:

```text
enabled: bool
```

Dispatch to:

```cpp
NetworkService::setEnabled(enabled)
```

### `network.configure`

Parameters:

```text
ssid: string
passphrase: string
```

Dispatch to:

```cpp
NetworkService::configure(ssid, passphrase)
```

### `network.forget`

No parameters.

Dispatch to:

```cpp
NetworkService::forget()
```

Malformed or missing Action parameters must be rejected without changing configuration or Connectivity state.

A valid Action remains `Handled` even when the domain operation itself reports a failure.

The operation result remains observable through:

```cpp
NetworkService::status().lastResult
```

Do not encode Service results into UI-specific Action return values.

Normal firmware should register these handlers in the composition root alongside existing host and audio Actions.

---

## 6. Application Shell Composition

Extend `ApplicationShell` with a `NetworkService` dependency.

Conceptually:

```cpp
ApplicationShell(
    HostService& hosts,
    NetworkService& network,
    ActionBus& actions,
    IDisplayAdapter& display,
    HostSettings& hostSettings,
    WiFiSettings& wifiSettings,
    AudioService& audio);
```

Exact argument ordering may follow existing project conventions.

`src/main.cpp` must continue to own composition.

Do not introduce a service locator or global singleton.

The runtime relationship becomes:

```text
main.cpp
├── NetworkService
├── HostService
├── AudioService
├── HostSettings
├── WiFiSettings
└── ApplicationShell
```

`NetworkService::update()` must remain outside UI scheduling and continue running on every normal firmware loop.

Only rendering is scheduled by `UiScheduler`.

---

## 7. New Home Wi-Fi Indicator

Remove the current `drawWifiOfflineIcon()` concept.

The Home Wi-Fi presentation must no longer encode `Offline` into the glyph itself.

Create a neutral Wi-Fi glyph renderer, conceptually:

```cpp
drawWifiIcon(display, position, color);
```

and a separate status-indicator renderer.

### Wi-Fi glyph

The new glyph should:

- resemble the conventional Wi-Fi symbol;
- use symmetrical arcs and a small central endpoint;
- fit the existing flat pixel-art visual language;
- use one-pixel geometry;
- be approximately 12–14 pixels wide;
- be approximately 9–11 pixels high;
- use `palette::ink` in every network state;
- avoid a slash, cross, warning badge, or embedded status color;
- remain readable at the UI-required 10% fallback brightness.

The implementation may use a small static bitmap as the existing Home graphics do.

The old glyph should be removed rather than retained as a second Wi-Fi representation.

### Separate status indicator

Render a small indicator immediately to the right of the Wi-Fi glyph.

The indicator must be visually distinct from the endpoint contained inside the Wi-Fi glyph.

Use this mapping:

| Domain state | Indicator |
| --- | --- |
| `configured == false` | hollow `Ordinal` dot |
| configured, `enabled == false` | filled `Pale` dot |
| `Connecting` | filled `Blue` dot |
| `Connected` | filled `Leaf` dot |
| `Error` | filled `Vermilion` dot |

The hollow versus filled treatment is intentional.

`Not configured` and `Wi-Fi disabled` must remain distinguishable without relying only on color.

Do not use RSSI to select the color.

Do not animate the indicator while connecting.

---

## 8. Home Status-Bar Layout

The Home top line remains structurally:

```text
time        Wi-Fi indicator        battery
```

The result should be visually close to:

```text
--:--              [wifi] ●      81%
```

with appropriate pixel spacing rather than literal text placeholders.

Remove the strings:

```text
OFFLINE
ONLINE
CONNECTING
ERROR
```

from Home Wi-Fi presentation.

Do not display SSID on Home.

Do not display RSSI on Home.

The status bar should reserve enough separation between:

```text
clock
Wi-Fi glyph/status
battery percentage
```

that state changes do not shift the other elements.

The existing clock placeholder `--:--` remains unchanged in this plan.

---

## 9. Incremental Home Rendering

Home must not redraw the Wi-Fi indicator continuously.

Add cached presentation state sufficient to detect semantic Wi-Fi changes.

A representation equivalent to:

```cpp
struct HomeNetworkFrame {
    bool configured;
    bool enabled;
    services::WifiConnectionStatus connection;
};
```

is sufficient.

Do not include:

```text
SSID
passphrase
RSSI
lastResult
```

in Home render-cache equality because those values do not change Home presentation.

Redraw the Wi-Fi region when and only when the Home-visible representation changes.

Examples:

```text
not configured → configured/off
configured/off → connecting
connecting → connected
connected → error
error → connecting
```

RSSI changes while remaining `Connected` must not repaint Home.

This preserves the incremental-rendering behavior delivered by Plan 022.

---

## 10. Settings Menu

Change the built-in Settings rows from:

```text
01 Bluetooth
02 Sound volume
```

to:

```text
01 Bluetooth
02 Wi-Fi
03 Sound volume
```

Generalize row-selection behavior instead of adding another two-state conditional.

The Settings screen must support:

```text
Up
Down
;
.
Enter
Escape
Tab
```

using the same physical-key semantics already established for built-in lists.

Selection must remain bounded.

No wraparound is required unless the existing Settings behavior already defines it.

### Row behavior

`Bluetooth`:

```text
Enter → existing Bluetooth/HostSettings route
```

`Wi-Fi`:

```text
Enter → Wi-Fi Settings
```

`Sound volume`:

```text
Left/Right → existing 10% steps
```

Moving the volume row from index `1` to index `2` must not change its audio or persistence behavior.

---

## 11. Wi-Fi Settings Navigation

Add a built-in route such as:

```text
wifi
```

and an Action:

```text
ui.wifi
```

`ui.wifi` is valid from the general Settings page.

Opening Wi-Fi Settings uses the existing forward page transition.

Back from the top-level Wi-Fi page should return to the general Settings page using a backward transition.

Do not reset directly to Home for this route.

Preserve existing Bluetooth-specific Back semantics unless an explicit architecture change is separately approved.

Plain Tab remains the built-in Settings shortcut.

If pressed from the Wi-Fi top-level page or one of its local configuration views, it must not accidentally submit partially entered credentials.

For modal/editor states, the local view consumes input first.

---

## 12. WiFiSettings Component

Introduce a dedicated built-in Apps-layer component, for example:

```text
src/apps/network/wifi_settings.h
src/apps/network/wifi_settings.cpp
```

Exact directory naming may follow existing Apps conventions.

`WiFiSettings` should depend on:

```text
NetworkService
ActionBus
IDisplayAdapter
```

It should not depend on Connectivity or hardware implementation headers.

Conceptually:

```text
ApplicationShell
      ↓ navigation
WiFiSettings
      ↓ Actions / snapshot
NetworkService
```

`WiFiSettings` owns only local presentation and edit state.

It may own:

```text
focused row
current local view
SSID draft
passphrase draft
confirmation state
render cache
```

It must not own persistent configuration.

---

## 13. Top-Level Wi-Fi Settings Screen

The screen follows the existing UI grammar:

```text
header
one-pixel separator
list rows
optional status/error area
```

The persistent Wi-Fi status list has no key-hint footer. Escape and Enter still
work. Editors and Forget use a compact transactional footer.

### Connected example

```text
WIFI                         CONNECTED
──────────────────────────────────────

01  Wi-Fi                         ON
02  Network                     HOME
03  Signal                   -54 dBm
04  Change network
05  Forget network
```

### Configured but disabled example

```text
WIFI                               OFF
──────────────────────────────────────

01  Wi-Fi                        OFF
02  Network                     HOME
03  Change network
04  Forget network
```

### Unconfigured example

```text
WIFI                    NOT CONFIGURED
──────────────────────────────────────

01  Configure network
```

These are conceptual layouts.

Final row positions must respect the existing 240×135 raster and shared UI requirements.

Do not shrink text below existing readable system-font usage merely to fit labels.

Use shorter wording where needed.

---

## 14. Wi-Fi Screen Status Labels

The header status is derived only from `WifiStatusSnapshot`.

Recommended labels:

```text
NOT CONFIGURED
OFF
CONNECTING
CONNECTED
ERROR
```

`Connecting` includes retry waiting because that distinction belongs to Connectivity.

Do not display:

```text
RETRY_WAITING
WifiState::Idle
adapter error enums
ESP-IDF errors
```

The Settings page may display a concise domain-level error message separately.

---

## 15. Enable / Disable Behavior

When a network is configured:

```text
Wi-Fi ON
```

or:

```text
Wi-Fi OFF
```

is the first row.

Enter toggles through:

```text
network.set-enabled
```

The UI must not directly call Connectivity.

### Enabling

If configuration exists:

```text
enabled = true
        ↓
persist candidate
        ↓
request connection
```

The UI immediately reflects the new Service snapshot.

Typical progression:

```text
OFF
→ CONNECTING
→ CONNECTED
```

or:

```text
OFF
→ CONNECTING
→ ERROR
```

### Unconfigured device

Do not offer an apparently functional ON toggle when no network is configured.

The primary operation should instead be:

```text
Configure network
```

Attempting to enable an unconfigured network must remain safely rejected by `NetworkService`.

---

## 16. Manual Network Configuration Flow

This plan deliberately uses manual entry.

Flow:

```text
Configure network
        ↓
SSID editor
        ↓
passphrase editor
        ↓
confirm / configure
        ↓
NetworkService
```

For an existing configuration, `Change network` enters the same flow.

The old persisted configuration must remain untouched while the user edits drafts.

Do not call `forget()` before configuration of the replacement network.

---

## 17. SSID Editor

Provide a dedicated local editing view.

Conceptual presentation:

```text
NETWORK NAME

HomeWifi_

ESC CANCEL                       ENTER NEXT
```

Requirements:

- accept printable keyboard input supported by the existing input abstraction;
- support Backspace;
- respect the existing maximum SSID length;
- never append embedded NUL;
- keep editing state local until confirmation;
- the unmarked Escape key (backtick) and Fn+backtick cancel without changing
  persisted configuration;
- Enter advances to passphrase entry;
- an empty SSID cannot be submitted as a valid network.

Do not implement separate Wi-Fi validation rules inside Apps.

Final validation remains owned by the existing lower-level/domain contracts.

---

## 18. Passphrase Editor

Conceptual presentation:

```text
PASSWORD

*******t_

ESC CANCEL                    ENTER CONNECT
```

Requirements:

- accept printable input;
- support Backspace;
- respect the existing maximum passphrase length;
- show only the newest typed character in clear text;
- immediately mask the previous character when another is typed;
- mask the newest character after about two seconds;
- use a simple mask character supported by the current bitmap font;
- never log the draft;
- never copy it into error messages;
- the unmarked Escape key (backtick) and Fn+backtick cancel the entire
  configuration flow;
- Enter submits the candidate.

An empty passphrase is allowed so open networks remain configurable.

The Service remains responsible for rejecting invalid protected-network passphrase forms.

The UI should display a concise validation failure and keep the editor usable for correction.

---

## 19. Credential Lifetime

Credential handling must remain bounded.

Draft SSID and passphrase live only in the Wi-Fi configuration UI while editing.

After successful configuration:

```text
clear draft SSID
clear draft passphrase
leave editor state
```

After cancellation:

```text
clear draft SSID
clear draft passphrase
leave editor state
```

After forgetting a network, no passphrase from the prior configuration may remain in UI state.

Do not add credentials to:

```text
logs
Actions beyond the immediate configure dispatch
render caches
Home state
diagnostics
test failure messages
```

Tests may use clearly synthetic credentials.

---

## 20. Shared Text-Entry Logic

Do not perform a broad HostSettings refactor merely to complete this plan.

However, avoid creating a large second ad-hoc editor implementation if a small Apps-layer reusable helper naturally removes duplicated input-buffer behavior.

A small helper may own only mechanical text-entry concerns such as:

```text
bounded append
Backspace
clear
current value
masked display generation
```

It must not own:

```text
navigation
Actions
Wi-Fi validation
host rename validation
pairing behavior
rendering policy
```

If introducing such a helper would substantially increase the blast radius or require rewriting HostSettings, keep Wi-Fi editing local in this plan and defer generalization.

Scope discipline is more important than speculative abstraction.

---

## 21. Configure / Change Network Semantics

Submit the completed draft through:

```text
network.configure
```

`NetworkService` remains responsible for persistence.

For a currently enabled network:

```text
save new configuration
        ↓
request connection to the new configuration
```

For a currently disabled network:

```text
save new configuration
        ↓
remain disabled
```

The UI must not automatically enable Wi-Fi merely because credentials were edited if persisted enabled intent was previously false.

For a completely unconfigured device, configuration should also preserve the Service's existing intent semantics rather than inventing a second policy in Apps.

If product testing later shows that initial setup should implicitly enable Wi-Fi, that should be an explicit follow-up behavior change rather than accidental UI logic.

---

## 22. Forget Network

Expose `Forget network` only when a network is configured.

Selecting it opens an explicit confirmation view:

```text
FORGET NETWORK?

HOME

ESC CANCEL                     ENTER FORGET
```

The unmarked Escape key (backtick) and Fn+backtick return without mutation.

The displayed network name may be shortened for the 240-pixel screen but must not modify the stored SSID.

Enter dispatches:

```text
network.forget
```

Escape returns without mutation.

On success:

```text
configured = false
enabled = false
connection = Off
```

Home therefore immediately renders:

```text
Wi-Fi glyph + hollow Ordinal dot
```

Do not silently forget a network from an error state.

---

## 23. Error Presentation

Home communicates only coarse network status.

Home error presentation:

```text
Wi-Fi glyph + filled Vermilion dot
```

No popup.

No automatic navigation.

No focus change.

Wi-Fi Settings may provide concise domain-level messages based on `NetworkResult`.

Suggested mapping:

```text
Success
    → no error text

InvalidInput
    → "Check network settings"

NotConfigured
    → "Configure a network first"

StorageError
    → "Settings could not be saved"

ConnectivityError
    → "Wi-Fi connection failed"
```

Wording may be shortened to fit the display.

Do not expose framework or adapter error values.

A background connection failure must not open a blocking modal.

---

## 24. RSSI Presentation

RSSI is informational only.

Show it only when:

```text
connection == Connected
signalStrengthDbm has a value
```

Example:

```text
Signal     -54 dBm
```

If connected but RSSI is temporarily unavailable:

```text
Signal     --
```

Do not derive qualitative labels such as:

```text
Excellent
Poor
Weak
Strong
```

in this plan.

Do not convert RSSI into Home color or Wi-Fi arc count.

---

## 25. Rendering and UI Scheduling

Preserve the behavior delivered by Plan 022.

`WiFiSettings` should cache enough render state to redraw only changed regions.

Relevant changes include:

```text
focus
status
enabled intent
configured state
SSID
RSSI
last domain result
editor contents
confirmation state
```

Repeated publication of identical Service state must not cause a full-screen repaint.

Background network polling must not request continuous animation frames.

The Home ambient wave remains the only currently approved idle animation.

Page transitions use the existing non-blocking slide infrastructure.

Wi-Fi connect/retry behavior continues independently while a transition is active.

---

## 26. Input Behavior

Top-level Wi-Fi lists use the existing system conventions:

```text
Up / ;
Down / .
Enter
unmarked Escape (backtick)
Fn+backtick
```

Text-entry views treat printable characters as text.

Characters such as:

```text
;
.
,
/
```

must remain printable when the editor is active and must not be interpreted as list navigation.

This follows the same modal/input-routing principle already used by host rename and pairing.

Backspace edits the current draft.

The unmarked Escape key (backtick) and Fn+backtick cancel the local editor
before any global Back behavior.

Enter acts only on the current view's explicit primary operation.

---

## 27. Audio Behavior

Do not introduce new semantic Wi-Fi sounds in this plan.

Existing generic key feedback continues to apply to accepted physical input.

Existing page navigation and value-step behavior must not regress.

Background Wi-Fi state changes must not emit key or navigation sounds.

Connection success or failure semantic sound cues remain part of the broader pending UI-sound work.

---

## 28. Security and Privacy

Wi-Fi credentials are sensitive configuration data.

Requirements:

- never log SSID/passphrase pairs;
- never log passphrases;
- never include passphrases in Action diagnostics;
- never keep a passphrase in clear text after the newest character's brief
  reveal, and never show more than that newest character;
- never expose passphrases through Home or status snapshots;
- preserve the existing dedicated persistent configuration storage behavior;
- do not weaken configuration validation;
- do not introduce compile-time credentials;
- do not introduce test credentials into production code;
- do not change NVS security/storage policy in this plan.

SSID may be displayed in Wi-Fi Settings because it is user-facing configuration metadata.

SSID should not be added to routine logs unless an explicit logging/privacy policy later allows it.

---

## 29. Firmware Size

Plan 026 established firmware-size observability.

This plan adds UI code but should not introduce another major networking dependency.

After the final production build, run:

```text
make firmware-size
```

Record the resulting application image size in the completion record.

A small increase from:

```text
WiFiSettings
new bitmap
new tests/runtime glue
```

is expected.

A large increase should be investigated before merge.

Do not optimize application architecture merely to recover a small expected UI-size increase.

Do not introduce a hard firmware-size budget in this plan.

---

## 30. Automated Tests

Add or update native tests for the following behavior.

### Home status

Verify:

```text
not configured
    → hollow quiet indicator

configured + disabled
    → filled Pale indicator

Connecting
    → Blue indicator

Connected
    → Leaf indicator

Error
    → Vermilion indicator
```

Verify that:

- Home no longer renders `OFFLINE`;
- Home does not render SSID;
- Home does not render RSSI;
- RSSI-only changes do not trigger Home Wi-Fi redraw;
- unchanged snapshots do not redraw the Wi-Fi region;
- battery updates remain independent;
- host-state updates remain independent;
- Home wave behavior is unchanged.

### Settings menu

Verify:

```text
01 Bluetooth
02 Wi-Fi
03 Sound volume
```

Verify:

- Up/Down navigate all three rows;
- Bluetooth still opens HostSettings;
- Wi-Fi opens WiFiSettings;
- Sound volume still changes only on the volume row;
- sound volume continues to persist;
- Settings rendering remains incremental.

### Network Actions

Verify:

- valid `network.set-enabled`;
- malformed `network.set-enabled`;
- valid `network.configure`;
- missing SSID/passphrase parameters;
- wrong parameter types;
- `network.forget`;
- unknown Action rejection.

Existing direct `NetworkService` tests remain valid.

### Wi-Fi configuration flow

Verify:

- manual SSID entry;
- Backspace;
- maximum length handling;
- cancellation;
- empty SSID rejection;
- empty passphrase submission for open network;
- newest-character passphrase reveal, then mask after about two seconds;
- transactional footers on editors and Forget;
- unmarked Escape and Fn+backtick cancel editors and Forget;
- protected-network passphrase input;
- invalid configuration result;
- successful configure;
- storage failure;
- old configuration retained when saving replacement fails;
- editing drafts does not mutate Service configuration;
- successful submission clears sensitive drafts.

### Forget flow

Verify:

- no Forget row while unconfigured;
- confirmation required;
- Escape preserves configuration;
- Enter forgets;
- successful forget updates Home state;
- failed persistence leaves configuration unchanged.

### Navigation

Verify:

- Settings → Wi-Fi uses forward transition;
- Wi-Fi → Settings uses backward transition;
- editor Escape returns to Wi-Fi screen rather than Home;
- confirmation Escape returns to Wi-Fi screen;
- transitions do not block Service updates;
- keyboard characters in editors are not stolen by list shortcuts.

---

## 31. Existing Regression Coverage

The full host test gate must continue to pass.

At minimum run the project's existing equivalents of:

```text
Python tests
native tests
architecture check
format check
static analysis
production firmware build
firmware-size report
```

Do not weaken existing assertions merely because constructor composition changes.

Update fixtures to provide `NetworkService` or a suitable real/fake Service boundary where `ApplicationShell` now requires it.

Prefer realistic Service-level state over introducing UI-only fake enums.

---

## 32. Production Composition Verification

Normal firmware must:

```text
load ConfigurationService
start HostService
start NetworkService
start AudioService
update NetworkService every loop
pass NetworkService to the Wi-Fi UI boundary
```

Splash behavior remains unchanged.

Input sampled on splash handoff remains consumed.

Wi-Fi runtime updates must continue before and after Home becomes visible.

The UI must not become responsible for calling `NetworkService::update()`.

---

## 33. Documentation Updates

Update:

```text
docs/ARCHITECTURE.md
docs/UI_REQUIREMENTS.md
docs/plans/README.md
README.md
docs/manuals/device-guide.md
```

as applicable.

### Architecture

Record that:

- Phase 3's NetworkService is now consumed by System UI;
- Home consumes the live network snapshot;
- manual Wi-Fi configuration is available;
- Wi-Fi scanning remains pending;
- Phase 4 live Wi-Fi status is delivered;
- the clock remains pending.

### UI requirements

Replace the old `OFFLINE` placeholder description.

Document:

```text
neutral Ink Wi-Fi glyph
separate semantic status indicator
no SSID on Home
no RSSI on Home
```

and the color/shape mapping.

### Plan index

Add:

```text
027 | Wi-Fi System UI and Home Network Status | Implemented / Complete
```

only once implementation is actually complete.

### Device guide

Document:

- how to open Settings;
- how to open Wi-Fi;
- how to configure SSID/password;
- how to enable/disable Wi-Fi;
- how to change network;
- how to forget network;
- meaning of Home Wi-Fi indicator states.

Never include real credentials in documentation.

---

## 34. Physical Acceptance

After automated checks and production firmware build pass, stage the final image on the physical Cardputer-Adv.

Use the normal firmware image, not a special validation-only UI build.

Perform at least these scenarios.

### A. No configured network

Start with no stored network.

Expected:

```text
Home
→ new Wi-Fi glyph
→ hollow quiet dot
→ no OFFLINE text
```

Open:

```text
Settings → Wi-Fi
```

Expected:

```text
NOT CONFIGURED
Configure network available
```

### B. Configure a valid network

Enter:

```text
SSID
passphrase
```

Confirm.

Expected progression:

```text
Home / Wi-Fi Settings
CONNECTING / Blue
        ↓
CONNECTED / Leaf
```

Verify normal Internet-capable station connection using existing diagnostics or runtime evidence.

### C. Reboot restoration

Reboot while Wi-Fi is enabled and configured.

Expected:

```text
configuration loads
NetworkService restores enabled intent
Blue while connecting
Leaf when connected
```

No manual re-entry is required.

### D. Disable

Disable Wi-Fi from Settings.

Expected:

```text
persisted enabled = false
radio connection stops
Home → filled quiet/Pale dot
SSID remains configured
```

Reboot.

Expected:

```text
Wi-Fi remains disabled
configured network remains present
```

### E. Re-enable

Enable Wi-Fi.

Expected:

```text
Blue
→ Leaf
```

without re-entering credentials.

### F. Connection failure

Use unavailable/incorrect synthetic test configuration as appropriate.

Expected:

```text
Blue during attempts/retry
eventual Vermilion error state
Wi-Fi screen shows concise failure
Home does not navigate or open popup
Bluetooth and local UI remain responsive
```

### G. Change network

Configure a replacement network.

Verify:

- old configuration is retained until new candidate persistence succeeds;
- new configuration becomes authoritative after successful save;
- enabled intent follows existing Service semantics;
- resulting connection uses the new configuration.

### H. Forget

Choose:

```text
Forget network
```

Cancel once.

Verify no change.

Repeat and confirm.

Expected:

```text
configuration cleared
Wi-Fi disabled
Home → hollow quiet dot
Wi-Fi screen → NOT CONFIGURED
```

### I. Bluetooth regression

While Wi-Fi is configured and active:

- verify existing BLE host state remains responsive;
- verify Bluetooth Settings still open;
- verify selected host state is unchanged;
- verify Wi-Fi UI navigation does not modify host selection;
- verify network activity does not interfere with host status rendering.

### J. UI quality

On the physical LCD verify:

- new Wi-Fi glyph is visually cleaner than the removed glyph;
- glyph and status dot do not visually merge;
- hollow versus filled dot is distinguishable;
- status remains readable at low brightness;
- battery percentage does not overlap;
- clock placeholder does not overlap;
- transitions remain smooth;
- text editors remain usable with the physical keyboard;
- the newest password character is briefly visible, then masked;
- unmarked Escape and Fn+backtick cancel editors and Forget;
- pairing comparison shows ENTER YES; passkey shows ENTER APPLY only when complete.

---

## 35. Recommended Implementation Order

Implement in small testable slices.

### Step 1 — Network Action boundary

- add ActionBus support to `NetworkService`;
- test Actions;
- register them in normal composition.

Do not change UI yet.

### Step 2 — Home live network state

- inject `NetworkService` into `ApplicationShell`;
- remove hardcoded `OFFLINE`;
- implement the new neutral Wi-Fi glyph;
- implement semantic dot mapping;
- cache Home network presentation;
- test incremental redraw.

At this point normal firmware should already show real Wi-Fi status.

### Step 3 — Three-row Settings

- add Wi-Fi row;
- generalize selection;
- preserve volume behavior;
- add `ui.wifi`;
- add Wi-Fi navigation route.

### Step 4 — WiFiSettings read-only status

Implement the top-level Wi-Fi screen first using real `NetworkService::status()`:

```text
status
enabled
SSID
RSSI
```

Do not add mutations until read-only rendering is covered.

### Step 5 — Enable/disable

- wire ON/OFF through ActionBus;
- test persistence and errors;
- physically verify if useful.

### Step 6 — Manual configure flow

- SSID draft;
- passphrase draft;
- newest-character reveal then mask;
- transactional footer hints;
- submit;
- errors;
- clear sensitive drafts.

### Step 7 — Change and forget

- reuse configure flow for replacement;
- add explicit forget confirmation;
- verify failed persistence behavior.

### Step 8 — Documentation and full validation

- architecture/docs;
- full host gate;
- production build;
- firmware-size report;
- final physical acceptance.

Avoid implementing every screen and mutation before the first tests pass.

---

## 36. Completion Criteria

Plan 027 is complete only when all of the following are true:

```text
[x] Home consumes NetworkService status.
[x] Home no longer displays OFFLINE/ONLINE Wi-Fi text.
[x] Old Wi-Fi glyph is removed.
[x] New neutral Wi-Fi glyph is implemented.
[x] Separate semantic status dot is implemented.
[x] Not-configured and disabled states are distinguishable without color alone.
[x] Wi-Fi status rendering is incremental.
[x] Settings contains Bluetooth, Wi-Fi and Sound volume.
[x] Wi-Fi Settings consumes only Service-layer network state.
[x] Wi-Fi can be enabled and disabled from the device.
[x] SSID can be configured manually.
[x] Passphrase can be entered without clear-text rendering.
[x] Existing network can be changed.
[x] Network can be forgotten only after confirmation.
[x] RSSI is shown only in Wi-Fi Settings while appropriate.
[x] Network mutations use logical Actions.
[x] Connectivity contracts remain unchanged.
[x] No Wi-Fi scanning is added.
[x] No credentials are logged.
[x] Existing Bluetooth/Host behavior is preserved.
[x] Automated project gates pass.
[x] Production firmware builds.
[x] Firmware-size report is recorded.
[ ] Physical Cardputer-Adv acceptance passes.
[x] Architecture, UI requirements, plan index and user documentation are aligned.
```

---

## 37. Follow-Up

Do not add Wi-Fi discovery simply because manual configuration now exists.

After physical use, evaluate whether typing SSIDs is sufficiently usable.

If discovery materially improves the device experience, create a separate plan, conceptually:

```text
028 — Wi-Fi Network Discovery
```

That future work may extend:

```text
IWifiAdapter
        ↓
WiFiService
        ↓
NetworkService
        ↓
WiFiSettings
```

with bounded scan lifecycle and a discovered-network list.

A likely future UI could become:

```text
SELECT NETWORK

01 HomeWifi
02 Guest
03 Phone
04 Other...
```

where:

```text
Other...
```

retains manual SSID entry for hidden networks.

That work must remain separate because it changes Connectivity and hardware contracts, whereas Plan 027 deliberately consumes the Wi-Fi foundation that already exists.
