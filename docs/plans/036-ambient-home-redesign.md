# 036 — Ambient Home Redesign

## Current status

**Software implemented — Home presentation, tests, and user documentation updated;
physical Cardputer-Adv visual acceptance pending.**

Plan [036/2](036-2-home-actions-and-connected-device-context.md) supersedes
the Home layout, Companion indicator, host-name, and controls below. This
document retains the original design rationale and historical implementation
record.

This plan replaces the current Mac-centric Home screen with a device-centric ambient Home.

The current firmware Home still contains:

- the placeholder `--:--` clock;
- selected-host label and host name;
- large Bluetooth connection status;
- Companion indicator attached to the host name;
- Wi-Fi status;
- battery percentage;
- the existing 28-second dotted wave rendered in the lower 36 rows at up to 2 FPS.

The project has evolved beyond a single-purpose Mac assistant. Cardputer Hub is now a general personal device platform built around independent Mini Apps and reusable Services.

Home should reflect that architecture.

---

# 0. Goal

Redesign Home as a quiet but visibly alive idle screen for Cardputer Hub itself.

Home must:

- stop presenting the selected Mac/host as its primary content;
- remove the unused clock placeholder;
- keep a compact global device-status bar;
- use a procedural ambient animation as the main visual content;
- feel noticeably more alive than the current slow dotted wave;
- remain calm enough to run continuously on a desk;
- remain independent from individual Mini Apps;
- preserve the existing Home navigation model.

Home is **not** becoming a Launcher or dashboard.

The intended mental model is:

```text
Home
=
global device status
+
ambient procedural visual
```

Mini App state belongs inside Mini Apps.

---

# 1. Final UX

Target resting Home:

```text
┌────────────────────────────────────────┐
│ ● WiFi   ● BT ◆                   82% │
├────────────────────────────────────────┤
│                                        │
│             ·      ·                   │
│        ·                 ·             │
│     ·        ·   ·          ·          │
│        ·                 ·             │
│             ·      ·                   │
│                                        │
└────────────────────────────────────────┘
```

The exact particle positions above are illustrative.

The body contains no permanent text.

Home continues to use the existing global controls:

```text
Enter     open Launcher
Tab       open Settings
```

Existing Back/navigation semantics remain unchanged.

Do not add:

```text
ENTER APPS
TAB SETTINGS
ESC BACK
```

or other permanent navigation hints.

---

# 2. Remove Mac-centric Home content

Remove the current Home presentation of:

```text
--:--

SELECTED HOST
<host name>

Bluetooth icon
READY / CONNECTING / SECURING / PAIRING / OFF / ERROR
```

The selected host remains available through the existing Bluetooth/host UI and relevant Mini Apps.

The Home screen must not display:

- selected host name;
- host ID;
- host platform;
- MAC CONTROL state;
- any Mini App state;
- application count;
- currently selected Launcher item.

This is a presentation change only.

Do not remove or weaken:

- `HostService`;
- host selection;
- Bluetooth reconnect behavior;
- Companion;
- MAC CONTROL;
- Launcher;
- Device Manager foundations.

---

# 3. Remove the clock slot

Remove:

```text
--:--
```

from Home.

Do not implement time synchronization as part of this plan.

Do not replace the clock with:

- date;
- uptime;
- greeting;
- app status;
- another permanent textual metric.

A future time feature may add an optional compact clock separately if there is a real use case.

---

# 4. Home status bar

The top region becomes a stable global status bar.

Target:

```text
● WiFi   ● BT ◆                         82%
```

The bar contains exactly four logical slots:

```text
Wi-Fi
Bluetooth
Companion
Battery
```

The Wi-Fi and Bluetooth groups are always present and always occupy the same coordinates.

The Companion slot has fixed reserved space but is visually empty while Companion is unavailable.

Battery remains right-aligned.

The layout must not move when:

- Wi-Fi changes state;
- Bluetooth changes state;
- Companion connects/disconnects;
- battery changes between one-, two-, and three-digit values.

Conceptually:

```text
| WiFi | BT | Companion reserve | flexible space | Battery |
```

Do not let the appearance of `◆` shift either `BT` or the battery value.

---

# 5. Wi-Fi indicator

Keep Wi-Fi as a compact status dot plus the permanent `WiFi` label.

Conceptually:

```text
● WiFi
```

Reuse the existing `NetworkService` snapshot as the source of truth.

Recommended mapping:

```text
not configured       ○ Ordinal
configured / off     ● Pale
connecting           ● Blue
connected            ● Leaf
error                 ● Vermilion
```

Do not show:

- SSID;
- RSSI;
- IP address;
- ONLINE/OFFLINE text.

Those belong in Wi-Fi Settings or future diagnostic surfaces.

The Wi-Fi label itself remains stable Ink text.

---

# 6. Bluetooth indicator

Bluetooth receives the same compact treatment:

```text
● BT
```

Use the existing `HostService` domain status as the current source of truth.

Recommended mapping:

```text
Off                   ○ Ordinal
Connecting            ● Blue
Securing              ● Blue
Pairing                ● Blue
Ready                  ● Leaf
Error                  ● Vermilion
```

Do not show the connection-state text on Home.

Do not show the selected host name.

Detailed Bluetooth state remains available in the existing Bluetooth UI and SYSTEM Mini App.

---

# 7. Companion indicator

Use:

```text
◆
```

as the compact Companion mark.

The mark appears **if and only if the existing live Companion capability is available**.

Use the existing capability source of truth:

```cpp
capabilities_.isAvailable("COMPANION")
```

or its existing canonical capability ID equivalent.

Do not infer Companion state from:

- Bluetooth `Ready`;
- selected host;
- MAC CONTROL availability by duplicated logic;
- host platform.

Expected behavior:

```text
Companion disconnected:

● WiFi   ● BT                         82%


Companion connected:

● WiFi   ● BT ◆                       82%
```

The slot remains reserved in both cases.

Recommended Companion color:

```text
Leaf
```

No additional text such as:

```text
COMPANION
CONNECTED
MAC
```

is needed on Home.

---

# 8. Battery

Keep the existing estimated battery percentage.

Examples:

```text
8%
42%
82%
100%
--%
```

Continue using the existing `BatteryService` snapshot passed into `ApplicationShell`.

Do not:

- read battery hardware directly from Home;
- add a battery icon;
- reinterpret the current estimate as a calibrated state-of-charge measurement.

The battery string remains right-aligned to a fixed right edge.

Changing its width must not move the other status items.

---

# 9. Status-bar layout

Use a compact top strip with a single structural separator below it.

Conceptually:

```text
0
┌────────────────────────────────────────┐
│ ● WiFi   ● BT ◆                   82% │
├────────────────────────────────────────┤
│                                        │
│            ambient area                │
│                                        │
135
```

Suggested geometry:

```text
status bar:
y = 0..20

separator:
y = 20 or 21

ambient viewport:
remaining screen height
```

Exact pixel anchors may be tuned during implementation, but once chosen they must be stable.

Use the existing:

```text
Bone background
Ink labels
semantic status colors
one-pixel structural separator
```

Do not add cards, rounded boxes, shadows, gradients, or a second header.

---

# 10. Ambient visual — Living Orb

Replace the existing lower dotted wave with a procedural **Living Orb** / particle-organism animation.

The target is not a literal sphere.

It should feel like a coherent soft object made from moving particles:

```text
          ·    ·

      ·            ·

    ·       · ·       ·

      ·            ·

          ·    ·
```

The shape should continuously:

- breathe;
- gently rotate;
- deform;
- drift by a few pixels;
- vary particle depth/size;
- remain recognizably coherent.

It must not look like:

- random TV noise;
- a starfield flying toward the viewer;
- a loading spinner;
- a screensaver changing between unrelated effects;
- the current wave merely moving faster.

---

# 11. Particle model

Use a deterministic fixed-size particle set.

Recommended initial design:

```text
40 particles total

28 outer particles
12 inner particles
```

No heap allocation is needed per frame.

Conceptually:

```cpp
struct HomeAmbientParticle {
    PixelPosition position;
    std::uint8_t size;
    HomeAmbientTone tone;
};
```

A fixed array is preferred:

```cpp
std::array<HomeAmbientParticle, 40>
```

Particle identity remains stable between frames.

Do not randomly regenerate positions every frame.

---

# 12. Base geometry

Start with a loose elliptical/orbital distribution around approximately the center of the available ambient area.

Suggested visual envelope:

```text
center X ≈ 120

horizontal radius ≈ 55–65 px
vertical radius   ≈ 34–42 px
```

The orb should occupy a meaningful portion of the display while leaving visible breathing room around it.

It must not touch:

- the status bar;
- the separator;
- screen edges.

The entire animation must remain clipped to an explicit ambient viewport.

---

# 13. Motion layers

Combine several slow deterministic motions instead of one sinusoid.

## 13.1 Breathing

The whole organism expands and contracts.

Suggested period:

```text
~8–10 seconds
```

Suggested radial amplitude:

```text
approximately ±8%
```

This should be the most immediately visible motion.

---

## 13.2 Rotation

Slowly rotate the particle arrangement.

Suggested period:

```text
~18–24 seconds per revolution
```

Rotation should not be the only motion.

---

## 13.3 Shape morphing

Apply low-frequency spatial deformation.

For example, radial offsets can combine harmonics conceptually similar to:

```text
sin(3θ + phaseA)
sin(5θ - phaseB)
```

with small amplitudes.

The goal is for the orb to subtly change between:

```text
round
slightly oval
soft asymmetric blob
round again
```

Avoid sharp or chaotic deformation.

Suggested morph periods:

```text
~11 seconds
~17 seconds
```

Using different non-identical periods prevents the loop from feeling obviously repetitive.

---

## 13.4 Center drift

Move the overall center slightly.

Suggested maximum drift:

```text
horizontal: ±4–6 px
vertical:   ±2–4 px
```

Suggested periods:

```text
~13–20 seconds
```

The object should never appear to bounce against fixed boundaries.

---

## 13.5 Local particle motion

Individual particles may receive a small additional phase offset.

Suggested magnitude:

```text
1–3 px
```

This gives the organism internal motion instead of behaving like a rigid rotating logo.

Particle positions must still be deterministic.

---

# 14. Particle depth

Use subtle size variation to create a sense of depth.

Allowed visual sizes:

```text
1×1
2×2
3×3
```

Most particles should remain small.

Only a small number should be `3×3` at any one time.

Depth may be derived from a deterministic pseudo-Z value associated with rotation or particle phase.

For example:

```text
far       1×1
middle    2×2
near      3×3
```

Size changes must be gradual in visual effect.

Do not randomly flicker between sizes.

---

# 15. Ambient colors

Ambient animation is decorative and must not compete with semantic status colors.

Prefer only neutral palette tokens:

```text
Home wave
Ordinal
Ink
```

Suggested treatment:

```text
far particles       Home wave
middle particles    Ordinal
nearest particles   Ink
```

Use Ink sparingly so the animation remains soft.

Do not use:

```text
Blue
Leaf
Vermilion
```

as continuously cycling decorative colors.

Those colors remain reserved for meaningful state in the status bar and other system UI.

This ensures:

```text
green = connected/success
blue  = transitional/active
red   = error/action required
```

continues to have semantic value.

---

# 16. Animation cadence

The current Home wave uses approximately:

```text
28-second cycle
maximum 2 visual updates per second
```

The new ambient visual should be clearly smoother and more active.

Target:

```text
20 FPS
```

Equivalent render interval:

```text
50 ms
```

The normal UI scheduler may continue to tick at its existing cadence.

Home owns its own animation redraw throttle.

Do not redraw the ambient viewport on every 20 ms scheduler update when the 50 ms ambient interval has not elapsed.

Do not exceed the target frame rate merely because UI updates arrive faster.

---

# 17. Elapsed-time behavior

Animation must use injected monotonic elapsed time.

Do not base movement on:

- frame count;
- wall clock;
- `delay()`;
- blocking loops.

If one UI update arrives late:

```text
elapsed = 180 ms
```

advance the animation phase by the elapsed amount but render only the current resulting frame.

Do not perform catch-up bursts such as:

```text
render frame
render frame
render frame
render frame
```

in one application update.

This follows the existing scheduler design.

---

# 18. Pause semantics

Preserve the current Home animation ownership rules.

## Home hidden

When Launcher, Settings, or a Mini App is visible:

```text
ambient animation pauses
```

Returning Home resumes from the retained phase.

Do not let hidden Home animation consume rendering work.

---

## Display Off

When the backlight is in the existing final Off state:

```text
ambient phase pauses
ambient frame rendering pauses
```

The existing framebuffer/state update contract for meaningful system state remains unchanged.

When Home becomes visible again, ambient motion resumes without a large phase jump.

---

## Navigation transition

While a Home page transition is active:

```text
freeze ambient phase
```

The transition should animate the already-rendered Home frame rather than simultaneously animating the organism underneath the slide.

Preserve the current transition behavior.

---

# 19. Display-power contract

Ambient motion is presentation only.

It must never:

- reset the display idle timer;
- call `requestWake()`;
- count as user activity;
- prevent dimming;
- prevent Off;
- synthesize input.

The display-power state machine introduced by plan 032 and configured by plan 035 remains authoritative.

The new Home must work correctly with:

```text
Normal timeout
Long timeout
Never
```

and all configured screen-brightness values.

---

# 20. Rendering strategy

Do not redraw the whole LCD for every ambient frame.

Split Home into two logical regions:

```text
status bar
ambient viewport
```

Status changes redraw only the corresponding status area.

Ambient animation redraws only the bounded ambient viewport.

Conceptually:

```text
┌────────────────────────────────────────┐
│       static/dynamic status bar        │ <- independently dirty
├────────────────────────────────────────┤
│                                        │
│                                        │
│          ambient viewport              │ <- animation dirty region
│                                        │
│                                        │
└────────────────────────────────────────┘
```

Clearing the bounded ambient viewport before drawing the next particle frame is acceptable.

Do not repaint the status bar merely because the animation advanced.

---

# 21. Home render cache

Replace the existing host-oriented Home frame state with device-oriented state.

Current Home state includes concepts such as:

```text
activeHost
hostName
HostConnectionStatus
companionReady
```

The new cache should instead model only what Home actually displays.

Conceptually:

```cpp
struct HomeStatusFrame {
    HomeWifiIndicator wifi;
    HomeBluetoothIndicator bluetooth;
    bool companionReady;
    std::optional<std::uint8_t> batteryPercent;
};
```

Exact structure may differ.

Remove host name from Home presentation state.

Keep Bluetooth domain status only as necessary to derive the compact BT indicator.

---

# 22. Home ambient state

Keep ambient presentation state owned by the shell/Home presentation.

Conceptually:

```cpp
std::uint32_t homeAmbientPhaseMs;
std::uint32_t homeAmbientFrameAccumulatorMs;
```

or a small dedicated presentation helper.

Do not create a Service for this animation.

This is UI presentation state, not reusable domain state.

---

# 23. Suggested source structure

The current `home_graphics.cpp` already owns Home-specific graphics.

The redesign may either extend it or split the procedural model for clarity.

Preferred structure:

```text
src/apps/shell/
    application_shell.cpp
    application_shell.h

    home_graphics.cpp
    home_graphics.h

    home_ambient.cpp
    home_ambient.h
```

Suggested responsibilities:

```text
home_graphics
    status-bar geometry
    Wi-Fi indicator drawing
    Bluetooth indicator drawing
    Companion diamond
    battery/status layout
    ambient viewport constants

home_ambient
    deterministic particle geometry
    motion phases
    depth/size calculation
    particle tone calculation
```

`ApplicationShell` owns:

```text
Home lifecycle
elapsed time
animation throttle
Service snapshots
dirty-state comparison
navigation
```

Do not move Home-specific procedural animation into System Core.

---

# 24. Remove obsolete Home graphics

The following current Home-specific presentation becomes obsolete:

```text
fitHomeHostName()
homeCompanionIndicatorPosition(name)
drawHomeHostName()
large body Bluetooth icon
drawHomeWave()
homeHostNameOriginX
homeHostNameRightEdge
homeHostNameMaxWidth
```

Remove obsolete code rather than retaining two Home implementations.

The current Home-only Micro 5 host-name asset should also be reviewed:

```text
src/apps/shell/assets/micro5_home.h
```

If it has no remaining production use after this redesign, remove:

- the unused generated asset;
- obsolete documentation specifically describing the old Home host-name font;
- generation tooling only if it has no remaining use elsewhere.

Do not remove shared Micro 5 assets used by other screens.

---

# 25. Navigation remains unchanged

This plan does not redesign system navigation.

Required behavior remains:

```gherkin
Given Home is visible
When the user presses Enter
Then Launcher opens
```

```gherkin
Given Home is visible
When the user presses Tab
Then Settings opens
```

Returning from Launcher or system UI returns to the ambient Home.

No extra intermediate page is introduced.

---

# 26. BDD — default Home

```gherkin
Scenario: Home renders the device-centric idle presentation
  Given normal firmware has finished the startup splash
  And no Mini App is active
  When Home is rendered
  Then the WiFi status group is visible
  And the BT status group is visible
  And battery percentage is visible when available
  And the ambient Living Orb is visible
  And no clock placeholder is visible
  And no selected-host label is visible
  And no host name is visible
  And no Bluetooth state text is visible
```

---

# 27. BDD — Companion connection

```gherkin
Scenario: Companion indicator appears
  Given Home is visible
  And the Companion capability is unavailable
  Then the Companion slot is visually empty

  When the Companion capability becomes available
  Then a Companion diamond appears in the reserved slot
  And the WiFi group does not move
  And the BT group does not move
  And the battery does not move
```

```gherkin
Scenario: Companion indicator disappears
  Given Home is visible
  And the Companion diamond is visible
  When the Companion capability becomes unavailable
  Then only the Companion slot is cleared
  And the remaining status-bar layout stays unchanged
```

---

# 28. BDD — Wi-Fi changes

```gherkin
Scenario: Wi-Fi state changes while Home is visible
  Given Home is visible
  When NetworkService changes from Connecting to Connected
  Then the Wi-Fi indicator changes from Blue to Leaf
  And the WiFi label remains in the same location
  And Bluetooth is not redrawn unnecessarily
  And the ambient viewport is not reset
```

---

# 29. BDD — Bluetooth changes

```gherkin
Scenario: Bluetooth becomes ready
  Given Home is visible
  And HostService reports Connecting
  When HostService reports Ready
  Then the BT indicator changes from Blue to Leaf
  And no host name is shown
  And no READY text is shown
```

---

# 30. BDD — animation advance

```gherkin
Scenario: Ambient animation advances smoothly
  Given Home is visible
  And the display is awake
  When at least one ambient frame interval elapses
  Then the particle geometry changes
  And the status bar remains unchanged
```

```gherkin
Scenario: Scheduler ticks faster than ambient animation
  Given Home is visible
  When less than 50 ms of accumulated ambient time has elapsed
  Then no new ambient frame is presented solely for the animation
```

---

# 31. BDD — delayed update

```gherkin
Scenario: A delayed UI update does not create catch-up rendering
  Given Home is visible
  When a single update receives 250 ms elapsed
  Then ambient phase advances by the elapsed duration
  And at most one resulting ambient frame is rendered
  And no catch-up frame loop occurs
```

---

# 32. BDD — display Off

```gherkin
Scenario: Ambient animation pauses while display is Off
  Given Home is visible
  And the ambient animation has reached a known phase
  When the display enters Off
  And time continues to pass
  Then the ambient phase does not advance

  When the display wakes
  Then animation resumes from the retained phase
```

The wake-only key remains consumed according to the existing display-power contract.

---

# 33. BDD — hidden Home

```gherkin
Scenario: Home animation pauses behind Launcher
  Given Home is visible
  And the ambient animation has reached a known phase
  When Launcher opens
  And time passes
  And the user returns to Home
  Then the ambient animation resumes from the retained phase
  And hidden Home did not render animation frames
```

---

# 34. BDD — battery unavailable

```gherkin
Scenario: Battery reading is unavailable
  Given Home is visible
  And BatteryService has no valid percentage
  Then the battery slot displays --%
  And the right edge remains aligned with normal percentage values
```

---

# 35. Ownership matrix

| Concern | Owner | Must not own |
| --- | --- | --- |
| Home route/navigation | `ApplicationShell` | Mini App behavior |
| Home status snapshots/cache | `ApplicationShell` | network/BLE policy |
| Ambient elapsed phase and redraw throttle | Home/ApplicationShell presentation | display-power state |
| Ambient particle geometry | `home_ambient` presentation helper | Services or hardware |
| Home status drawing | `home_graphics` | Service state transitions |
| Wi-Fi state | `NetworkService` | Home drawing code |
| Bluetooth/host connection state | `HostService` | Home drawing code |
| Companion availability | `CompanionService` / `CapabilityRegistry` | inferred Home state |
| Battery estimate | `BatteryService` | display adapter |
| Display idle/dim/off/wake | `DisplayPowerController` / `SystemRuntime` | ambient animation |
| Screen timeout/brightness configuration | `DeviceSettingsService` | Home |
| Framebuffer, dirty presentation, transitions | display adapter | Home domain policy |
| Mini App state | individual Mini Apps/Services | Home |

---

# 36. Invariants

The implementation must preserve these invariants.

### Home is application-neutral

```text
Home never reads state from PomodoroApp, LED Gallery, MAC CONTROL, Launcher,
or future application-specific Services solely to decorate the idle screen.
```

### Status positions are stable

```text
WiFi anchor never moves.
BT anchor never moves.
Companion slot never moves.
Battery right edge never moves.
```

### Companion has one source of truth

```text
◆ visible
⇔
live Companion capability available
```

### Ambient animation is presentation only

```text
ambient motion
≠ user activity
≠ wake request
≠ navigation
≠ Service state
```

### Ambient rendering is bounded

```text
ambient drawing never overwrites the status bar
```

### Hidden animation does no work

```text
Home hidden or display Off
→
no ambient frame rendering
```

### Animation is deterministic

For the same phase and constants:

```text
particle positions
particle sizes
particle tones
```

must be identical.

### Semantic colors remain semantic

Continuous decorative animation must not cycle through Blue, Leaf, and Vermilion.

---

# 37. Host-side tests

Update existing Home tests rather than layering the new behavior on top of obsolete host-centric expectations.

Relevant existing suites currently include Home checks in:

```text
test/test_application_shell/
test/test_wifi_settings/
test/test_host_settings/
test/test_host_service/
```

Rename or remove old tests whose only purpose was the old selected-host Home layout.

Add coverage for:

```text
Home no longer renders --:--
Home no longer renders SELECTED HOST
Home no longer renders host name
Home no longer renders READY / CONNECTING / PAIRING text

WiFi group always present
BT group always present
Companion diamond conditional
battery fixed alignment

Wi-Fi indicator mapping
Bluetooth indicator mapping

Companion connect redraw
Companion disconnect redraw

status anchors do not change
battery width does not shift other groups

ambient frame deterministic for known phase
ambient particles stay inside viewport
ambient particles change across phases
ambient particle count remains fixed

ambient 50 ms frame throttle
large elapsed produces one render
display Off freezes phase
Home hidden freezes phase
transition freezes phase

Enter still opens Launcher
Tab still opens Settings
wake-only input behavior remains unchanged
```

Avoid tests that assert implementation-specific trigonometric expressions.

Prefer behavioral geometry invariants.

---

# 38. Ambient geometry tests

For deterministic phases such as:

```text
0 ms
2,000 ms
4,000 ms
8,000 ms
```

verify:

- exact particle count;
- every particle remains inside the ambient viewport;
- size remains within `1..3`;
- only allowed ambient tones are used;
- at least a meaningful subset of particle coordinates changes between phases;
- average radius changes during breathing;
- the center drift remains within its configured bounds.

A representative captured frame may also be used for visual review, but screenshot tests must not replace behavioral tests.

---

# 39. Incremental-render tests

Verify that changing:

```text
battery
Wi-Fi status
Bluetooth status
Companion availability
```

does not require redrawing the ambient viewport.

Verify that an ambient animation frame does not redraw the status bar.

The existing display fake/dirty-region infrastructure should be reused where practical.

Do not weaken plan-022 incremental-rendering guarantees.

---

# 40. Performance requirements

Target:

```text
20 ambient FPS
40 particles
no per-frame heap allocation
no blocking delay
no catch-up rendering
```

The animation must coexist with:

- Bluetooth;
- Companion traffic;
- Wi-Fi;
- Pomodoro;
- IndicatorService;
- keyboard polling;
- display-power transitions.

Do not move time-sensitive Service updates into UI rendering.

The existing main-loop ordering remains unchanged.

If profiling reveals that 20 FPS causes visible input, Connectivity, or display-transfer problems on physical hardware, optimize the procedural renderer before reducing the design to the old low-frequency motion.

---

# 41. Physical visual acceptance

Validate on a real Cardputer-Adv.

The Home screen should be observed continuously for at least several minutes.

Confirm:

- animation is clearly more noticeable than the old 2 FPS dotted wave;
- motion remains calm rather than distracting;
- breathing is visible;
- shape deformation is visible;
- movement is smooth;
- the loop does not obviously repeat every few seconds;
- the orb remains visually coherent;
- no particles touch the status bar;
- no visible stale pixels remain between frames;
- status text does not jitter;
- Companion appearance causes no layout jump.

Check at several configured screen brightness levels, including:

```text
20%
50%
100%
```

Also verify readability after automatic dimming.

---

# 42. Physical state acceptance

Exercise at least:

```text
Wi-Fi connected
Wi-Fi disabled or unavailable

Bluetooth Off
Bluetooth connecting
Bluetooth Ready

Companion disconnected
Companion connected

battery percentage update
```

Confirm the status bar reacts without disturbing the animation.

---

# 43. Navigation acceptance

On physical hardware verify:

```text
boot
↓
Home ambient screen

Enter
↓
Launcher

Esc
↓
Home ambient screen

Tab
↓
Settings

Tab / Back according to current Settings behavior
↓
Home ambient screen
```

Transitions must remain smooth.

The Home organism must not visibly continue animating underneath a page slide.

---

# 44. Display-power acceptance

Verify all current timeout modes introduced by plan 035:

```text
Normal
Long
Never
```

At minimum physically confirm:

- ambient animation does not prevent dimming;
- ambient animation does not prevent Off;
- the first key from Dimmed/Off remains wake-only;
- the animation resumes correctly after wake;
- `Never` allows the ambient Home to run continuously;
- changing configured screen brightness continues to affect Home correctly.

---

# 45. Documentation updates

Update the project documentation in the same change.

## `docs/UI_REQUIREMENTS.md`

Replace the old Home contract describing:

```text
selected host
clock slot
large Bluetooth state
28-second lower dotted wave
2 FPS
```

with:

```text
global status bar
WiFi
BT
conditional Companion diamond
battery
Living Orb ambient viewport
20 FPS target
pause behavior
```

Update the Home Wave design-token wording if necessary so it describes a general ambient neutral rather than specifically the old wave.

---

## `docs/ARCHITECTURE.md`

Update Home/Application Shell documentation to state that Home is device-centric and application-neutral.

Remove statements that define selected-host presentation as the Home contract.

Document that:

```text
Home consumes compact global status snapshots
but does not expose Mini App state.
```

---

## `README.md`

Update the current-firmware description.

Replace wording equivalent to:

```text
Home shows selected host, BT state, time placeholder and slow dotted wave
```

with the new ambient Home behavior.

---

## `docs/manuals/device-guide.md`

Update the user-facing Home description and controls.

Document:

```text
● WiFi
● BT
◆ when Companion is connected
battery percentage
ambient Living Orb
Enter → Apps
Tab → Settings
```

Do not describe implementation details such as harmonic functions or FPS in the user guide unless useful.

---

## `docs/plans/README.md`

Add:

```text
036 | Ambient Home redesign | Planned / implemented status as appropriate
```

and update its review date.

---

# 46. Non-goals

This plan does not include:

- turning Home into Launcher;
- favorite/recent apps;
- app widgets;
- Pomodoro summary;
- Cursor summary;
- VPS summary;
- weather;
- notifications;
- time synchronization;
- date;
- RSSI display;
- selected host display;
- configurable Home themes;
- user-selectable ambient effects;
- LED Puzzle synchronization with Home;
- ambient sound;
- sleep/deep-sleep changes;
- new Companion protocol messages.

Those may be separate plans later.

---

# 47. Recommended implementation order

## Step 1 — Lock new Home tests

Add failing behavioral tests for:

```text
new status bar
removed host/clock content
conditional Companion diamond
navigation preservation
```

---

## Step 2 — Replace Home layout

Remove:

```text
clock
selected-host presentation
large BT presentation
old Companion placement
```

Implement the static status-bar geometry.

Keep the old wave temporarily if useful during this intermediate step.

---

## Step 3 — Add compact BT status mapping

Introduce a Home-level presentation enum/helper equivalent to the existing Wi-Fi indicator mapping.

Keep domain enums out of low-level drawing helpers where practical.

---

## Step 4 — Add Living Orb model

Implement deterministic particle geometry independently from shell routing.

Test:

```text
bounds
count
size
motion
tone
```

before animation is wired into Home.

---

## Step 5 — Replace dotted wave

Remove `drawHomeWave()` and integrate the new ambient renderer.

Use a bounded viewport.

---

## Step 6 — Add animation throttle

Implement:

```text
50 ms ambient render interval
```

using injected elapsed time.

Ensure delayed updates produce one current frame rather than catch-up rendering.

---

## Step 7 — Preserve pause behavior

Explicitly verify:

```text
Home hidden
display Off
page transition
```

all pause ambient phase/rendering according to this plan.

---

## Step 8 — Incremental rendering

Ensure:

```text
status update → status-only dirty region
ambient frame → ambient-only dirty region
```

---

## Step 9 — Remove obsolete Home assets/code

Delete old host-name rendering and Home-only assets when no longer referenced.

Run architecture/static checks to catch remaining dead dependencies.

---

## Step 10 — Update documentation

Update:

```text
README
ARCHITECTURE
UI_REQUIREMENTS
device guide
plans index
```

---

## Step 11 — Full validation

Run:

```bash
make test
make format
make format-check
make lint
make firmware-check
make check
```

Also inspect:

```bash
make firmware-size
```

to catch any unexpectedly large increase from the procedural renderer.

---

## Step 12 — Physical tuning

Flash the final image and tune only visual constants if required:

```text
orb radius
breathing amplitude
particle sizes
neutral tone distribution
drift amplitude
motion periods
```

Do not change the architectural contract during visual tuning.

---

# 48. Acceptance criteria

Plan 036 is complete when all of the following are true:

- Home no longer shows a clock placeholder.
- Home no longer shows selected host information.
- Home no longer presents Mac/Bluetooth state as dominant content.
- `WiFi` is permanently present in the top bar.
- `BT` is permanently present in the top bar.
- both status groups retain fixed positions.
- `◆` appears only while Companion is live.
- Companion appearance/disappearance causes no layout shift.
- battery remains right-aligned.
- the old lower dotted wave is removed.
- the Living Orb occupies the main Home body.
- the Living Orb combines breathing, rotation, deformation, drift, and particle depth.
- the animation is deterministic.
- the animation targets 20 FPS.
- no per-frame heap allocation is required.
- the ambient renderer is bounded below the status bar.
- ambient motion does not reset display idle state.
- ambient motion does not wake the display.
- animation pauses while Home is hidden.
- animation pauses while the display is Off.
- animation freezes during page transitions.
- delayed frames do not cause catch-up rendering.
- Home remains application-neutral.
- `Enter` still opens Launcher.
- `Tab` still opens Settings.
- display-power behavior from plans 032 and 035 remains intact.
- incremental rendering remains intact.
- host tests pass.
- firmware build passes.
- physical Cardputer-Adv validation confirms the animation is smooth, clearly more alive than the previous wave, and still calm enough for continuous desk use.
