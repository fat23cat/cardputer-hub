# 031 — MAC CONTROL App Grid & Telegram Activation

Status: **Software implemented; physical Telegram acceptance pending**

Base implementation:

```text
feat/030-macos-companion-foundation
```

Suggested branch:

```text
feat/031-mac-control-app-grid
```

Suggested PR:

```text
[031] Add MAC CONTROL app grid and Telegram activation
```

---

## 0. Starting Point

Plan 030 is treated as fully software- and physically verified.

Verified:

```text
firmware build ✓
host tests ✓
companion checks ✓
CI ✓

BLE/HID ✓
Companion handshake ✓
Home ◆ lifecycle ✓
Mac sleep/wake ✓
automatic reconnect ✓
physical Cardputer ↔ macOS Companion communication ✓
```

The only Plan 030 end-to-end path intentionally left unverified is:

```text
Cardputer
    ↓
APP_ACTIVATE
    ↓
real macOS application
```

Plan 031 is the first time this path becomes available to the user.

---

# 1. Goal

Add a production Mini App:

```text
MAC CONTROL
```

with a direct-key interface:

```text
┌──────────┬──────────┬──────────┐
│ 1        │ 2        │ 3        │
│          │          │          │
│ TELEGRAM │          │          │
├──────────┼──────────┼──────────┤
│ 4        │ 5        │ 6        │
│          │          │          │
│          │          │          │
└──────────┴──────────┴──────────┘
```

Every on-screen tile always maps to a physical digit:

```text
1 2 3
4 5 6
```

The number is the stable launch method.

The icon and label only explain what is currently assigned to that number.

Plan 031 has one production binding:

```text
1 → Telegram
```

Pressing `1`:

```text
physical key 1
      ↓
MAC CONTROL binding
      ↓
logical Action
      ↓
HostControlService
      ↓
CompanionService
      ↓
APP_ACTIVATE
      ↓
Cardputer Companion.app
      ↓
Telegram
```

If Telegram is already running:

```text
focus Telegram
```

If Telegram is closed:

```text
launch Telegram
→ focus Telegram
```

---

# 2. Core UX Principle

MAC CONTROL is not a menu with a selection cursor.

It is a physical control surface.

The user sees:

```text
1 → application A
2 → application B
3 → application C
4 → application D
5 → application E
6 → application F
```

and presses the matching physical digit.

There is no:

```text
cursor
selection
Enter
scroll list
letter hotkeys
```

This avoids collisions among apps that share the same initial letter:

```text
Telegram
Terminal
Teams
```

do not conflict.

---

# 3. Multiple Pages

MAC CONTROL is designed from the start as a paged `3 × 2` grid.

Each page reuses:

```text
1 2 3
4 5 6
```

Example:

```text
PAGE 1

1 Telegram
2 Safari
3 Zed
4 Terminal
5 Finder
6 Spotify
```

Next page:

```text
PAGE 2

1 Notes
2 Calendar
3 Music
4 Mail
5 Photos
6 ...
```

The digits belong to **the current page**. They are not a global application ID.

---

# 4. Page Navigation

Use the existing logical keys:

```text
Left
Right
```

to switch pages.

On Cardputer, keep the existing physical aliases wherever the shared input layer already maps them to Left/Right.

Behavior:

```text
Right
→ next page

Left
→ previous page
```

Page transition:

```text
~220 ms horizontal cubic slide
```

in line with the existing UI motion grammar.

On the first page:

```text
Left → no-op
```

On the last page:

```text
Right → no-op
```

No wrap-around.

---

# 5. No Application Chrome

MAC CONTROL has **no header**.

Do not show:

```text
MAC CONTROL
COMPANION
CONNECTED
READY
◆
host name
page title
footer
ESC BACK
```

The resting view is only the grid.

Why:

MAC CONTROL is available only while the `COMPANION` capability is live.

Repeating connection state inside the app is therefore pointless.

---

# 6. Grid Geometry

Target:

```text
240 × 135
```

The grid occupies practically the entire screen:

```text
3 columns
×
2 rows
```

Use:

```text
full-bleed Bone background
1 px Ink separators
square geometry
```

Do not use:

```text
rounded rectangles
cards
shadows
gradients
```

Conceptually:

```text
0        80       160      240
┌────────┬────────┬────────┐
│        │        │        │
│        │        │        │
│        │        │        │
├────────┼────────┼────────┤
│        │        │        │
│        │        │        │
│        │        │        │
└────────┴────────┴────────┘
0                ~67      135
```

Exact pixel geometry is decided in implementation so separators stay exactly 1 px and the full raster is used.

---

# 7. Tile Anatomy

Every bound tile contains two elements:

```text
number
label
```

For example:

```text
┌──────────────┐
│ 1            │
│              │
│   TELEGRAM   │
└──────────────┘
```

### Number

Use Micro 5.

The number:

```text
1–6
```

sits in the top-left corner.

It is the primary input reference.

Bound tiles do not draw an application icon. The uppercase label is the only
identity besides the slot number.

### Label

Short uppercase label:

```text
TELEGRAM
```

in the compact M5 bitmap font.

---

# 8. Monochrome Visual Language

The resting grid is almost entirely:

```text
Bone
Ink
Ordinal
Pale
```

Color is not used as decoration.

Bright colors appear only as semantic feedback:

```text
Blue       action accepted / executing
Leaf       success
Vermilion  failure
```

This keeps the existing Cardputer Hub UI language.

---

# 9. Empty Slots

An unbound slot still exists physically.

For example:

```text
┌──────────────┐
│ 2            │
│              │
│              │
│              │
└──────────────┘
```

Show:

```text
number → Ordinal
empty content
```

Do not write:

```text
EMPTY
UNBOUND
N/A
```

Pressing the matching digit:

```text
no-op
```

and does not start the takeover animation.

---

# 10. Direct-Key Interaction

On the current page:

```text
1 → slot 1
2 → slot 2
3 → slot 3
4 → slot 4
5 → slot 5
6 → slot 6
```

Trigger only on an accepted physical key-down edge.

Holding a digit must not produce:

```text
APP_ACTIVATE
APP_ACTIVATE
APP_ACTIVATE
...
```

While the previous command is Pending, new application-launch commands are not accepted.

---

# 11. Tile Status

Pressing a bound tile lights that square in place. There is no expand, flood,
digit scale, or collapse.

Sequence:

```text
user presses 1
        ↓
grid stays at rest while the command is Pending
        ↓
success lights the tile Leaf; failure lights it Vermilion
```

Do not take over the full screen.

---

# 12. Pending State

The pressed tile stays at rest until the command finishes. The other five tiles stay on the grid.

Example:

```text
┌──────────┬──────────┬──────────┐
│ 1        │ 2        │ 3        │
│ TELEGRAM │          │          │
│ OPENING  │          │          │
├──────────┼──────────┼──────────┤
│ 4        │ 5        │ 6        │
│          │          │          │
└──────────┴──────────┴──────────┘
```

Surface:

```text
Blue
```

Content:

```text
Bone
```

The tile keeps its resting numeral and label.

Do not use a spinner.

`OPENING` is enough for the semantic state.

---

# 13. Success Animation

When a successful `APP_ACTIVATE` response arrives:

```text
Blue
  ↓
Leaf
```

The full-screen takeover briefly becomes Leaf.

Example:

```text
┌────────────────────────────────────────┐
│                                        │
│                 1                      │
│                                        │
│             TELEGRAM                   │
│              OPENED                    │
│                                        │
└────────────────────────────────────────┘
```

Duration:

```text
~250–350 ms
```

After that the tile snaps back to the resting grid. No reverse animation.

Do not navigate to another Cardputer application.

The user stays in MAC CONTROL.

---

# 14. Error State

For example, the Telegram bundle ID is not found.

Takeover:

```text
Blue
 ↓
Vermilion
```

Show:

```text
TELEGRAM

NOT FOUND
```

or, for a generic failure:

```text
FAILED
```

After a short delay:

```text
~400–600 ms
```

snap back to the resting tile.

MAC CONTROL stays open.

---

# 15. Companion Loss

The MAC CONTROL AppDescriptor requires:

```text
COMPANION
```

If Companion disappears:

```text
CapabilityRegistry
    loses COMPANION
        ↓
MiniAppRuntime
    detects missing required capability
        ↓
MAC CONTROL deactivated
        ↓
ApplicationShell
        ↓
Launcher
```

Use the existing runtime lifecycle.

Do not add:

```text
BLE listener inside MAC CONTROL
Companion listener inside MAC CONTROL
manual disconnect screen
```

---

# 16. Companion Loss During Takeover

Scenario:

```text
press 1
↓
command Pending
↓
Mac disappears
```

The `COMPANION` capability is lost.

Behavior:

```text
cancel local animation state
deactivate MAC CONTROL
return Launcher
```

Do not show a Vermilion `FAILED` takeover when the required capability itself disappeared.

Launcher is already the correct unavailable state.

After reconnect:

```text
MAC CONTROL becomes eligible again
```

But:

```text
do not automatically reopen it
do not replay APP_ACTIVATE
```

---

# 17. Binding Model

The grid must not be written as:

```cpp
if (key == '1') {
    openTelegram();
}
```

Use data-driven fixed bindings.

Conceptually:

```cpp
struct MacControlBinding {
    std::uint8_t slot;
    std::string_view label;
    std::string_view iconId;
    core::Action action;
};
```

Page:

```cpp
struct MacControlPage {
    std::array<std::optional<MacControlBinding>, 6> slots;
};
```

Production 031:

```text
page 0
  slot 1
    number: 1
    icon: telegram
    label: TELEGRAM
    action:
      host.app.activate
      bundleId: <Telegram bundle ID>

  slots 2–6
    unbound
```

The paging/data model must support additional pages without changing Mini App input logic.

---

# 18. Telegram Bundle Identifier

Do not guess the identifier.

On the test/dev Mac, determine the real value:

```bash
mdls -name kMDItemCFBundleIdentifier -r /Applications/Telegram.app
```

That bundle ID is binding data.

Do not put a Telegram-specific identifier inside:

```text
Companion protocol
CompanionService
HostControlService
WorkspaceApplicationController
```

A persisted configuration will later replace the built-in binding table.

---

# 19. Logical Action

Use the production logical action:

```text
host.app.activate
```

Parameters:

```text
bundleId: string
```

Example:

```text
Action
├── id: host.app.activate
├── source: mac-control
└── bundleId: <Telegram bundle ID>
```

Follow the existing `appId` naming style, so the parameter is called:

```text
bundleId
```

and not `bundle-id`.

---

# 20. HostControlService

Add:

```text
src/services/host_control/
    host_control_service.h
    host_control_service.cpp
```

Boundary:

```text
Mini App
   ↓
Action
   ↓
HostControlService
   ↓
host transport/service
```

031 route:

```text
host.app.activate
        ↓
CompanionService.activateApplication(bundleId)
```

Future:

```text
HostControlService
       ↙       ↘
     HID     Companion
```

MAC CONTROL does not know the transport.

---

# 21. ActionBus Integration

Use the existing:

```text
core::ActionBus
```

`HostControlService` implements:

```text
IActionHandler
```

Register:

```text
host.app.activate
```

Flow:

```text
MAC CONTROL
     ↓
ActionBus::dispatch()
     ↓
HostControlService::handle()
```

Do not create an extra:

```text
CommandBus
MacActionBus
CompanionCommandRouter
```

---

# 22. HostControlService Validation

For `host.app.activate`:

```text
selected host exists
COMPANION capability available
bundleId exists
bundleId is string
bundleId non-empty
```

After that:

```text
CompanionService.activateApplication(bundleId)
```

Mapping:

```text
Submitted → Handled

NotReady
Busy
Invalid
    → Rejected
```

Do not:

```text
automatic HID fallback
different host fallback
automatic retry
request replay
```

---

# 23. Host-Control Command State

HostControlService owns the user-facing command lifecycle.

MAC CONTROL must not read raw `CompanionStatus`.

Expose a normalized snapshot, conceptually:

```cpp
enum class HostControlCommandState {
    Idle,
    Pending,
    Succeeded,
    Failed,
};

enum class HostControlFailure {
    None,
    NotFound,
    Unavailable,
    Busy,
    Invalid,
    Timeout,
    ProtocolError,
};

struct HostControlStatus {
    std::uint32_t generation;
    HostControlCommandState state;
    HostControlFailure failure;
};
```

MAC CONTROL observes this state.

`generation` distinguishes a new command completion from an old one.

---

# 24. One User Command at a Time

Although `CompanionService` supports several outstanding requests, MAC CONTROL 031 allows only:

```text
one user-visible host command
```

While:

```text
state == Pending
```

a new press on a bound tile is:

```text
ignored
```

This prevents overlapping takeovers and keeps the UI deterministic.

---

# 25. Completion Mapping

Companion completion:

```text
APP_ACTIVATE / OK
    → Succeeded

APP_ACTIVATE / NOT_FOUND
    → Failed / NotFound

timeout
    → Failed / Timeout

transport loss
    → Unavailable
```

Transport loss also usually removes `COMPANION`, so the Mini App closes back to Launcher.

---

# 26. MAC CONTROL Rendering State

Mini App state machine:

```text
Grid
  ↓ key 1
PendingTakeover
  ↓ response OK
SuccessTakeover
  ↓ hold 1.5 s
Grid
```

Failure:

```text
PendingTakeover
  ↓
FailureTakeover
  ↓ hold 2 s
Grid
```

Capability loss:

```text
ANY STATE
   ↓
MiniAppRuntime deactivation
   ↓
Launcher
```

---

# 27. Page Switching During Animation

While state is not:

```text
Grid
```

ignore:

```text
Left
Right
1–6
```

Escape remains shell-owned and may close the Mini App.

This prevents page mutation underneath an active takeover.

---

# 28. Icon Assets

031 does not ship application icons inside MAC CONTROL tiles.

Identity is the slot number plus the uppercase label. Do not add a Telegram
paper-plane bitmap or any other per-app glyph in this plan.

---

# 29. No Persistent Page Indicator in 031

Resting surface remains exactly the six tiles.

Do not add:

```text
PAGE 1/2
dots
header
bottom bar
```

Page movement itself communicates paging through the horizontal slide.

When production configuration eventually contains many pages, discoverability can be reassessed separately.

The paging model should still be implemented and unit-tested now.

---

# 30. Out of Scope

Do not add in 031:

```text
Zed production binding
Safari production binding
Terminal production binding
Finder
Spotify
Telegram voice-message action

application profiles
active-app profiles
APP_ACTIVE_CHANGED-driven mapping
layers
Tab layer switching

user-editable mappings
persistent mappings
mapping UI

HID shortcuts
media controls
window management
screenshots
lock Mac

menu-bar Companion UI
Companion settings UI

USB/Wi-Fi host control
```

Only Telegram is production-bound.

Other apps shown in mockups are visual examples, not 031 requirements.

---

# 31. Suggested File Layout

```text
src/apps/mac_control/
    mac_control_app.h
    mac_control_app.cpp
    mac_control_graphics.h
    mac_control_graphics.cpp
    mac_control_bindings.h
    mac_control_bindings.cpp

src/services/host_control/
    host_control_service.h
    host_control_service.cpp
```

Reuse:

```text
core/actions/
core/app_registry/
core/capabilities/
apps/runtime/
services/companion/
```

---

# 32. AppRegistry

Register:

```text
id: mac-control
displayName: MAC CONTROL
iconId: mac-control
entryRoute: mac-control
requiredCapabilities:
    COMPANION
```

Therefore Launcher automatically handles:

```text
Companion missing
→ MAC CONTROL unavailable
```

without Mini App-specific transport logic.

---

# 33. Tests — Binding / Paging

Test:

```text
slot numbers always map 1–6

page 0 / key 1 maps slot 1

page 1 / key 1 maps page-1 slot 1

page change does not modify bindings

unbound slot is no-op

Left on first page is no-op

Right on last page is no-op

multi-page Right moves forward

multi-page Left moves backward
```

Use a synthetic second page in tests even though production 031 contains one page.

---

# 34. Tests — MAC CONTROL

Test:

```text
registered in AppRegistry

requires COMPANION

cannot activate without COMPANION

activates with COMPANION

renders six fixed tiles

tile 1 renders:
    1
    TELEGRAM

tiles 2–6 render empty

key 1 dispatches exactly one host.app.activate

Action source == mac-control

Action bundleId correct

holding 1 does not repeatedly dispatch

keys 2–6 do nothing

Left/Right ignored during takeover

second command ignored while Pending

Escape closes Mini App

onDeactivate resets animation/pending local state
```

---

# 35. Tests — Animation

Inject elapsed time.

Verify:

```text
key press lights the source tile immediately

source rectangle == pressed tile bounds

expansion reaches full screen

Pending stays Blue

Succeeded changes surface to Leaf

Failed changes surface to Vermilion

success holds Leaf on the same tile for 1.5 s

failure holds Vermilion on the same tile for 2 s

animation stops requesting frames after returning to Grid
```

No real-time sleeps in unit tests.

---

# 36. Tests — HostControlService

Test:

```text
valid host.app.activate
→ CompanionService request

missing bundleId
→ Rejected

wrong parameter type
→ Rejected

empty bundleId
→ Rejected

Companion unavailable
→ Rejected

Companion busy
→ Rejected

OK completion
→ Succeeded

NOT_FOUND
→ Failed / NotFound

timeout
→ Failed / Timeout

transport loss
→ Unavailable

disconnect clears Pending

reconnect does not replay command
```

---

# 37. Runtime Capability Integration Test

Critical test:

```text
COMPANION available
        ↓
open MAC CONTROL
        ↓
runtime activeAppId == mac-control
        ↓
remove COMPANION
        ↓
MiniAppRuntime::update()
        ↓
DeactivatedMissingCapability
        ↓
ApplicationShell restores Launcher
```

This covers the validated physical Plan-030 behavior:

```text
close Mac lid
→ Companion disappears
→ MAC CONTROL exits
```

---

# 38. Physical Acceptance — Telegram Running

Setup:

```text
Cardputer paired
Companion ready
MAC CONTROL available
Telegram running
another Mac app foreground
```

Open MAC CONTROL.

Display:

```text
┌────────┬────────┬────────┐
│1       │2       │3       │
│ [TG]   │        │        │
│TELEGRAM│        │        │
├────────┼────────┼────────┤
│4       │5       │6       │
│        │        │        │
│        │        │        │
└────────┴────────┴────────┘
```

Press:

```text
1
```

Expected:

```text
tile 1 → Blue expand
→ full-screen OPENING
→ Telegram becomes foreground
→ Leaf OPENED
→ collapse to tile 1
→ grid
```

---

# 39. Physical Acceptance — Telegram Closed

Quit Telegram completely.

Press:

```text
1
```

Expected:

```text
Telegram launches
Telegram becomes foreground
Leaf success takeover
return grid
```

---

# 40. Physical Acceptance — Repeated Use

Run:

```text
1
wait complete
1
wait complete
1
```

Expected:

```text
no stuck Pending
no duplicate session issue
no Companion disconnect
no crash
correct animation every time
```

---

# 41. Physical Acceptance — Companion Loss

Open MAC CONTROL.

Close Mac lid.

Expected:

```text
Companion disappears
        ↓
COMPANION capability removed
        ↓
MAC CONTROL immediately deactivated
        ↓
Launcher
```

No internal disconnect/error screen.

Open lid.

Expected:

```text
BLE reconnects
Companion reconnects
MAC CONTROL becomes available
```

But:

```text
MAC CONTROL does not reopen automatically
```

---

# 42. Physical Acceptance — In-flight Loss

If timing permits:

```text
press 1
immediately break Mac connection
```

Expected:

```text
pending action invalidated
MAC CONTROL closes
Launcher appears
```

After reconnect:

```text
Telegram activation is NOT replayed
```

---

# 43. Physical Acceptance — Failure

Temporarily use a non-existent test bundle ID.

Press bound number.

Expected:

```text
Blue expansion
→ Vermilion
→ NOT FOUND
→ reverse collapse
→ grid
```

Companion remains healthy.

HID remains healthy.

MAC CONTROL remains open.

---

# 44. Documentation

Update:

```text
docs/ARCHITECTURE.md
docs/UI_REQUIREMENTS.md
docs/manuals/device-guide.md
docs/plans/README.md
```

Document:

```text
MAC CONTROL 3×2 numeric grid

1–6 mapping per page

monochrome application icon language

paged grid behavior

Left/Right page navigation

full-screen spatial takeover animation

Blue → Leaf success flow

Blue → Vermilion error flow

no connection chrome inside MAC CONTROL

host.app.activate Action

HostControlService boundary

Companion loss → Launcher

Telegram default binding
```

---

# 45. Recommended Implementation Order

```text
01. Add HostControlService

02. Add host.app.activate Action route

03. Add HostControlStatus normalized completion state

04. Wire Companion APP_ACTIVATE completions

05. Add MAC CONTROL AppDescriptor requiring COMPANION

06. Add MacControlBinding / MacControlPage model

07. Add production Telegram binding

08. Add synthetic multi-page tests

09. Add 3×2 full-screen grid geometry

10. Add numeric 1–6 rendering

11. Bind TELEGRAM by label only; no per-app icon

12. Add direct numeric input

13. Add Left/Right page state

14. Add horizontal page transition

15. Add source-tile Blue activation

16. Add tile → full-screen expansion

17. Add Pending Blue takeover

18. Add Leaf success state

19. Add Vermilion failure state

20. Add reverse collapse

21. Wire capability loss through existing MiniAppRuntime lifecycle

22. Add unit/integration tests

23. Run host-check

24. Run firmware-check

25. Run firmware-size

26. Run CI

27. Flash Cardputer

28. Verify 1 → Telegram focus

29. Verify 1 → Telegram launch

30. Verify disconnect → Launcher

31. Verify reconnect / no replay

32. Update docs
```

---

# 46. Completion Criteria

031 complete when:

```text
[ ] MAC CONTROL appears in Launcher

[ ] MAC CONTROL requires COMPANION

[ ] no header/status/footer is rendered inside MAC CONTROL

[ ] resting screen is a full-screen 3×2 grid

[ ] every page maps slots to physical numbers 1–6

[ ] each bound tile renders:
      number
      monochrome icon
      label

[ ] unbound tiles retain their number but perform no action

[ ] paging model supports more than six bindings

[ ] Left/Right switch pages

[ ] page transition uses horizontal slide

[ ] numeric key directly activates corresponding current-page slot

[ ] Telegram is production-bound to key 1

[ ] MAC CONTROL emits logical host.app.activate

[ ] Mini App does not call CompanionService directly

[ ] HostControlService routes Action to CompanionService

[ ] Telegram running → focused

[ ] Telegram closed → launched and focused

[ ] accepted key waits on the resting grid

[ ] pressed tile stays at rest until Leaf or Vermilion

[ ] pending command does not light the tile

[ ] successful activation produces Leaf success state

[ ] failure produces Vermilion failure state

[ ] status hold then snaps back to the resting tile

[ ] settled grid stops animation frames

[ ] only one user command may be Pending

[ ] no command is replayed after reconnect

[ ] Companion loss automatically closes MAC CONTROL

[ ] Companion loss returns to Launcher

[ ] reconnect does not reopen MAC CONTROL

[ ] native tests pass

[ ] host-check passes

[ ] firmware-check passes

[ ] firmware-size passes

[ ] CI passes

[ ] physical Telegram focus passes

[ ] physical Telegram launch passes

[ ] physical disconnect/reconnect passes
```

---

# 47. End State

After Plan 031:

```text
┌─────┬─────┬─────┐
│  1  │  2  │  3  │
│ TG  │     │     │
├─────┼─────┼─────┤
│  4  │  5  │  6  │
│     │     │     │
└─────┴─────┴─────┘
   │
   │ press 1
   ▼
Blue tile
   ↓
Blue full-screen takeover
   ↓
host.app.activate
   ↓
HostControlService
   ↓
CompanionService
   ↓
Cardputer Companion.app
   ↓
Telegram
   ↓
Leaf success
   ↓
collapse
   ↓
grid
```
