# 037/2 — macOS Companion Menu Bar UI

Status: **Implemented; appearance validation pending**

The main popover was later compacted into a direct-on-material status layout
with a single protocol/last-seen line and quiet menu actions. The layout
sketches below document the original implementation target; the current
presentation is owned by `CompanionPopoverView`.

The menu-bar UI is implemented and its connected v2 main view was visually
checked on macOS. `make companion-check` passed on macOS. Physical Reconnect,
Diagnostics, and the Start at Login toggle were confirmed on a Cardputer by
the user. Dark Mode, Reduce Transparency, and Reduce Motion remain to be
visually checked.

Parent plan:

```text
037 — Mac Status Mini App and macOS System Telemetry
```

Suggested branch:

```text
feat/037-mac-status-telemetry
```

This addendum belongs to the same PR and does not create a separate release scope.

---

# 0. Goal

Turn the currently headless Cardputer Companion into a lightweight native macOS menu-bar utility.

The Companion remains:

```text
LSUIElement = true
```

and therefore has:

```text
no Dock icon
no main application window
no normal app-switcher presence
```

The primary UI becomes a menu-bar status item.

Clicking it opens a small modern system popover containing:

```text
Cardputer connection status
selected Companion protocol
session status / last seen
Reconnect action
Start at Login toggle
Diagnostics
About
Quit
```

The UI should feel native, light and modern.

Do not build a large custom desktop application.

---

# 1. Design Direction

Use a system-first visual approach inspired by current macOS Liquid Glass.

Do not create a fake glass window from manually tuned:

```text
blur radius
transparent white fills
hard-coded gradients
custom desktop sampling
multiple translucent overlays
```

Instead use native macOS presentation primitives.

Preferred architecture:

```text
NSStatusItem
    ↓
NSPopover
    ↓
NSHostingController
    ↓
SwiftUI CompanionPopoverView
```

The popover provides the native system material.

On newer macOS releases, the system may apply the current Liquid Glass visual language automatically.

On older supported releases, the same component remains a normal native translucent macOS popover.

The deployment target remains:

```text
macOS 13+
```

Do not make macOS 26 a requirement.

---

# 2. Why NSPopover

Do not attach a traditional `NSMenu` directly to the status item.

A plain menu is suitable for simple command lists but does not give enough freedom for:

```text
connection status card
secondary session details
modern grouped controls
custom hierarchy
status indicators
diagnostics presentation
```

Use:

```text
NSStatusItem
+
NSPopover
```

The status-item button remains the menu-bar anchor.

The popover owns presentation only.

---

# 3. Menu Bar Status Item

Create one square status item.

Conceptually:

```text
[ Cardputer icon ]
```

Use a monochrome template image so macOS controls its appearance automatically in:

```text
Light mode
Dark mode
active menu-bar state
different menu-bar backgrounds
accessibility modes
```

Do not render a colored RGB icon in the menu bar.

Suggested icon concept:

```text
small Cardputer silhouette
```

or:

```text
small terminal/device glyph
```

Prefer a Cardputer-specific silhouette if it remains legible at menu-bar size.

---

# 4. Status Item State

The icon may reflect broad Companion state using only subtle variations.

States:

```text
Disconnected
Connecting
Connected
Error
```

Recommended representation:

```text
Disconnected
→ normal glyph with reduced emphasis

Connecting
→ normal glyph

Connected
→ normal full-emphasis glyph

Error
→ glyph with small exclamation badge if clearly readable
```

Do not:

```text
continuously animate the menu-bar icon
flash
pulse
change color every second
show telemetry numbers in the menu bar
```

The icon should remain visually quiet.

---

# 5. Tooltip

Status item tooltip:

Disconnected:

```text
Cardputer Companion — Disconnected
```

Connecting:

```text
Cardputer Companion — Connecting
```

Connected:

```text
Cardputer Companion — Connected
```

Protocol error:

```text
Cardputer Companion — Connection error
```

The tooltip reflects the same presentation state as the popover.

---

# 6. Popover Size

Recommended approximate content size:

```text
width: 300–320 px
height: 300–360 px
```

Avoid:

```text
large desktop-style window dimensions
scrolling for the normal state
horizontal scrolling
```

The normal popover should fit completely on screen even on small MacBook displays.

Exact dimensions may be adjusted after visual testing.

---

# 7. Main Popover Layout

Conceptual UI:

```text
╭──────────────────────────────────╮
│ CARDPUTER COMPANION              │
│                                  │
│ ╭──────────────────────────────╮ │
│ │ ● Cardputer                 │ │
│ │   Connected                 │ │
│ │                              │ │
│ │ Protocol            v2       │ │
│ │ Last seen          Just now  │ │
│ ╰──────────────────────────────╯ │
│                                  │
│ Reconnect Cardputer             │
│                                  │
│ Start at Login              [●] │
│                                  │
│ Diagnostics                  ›  │
│ About                           │
│                                  │
│ Quit Cardputer Companion        │
╰──────────────────────────────────╯
```

The outer rounded rectangle is conceptual only.

Do not manually draw an additional opaque container over the native popover background unless required for readability.

---

# 8. Header

Top area:

```text
CARDPUTER COMPANION
```

Use a compact secondary/title treatment.

Do not add:

```text
logo artwork
huge app title
version number in the header
marketing subtitle
```

The device/status card should be visually more important than the product title.

---

# 9. Device Status Card

The primary visual element is a compact device card.

Connected example:

```text
● Cardputer

Connected

Protocol       v2
Last seen      Just now
```

Disconnected example:

```text
○ Cardputer

Disconnected
```

Connecting example:

```text
◐ Cardputer

Connecting…
```

Error example:

```text
! Cardputer

Connection error
```

The Cardputer label may later be replaced with an actual user-visible device name if a reliable name is available from the existing BLE/session state.

Do not invent or persist a second device name in this plan.

---

# 10. Status Dot

Use a very small semantic status mark inside the popover.

Suggested states:

```text
Leaf / green
→ Connected

neutral / secondary
→ Disconnected

Blue / accent
→ Connecting

Vermilion / red
→ Error
```

On macOS, use system semantic colors rather than hard-coded Cardputer firmware RGB values.

The indicator should remain secondary to the text.

---

# 11. Connection Presentation Model

Introduce a UI-facing model independent from raw CoreBluetooth state.

Conceptually:

```swift
enum CompanionConnectionPresentationState {
    case disconnected
    case connecting
    case connected
    case error
}
```

Do not expose raw:

```text
CBPeripheralState
CBCentralManagerState
CompanionAttachAction
```

directly to SwiftUI.

The existing Companion runtime remains the source of truth.

The UI adapter maps runtime state into presentation state.

---

# 12. Connected Detail

When connected, show:

```text
Protocol     v1 / v2
Last seen    Just now / Ns ago
```

`Protocol` comes from the negotiated Companion session version.

`Last seen` is based on the latest valid Companion protocol activity.

Do not define `Last seen` as:

```text
time since BLE peripheral object existed
```

It must mean meaningful valid Companion traffic.

---

# 13. Last Seen Formatting

Formatting:

```text
< 2 seconds
→ Just now

2–59 seconds
→ 8s ago

1–59 minutes
→ 4m ago

>= 60 minutes
→ 1h ago
```

The value does not need second-perfect updates while the popover is closed.

While the popover is visible, update the display at a low UI rate such as:

```text
1 Hz
```

Do not generate protocol traffic solely to update the label.

---

# 14. Session Duration

Session duration is useful but less important than `Last seen`.

Do not put it in the main card initially.

Expose it inside Diagnostics:

```text
Session       18m 42s
```

This keeps the normal view small.

---

# 15. Protocol Version

Display only while a Companion session has successfully negotiated.

Examples:

```text
Protocol     v2
```

or:

```text
Protocol     v1
```

When disconnected:

```text
Protocol
```

row may be hidden rather than showing:

```text
--
```

Prefer hiding non-applicable secondary rows.

---

# 16. Reconnect Cardputer

Add a primary utility action:

```text
Reconnect Cardputer
```

Behavior:

```text
user presses Reconnect
    ↓
current Companion attach is reset
    ↓
existing retry/lookup mechanism starts immediately
    ↓
connected HID Cardputer is searched
    ↓
Companion service is rediscovered
    ↓
handshake runs again
```

This is a recovery operation.

It must not:

```text
forget BLE bond
remove HostProfile
reset Cardputer configuration
disconnect HID permanently
change the selected Cardputer host
```

Reuse existing `CompanionAttachCoordinator` lifecycle rather than adding a second connection implementation.

---

# 17. Reconnect UI State

While reconnect is in progress:

```text
Reconnect Cardputer
```

becomes temporarily unavailable.

Device state becomes:

```text
Connecting…
```

Do not display:

```text
spinner covering the entire popover
modal dialog
progress window
```

A small inline progress indicator beside `Connecting…` is acceptable.

If the attempt fails, normal retry behavior continues.

The user may close the popover at any time.

---

# 18. Start at Login

Expose the existing login-item functionality as:

```text
Start at Login       toggle
```

The state must reflect actual `SMAppService` registration status.

Do not maintain a duplicate preference such as:

```text
UserDefaults.startAtLogin = true
```

that can disagree with the real service state.

Toggle ON:

```text
SMAppService registration
```

Toggle OFF:

```text
SMAppService unregister
```

Errors remain local to the row.

---

# 19. Start-at-Login Failure

If registration fails:

```text
Start at Login    !
```

Optionally expose a short secondary label:

```text
Could not update login setting
```

Do not:

```text
open an alert window automatically
navigate to System Settings automatically
terminate Companion
```

A retry occurs when the user toggles again.

---

# 20. Diagnostics Entry

Add:

```text
Diagnostics   ›
```

Selecting it replaces the main popover content with a diagnostics screen.

Do not open a second window.

Back navigation returns to the main view.

---

# 21. Diagnostics UI

Conceptual view:

```text
╭──────────────────────────────────╮
│ ‹  DIAGNOSTICS                   │
│                                  │
│ Connection       Connected       │
│ Protocol         v2              │
│ Session          18m 42s         │
│ Last message     < 1s            │
│                                  │
│ CAPABILITIES                     │
│                                  │
│ App Control      Available       │
│ App Events       Available       │
│ System Metrics   Available       │
│                                  │
╰──────────────────────────────────╯
```

This is intentionally read-only.

---

# 22. Diagnostics Capability Mapping

Use the negotiated live capability state.

Display human-readable names:

```text
APP_ACTIVE / APP_ACTIVATE
→ App Control

APP_ACTIVE_EVENTS
→ App Events

SYSTEM_METRICS
→ System Metrics
```

Do not dump raw capability integer IDs in the default diagnostics view.

Unknown future capabilities may be omitted or shown generically.

---

# 23. Additional Diagnostics

Useful read-only values if already available cheaply:

```text
Bluetooth       Ready
Session ID      42
Protocol        v2
Last message    <1s
```

Do not display:

```text
BLE peer identity
bond identity
raw packet payloads
bundle identifiers
private identifiers
Bluetooth MAC address
```

Existing privacy/logging requirements remain unchanged.

---

# 24. Diagnostics Scope

Diagnostics must not become a developer console.

Do not add:

```text
live logs
packet dump
raw GATT messages
protocol hex
force capability
send arbitrary command
BLE scanner
bond deletion
HostProfile manipulation
```

Those are outside Plan 037/2.

---

# 25. About

Add:

```text
About
```

Selecting it may either:

```text
replace popover content
```

or use a compact standard About panel.

Preferred for consistency:

```text
replace popover content
```

Conceptually:

```text
‹  ABOUT

Cardputer Companion

Version 0.x.x
Build ...

Part of Cardputer Hub
```

No marketing content is required.

---

# 26. Quit

Add:

```text
Quit Cardputer Companion
```

at the bottom.

It should be visually separated from the normal controls.

Behavior:

```text
close Companion session
stop retry timers
stop observers
close popover
remove status item
terminate application
```

Do not show a confirmation dialog.

Quit does not modify:

```text
Start at Login
Cardputer configuration
BLE bonds
HostProfile configuration
```

If Start at Login remains enabled, Companion may start again on the next user login as expected.

---

# 27. Popover Interaction

Click status icon:

```text
closed
→ open popover

open
→ close popover
```

Click outside:

```text
→ close popover
```

Use transient or equivalent native popover behavior.

Do not leave an invisible key window after closing.

---

# 28. Keyboard Behavior

Normal macOS keyboard expectations should work.

At minimum:

```text
Escape
→ close popover / return from nested view as appropriate

Tab
→ normal focus traversal

Space / Enter
→ activate focused control
```

Do not invent Cardputer-style keyboard shortcuts for the macOS popover.

---

# 29. Modern Visual Style

The design should be:

```text
light
spacious
soft
native
minimal
quiet
```

Use:

```text
native font
system spacing
rounded grouped surfaces
system controls
semantic foreground colors
system accent
native popover material
```

Avoid:

```text
heavy borders
large shadows
gradients
neon
glow
custom title bars
dense settings-table appearance
```

The UI should feel like a modern macOS utility rather than a settings dialog.

---

# 30. Liquid Glass Adaptation

The app must not require Liquid Glass APIs to function.

Base implementation:

```text
NSPopover
native background/material
SwiftUI content
```

On systems where current macOS Liquid Glass styling is provided by native popovers and controls:

```text
use native appearance
```

Do not cover the system popover background with an opaque custom root view.

Do not add an unnecessary `NSVisualEffectView` over a popover merely to simulate glass.

If macOS-26-only custom glass APIs are later used for selected controls:

```text
guard with availability
```

and keep the macOS 13 fallback visually complete.

For Plan 037/2, native popover styling is sufficient.

---

# 31. Optional Glass Treatment for Primary Actions

If physical/UI testing shows that the native popover needs one stronger visual focal point, a glass-like elevated treatment may be applied only to:

```text
device status card
```

or:

```text
Reconnect button
```

but not both unless the result remains restrained.

Do not put every row inside an independent glass capsule.

The default implementation should first be tested without custom glass effects.

---

# 32. Reduced Transparency

The UI must remain readable when macOS accessibility settings reduce transparency.

Do not rely on:

```text
background blur alone
subtle transparent white alone
background color sampling
```

for separation.

Text hierarchy and spacing must remain sufficient.

---

# 33. Reduced Motion

No essential information may depend on animation.

If a small connecting spinner or state transition is used:

```text
respect Reduce Motion
```

Do not use fluid morphing as required feedback.

---

# 34. Dark / Light Appearance

Support both automatically.

Do not hard-code:

```text
white background
black background
fixed grey card fill
```

Use system semantic colors.

Verify:

```text
Light
Dark
increased contrast
reduced transparency
```

---

# 35. UI State Model

Introduce a view model or presentation model separate from `CompanionCentral`.

Conceptually:

```swift
struct CompanionUIState {
    var connection: CompanionConnectionPresentationState
    var protocolVersion: UInt8?
    var lastMessageAt: Date?
    var sessionStartedAt: Date?
    var capabilities: Set<CompanionCapability>
    var startAtLogin: Bool
    var startAtLoginError: Bool
}
```

Exact type may differ.

The SwiftUI layer observes this state.

It does not query CoreBluetooth directly.

---

# 36. Runtime → UI Boundary

`CompanionCentral` currently owns the actual runtime connection lifecycle.

Do not move connection logic into SwiftUI.

Add a small observable bridge.

Conceptually:

```text
CompanionCentral
      ↓
CompanionStatusStore
      ↓
SwiftUI
```

Actions flow back:

```text
SwiftUI
   ↓
CompanionUIActions
   ↓
CompanionCentral
```

For example:

```text
reconnect()
setStartAtLogin(enabled)
quit()
```

---

# 37. Ownership Matrix

| Component | Owns | Must not own |
| --- | --- | --- |
| `CompanionCentral` | CoreBluetooth connection lifecycle | UI layout |
| `CompanionSession` | protocol session | status-item presentation |
| `CompanionStatusStore` | UI-facing observable snapshot | BLE decisions |
| `CompanionPopoverView` | layout and user interaction | CoreBluetooth |
| `CompanionMenuBarController` | status item + popover lifecycle | protocol semantics |
| `SMAppService` adapter | login registration | general Companion state |

---

# 38. Architecture Invariants

The following must remain true:

```text
SwiftUI does not call CoreBluetooth directly.

SwiftUI does not parse Companion protocol messages.

CompanionCentral remains the owner of attach/reconnect lifecycle.

The status UI reflects runtime state; it does not create parallel state.

Start at Login reflects actual SMAppService state.

Closing the popover does not disconnect Cardputer.

Opening the popover does not trigger new protocol traffic.

Last Seen is derived from existing valid traffic.

Reconnect reuses existing connection infrastructure.

Quit is the only UI command that terminates Companion.

No main app window is introduced.

LSUIElement remains enabled.

macOS 13 remains supported.
```

---

# 39. BDD — Menu Bar Presence

```gherkin
Scenario: Companion starts
  Given Cardputer Companion launches successfully
  Then a Cardputer Companion status item appears in the macOS menu bar
  And no Dock icon appears
  And no main application window opens
```

---

# 40. BDD — Open Popover

```gherkin
Scenario: User opens Companion
  Given the menu-bar item is visible
  When the user clicks the status item
  Then the Companion popover opens
  And the current Cardputer connection status is visible
```

---

# 41. BDD — Close Popover

```gherkin
Scenario: User clicks outside the Companion popover
  Given the popover is open
  When the user clicks another application
  Then the popover closes
  And the Companion connection remains unchanged
```

---

# 42. BDD — Connected Status

```gherkin
Scenario: Compatible Cardputer is connected
  Given Companion has completed its handshake
  And protocol v2 is selected
  When the popover opens
  Then the Cardputer state is Connected
  And Protocol shows v2
  And Last seen reflects recent valid protocol traffic
```

---

# 43. BDD — Disconnected Status

```gherkin
Scenario: No compatible Cardputer session exists
  Given Companion has no active Companion session
  When the popover opens
  Then the Cardputer state is Disconnected
  And no protocol version is shown
```

---

# 44. BDD — Connecting Status

```gherkin
Scenario: Companion is attaching to Cardputer
  Given the attach coordinator is currently connecting or handshaking
  When the popover is visible
  Then the Cardputer state is Connecting
  And the UI remains interactive
```

---

# 45. BDD — Reconnect

```gherkin
Scenario: User requests reconnect
  Given the popover is open
  When the user presses Reconnect Cardputer
  Then the existing Companion attach session is reset
  And connection lookup begins immediately
  And the presentation changes to Connecting
  And no BLE bond is deleted
```

---

# 46. BDD — Start at Login

```gherkin
Scenario: User enables Start at Login
  Given login registration is currently disabled
  When the user enables Start at Login
  Then Companion registers using SMAppService
  And the toggle reflects the resulting actual registration state
```

```gherkin
Scenario: Login registration fails
  Given registration fails
  When the operation completes
  Then the Companion continues running
  And the Start at Login row indicates the error
```

---

# 47. BDD — Diagnostics

```gherkin
Scenario: User opens Diagnostics
  Given the popover is open
  When the user selects Diagnostics
  Then the same popover shows the Diagnostics view
  And current protocol and capability state are shown
  And no second application window opens
```

---

# 48. BDD — Quit

```gherkin
Scenario: User quits Companion
  Given the Companion is running
  When the user selects Quit Cardputer Companion
  Then active Companion resources are released
  And timers and observers are stopped
  And the application terminates
  And BLE bonds are unchanged
  And Start at Login registration is unchanged
```

---

# 49. Testing

Add host/macOS tests for presentation-state mapping:

```text
disconnected
connecting
connected
error

protocol v1
protocol v2

last-seen formatting

capability mapping

start-at-login state

start-at-login error state
```

Where UI automation is impractical, extract pure presentation helpers and test them independently.

---

# 50. Manual UI Validation

Verify on macOS:

```text
Light Mode
Dark Mode

Reduce Transparency ON/OFF
Reduce Motion ON/OFF

Connected
Disconnected
Connecting
Error

v1
v2
```

Confirm:

```text
status icon remains readable

popover feels native

no opaque rectangle covers native popover material

text remains readable with reduced transparency

popover closes normally

Companion continues working while popover is closed
```

---

# 51. Non-goals

Plan 037/2 does not add:

```text
main desktop window
Dock icon
Preferences window
Cardputer battery telemetry
Cardputer Wi-Fi telemetry
Cardputer firmware telemetry
Cardputer uptime telemetry
BLE scanner
bond manager
host selector
logs viewer
packet inspector
notifications on every disconnect
Mac CPU/RAM display inside Companion
automatic popover opening
menu-bar CPU/RAM numbers
custom themes
user-selectable glass intensity
```

Cardputer-side device telemetry may be introduced by a future bidirectional `DEVICE_STATUS` protocol feature.

---

# 52. Recommended Implementation Order

```text
01. Add CompanionStatusStore

02. Map Companion runtime lifecycle to UI state

03. Expose negotiated protocol version

04. Expose last valid message timestamp

05. Expose session start timestamp

06. Expose live capability snapshot

07. Add CompanionMenuBarController

08. Create NSStatusItem

09. Add template menu-bar icon

10. Create NSPopover

11. Host SwiftUI CompanionPopoverView

12. Implement main status card

13. Implement Connected / Connecting / Disconnected / Error

14. Implement Protocol row

15. Implement Last seen row

16. Wire Reconnect Cardputer

17. Wire real SMAppService status

18. Add Start at Login toggle

19. Add Diagnostics navigation

20. Add Diagnostics state rows

21. Add capability rows

22. Add About view

23. Add Quit action

24. Add tooltip state

25. Verify Light / Dark

26. Verify Reduce Transparency

27. Verify Reduce Motion

28. Run Companion tests

29. Run companion-check

30. Perform physical Cardputer reconnect testing
```

---

# 53. Completion Criteria

Plan 037/2 is complete when:

```text
[x] Companion has a macOS menu-bar status item

[x] Companion remains LSUIElement

[x] no Dock icon appears

[x] no main application window appears

[x] clicking the status item opens a native popover

[x] popover uses a light modern system-native visual style

[x] macOS 13 remains supported

[x] newer macOS can adopt native Liquid Glass appearance

[x] no fake custom glass implementation is required

[x] Cardputer Connected state is visible

[x] Cardputer Connecting state is visible

[x] Cardputer Disconnected state is visible

[x] Cardputer Error state is visible

[x] negotiated protocol version is visible while connected

[x] Last seen is visible while connected

[x] Reconnect Cardputer works

[x] reconnect does not delete bonds

[x] Start at Login reflects SMAppService

[x] Start at Login can be enabled/disabled

[x] Diagnostics is available inside the same popover

[x] Diagnostics shows session state

[x] Diagnostics shows protocol version

[x] Diagnostics shows live capabilities

[x] About is available

[x] Quit Cardputer Companion is available

[x] Quit releases runtime resources

[x] Quit does not modify login registration

[x] Light Mode looks correct

[ ] Dark Mode looks correct

[ ] Reduce Transparency remains readable

[ ] Reduce Motion remains usable

[x] Companion operation does not depend on the popover being open

[x] companion-check passes

[x] physical reconnect validation passes
```

---

# 54. End State

Normal use:

```text
macOS menu bar

        [▣]
         │
         ▼

╭──────────────────────────────────╮
│ CARDPUTER COMPANION              │
│                                  │
│ ╭──────────────────────────────╮ │
│ │ ● Cardputer                 │ │
│ │   Connected                 │ │
│ │                              │ │
│ │ Protocol               v2    │ │
│ │ Last seen        Just now    │ │
│ ╰──────────────────────────────╯ │
│                                  │
│ Reconnect Cardputer             │
│                                  │
│ Start at Login              ON  │
│                                  │
│ Diagnostics                  ›  │
│ About                           │
│                                  │
│ Quit Cardputer Companion        │
╰──────────────────────────────────╯
```

The Companion remains a background utility.

The menu-bar UI provides visibility and recovery controls without becoming the center of the product.

The visual treatment follows the operating system rather than maintaining a separate custom glass design.
