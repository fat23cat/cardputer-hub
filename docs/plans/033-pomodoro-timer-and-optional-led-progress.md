# 033 — Pomodoro Timer & Optional LED Progress

Status: **Software implemented; physical Cardputer-Adv LED and display-wake
acceptance pending**

# 0. Current Status

Delivered in software:

```text
Pomodoro Mini App
background PomodoroService
optional Unit Puzzle LED progress
phase-change audio
programmatic LCD wake on phase transition
```

Physical Cardputer-Adv acceptance of the LED matrix and of the display-wake
addendum remains. The wake contract is in **Addendum — Pomodoro Phase
Transition Display Wake** at the end of this file.

Suggested branch:

```text
feat/033-pomodoro-led-progress
```

Suggested PR:

```text
[033] Add Pomodoro timer and optional LED progress
```

---

## 0. Starting Point

Plan 031 is merged into `main`.

The project already has:

```text
AppRegistry
MiniAppRuntime
ApplicationShell
ActionBus
ConfigurationService
AudioService
shared UI palette
shared input model
background Service update loop
```

The architecture already reserves the external:

```text
M5Stack Unit Puzzle 8×8 WS2812E RGB LED matrix
```

for:

```text
IndicatorService
    ↓
ILEDAdapter
    ↓
PuzzleWs2812Adapter
```

Plan 033 implements that indicator foundation together with the first production feature that uses it: Pomodoro.

Pomodoro must remain fully usable when the LED matrix is absent.

---

# 1. Goal

Add a production Mini App:

```text
POMODORO
```

with a background-capable Pomodoro session:

```text
WORK       25:00
SHORT      05:00
WORK       25:00
SHORT      05:00
WORK       25:00
SHORT      05:00
WORK       25:00
LONG       15:00
→ repeat
```

Rules:

```text
4 completed Work sessions
→ Long Break

otherwise
→ Short Break
```

All phase transitions are automatic.

The next phase starts automatically.

The Pomodoro session continues when the Pomodoro Mini App is closed.

The external 8×8 LED matrix, when available, displays phase progress by starting with all 64 pixels lit and turning pixels off as the phase progresses.

Pomodoro LED output uses a fixed production brightness of:

```text
10%
```

for all normal and transition frames.

---

# 2. Core Product Principle

Pomodoro is **not** a global application mode.

The Pomodoro Mini App is a normal Mini App.

However, an active Pomodoro session has a lifecycle longer than the Pomodoro UI.

Therefore:

```text
PomodoroApp
    owns:
        UI
        input handling
        presentation

PomodoroService
    owns:
        timer
        phase
        cycle progression
        pause/resume state
        session lifecycle
```

Closing the Mini App must not stop an active session.

Conceptually:

```text
POMODORO
25:00
 ↓ Start

23:42
 ↓ Escape

HOME

PomodoroService continues running
LED continues progressing

 ↓ reopen POMODORO

18:31
```

Do not introduce:

```text
BackgroundMiniAppRuntime
BackgroundTaskManager
global Pomodoro shell mode
Pomodoro-specific ApplicationShell behavior
```

Plan 033 only needs a long-lived Service.

---

# 3. Pomodoro Durations

Production values are fixed:

```text
Work:
25 minutes

Short Break:
5 minutes

Long Break:
15 minutes
```

They are not user-configurable in Plan 033.

Keep them grouped in a configuration/domain structure so persistence or settings can be added later.

Conceptually:

```cpp
struct PomodoroDurations {
    std::chrono::milliseconds work;
    std::chrono::milliseconds shortBreak;
    std::chrono::milliseconds longBreak;
};
```

Production defaults:

```text
25 / 5 / 15
```

Tests may inject shorter durations.

Do not add a settings UI in Plan 033.

---

# 4. Pomodoro State Model

Session state:

```cpp
enum class PomodoroRunState {
    Idle,
    Running,
    Paused,
};
```

Phase:

```cpp
enum class PomodoroPhase {
    Work,
    ShortBreak,
    LongBreak,
};
```

Conceptual snapshot:

```cpp
struct PomodoroSnapshot {
    std::uint32_t generation;

    PomodoroRunState runState;
    PomodoroPhase phase;

    std::chrono::milliseconds duration;
    std::chrono::milliseconds remaining;

    std::uint8_t completedWorkSessions;
};
```

`completedWorkSessions` represents progress through the current four-work cycle.

Valid logical range:

```text
0..3
```

During the fourth Work phase, the UI may present:

```text
4 / 4
```

based on:

```text
completedWorkSessions + 1
```

The counter resets after the Long Break completes.

---

# 5. Initial State

At startup:

```text
runState = Idle
phase = Work
remaining = 25:00
completedWorkSessions = 0
```

The Pomodoro Mini App initially displays:

```text
FOCUS
25:00
1 / 4
```

No timer runs until the user starts it.

No Pomodoro LED claim exists while Idle.

---

# 6. Start / Pause / Resume

Use:

```text
Space
```

as the primary control.

Behavior:

```text
Idle + Space
→ Running

Running + Space
→ Paused

Paused + Space
→ Running
```

Pause freezes:

```text
remaining time
LED progress
phase progression
```

No phase transition may occur while paused.

---

# 7. Automatic Phase Progression

When a Work phase reaches zero:

```text
increment completed work count
```

If the completed Work was number 1, 2, or 3:

```text
Work
→ Short Break
→ automatically Running
```

If the completed Work was number 4:

```text
Work
→ Long Break
→ automatically Running
```

When a Short Break reaches zero:

```text
Short Break
→ Work
→ automatically Running
```

When a Long Break reaches zero:

```text
Long Break
→ reset cycle count
→ Work
→ automatically Running
```

No confirmation screen is shown.

No Enter press is required.

No phase waits in an Idle state between phases.

---

# 8. Elapsed-Time Correctness

Pomodoro advances from elapsed monotonic time.

Do not implement countdown by:

```text
one update call == one tick
```

Do not assume update frequency.

The Service receives:

```cpp
update(std::chrono::milliseconds elapsed)
```

and subtracts actual elapsed time from the active phase.

A delayed firmware loop must not create timer drift.

---

# 9. Oversized Elapsed Time

A single update may cross a phase boundary.

It may also cross multiple phase boundaries.

Example test configuration:

```text
Work = 10 s
Break = 5 s
```

Input:

```text
elapsed = 17 s
```

must produce:

```text
Work completed
Short Break completed
next Work has 8 s remaining
```

Do not:

```text
clamp at zero and discard remaining elapsed
produce negative time
delay the transition until another update
```

The state machine consumes the entire elapsed value.

---

# 10. Reset

Use:

```text
R
```

Reset means reset the **entire Pomodoro session**, not merely restart the current phase.

Result:

```text
runState = Idle
phase = Work
remaining = 25:00
completedWorkSessions = 0
```

The Pomodoro LED claim is released.

---

# 11. Skip

Use:

```text
Right
```

and, if convenient in the existing input model:

```text
S
```

as an alias.

Skip behaves as if the current phase completed normally.

Examples:

```text
Work #1 + Skip
→ Short Break

Short Break + Skip
→ next Work

Work #4 + Skip
→ Long Break

Long Break + Skip
→ Work #1
```

The next phase starts automatically.

Skip affects cycle accounting exactly like natural completion.

---

# 12. Escape Behavior

Escape closes the Pomodoro Mini App.

It does not:

```text
pause
reset
skip
stop
```

the Pomodoro session.

Example:

```text
Running Work 18:22
↓ Escape
Launcher/Home

Pomodoro remains Running
```

Reopening the Mini App renders the current Service snapshot.

---

# 13. Mini App Registration

Register:

```text
id: pomodoro
displayName: POMODORO
iconId: pomodoro
entryRoute: pomodoro
requiredCapabilities: none
```

The application must always be launchable.

The external LED matrix is optional and must never be a required capability.

---

# 14. Pomodoro App Ownership

Suggested files:

```text
src/apps/pomodoro/
    pomodoro_app.h
    pomodoro_app.cpp
    pomodoro_graphics.h
    pomodoro_graphics.cpp
```

`PomodoroApp` owns:

```text
input interpretation
screen rendering
presentation-only state
```

It may read:

```text
PomodoroService snapshot
```

It may invoke:

```text
start
pause
resume
reset
skip
```

It must not own:

```text
countdown state
phase progression
cycle progression
LED state
LED brightness
WS2812 mapping
hardware calls
```

---

# 15. Pomodoro Service Ownership

Suggested files:

```text
src/services/pomodoro/
    pomodoro_service.h
    pomodoro_service.cpp
```

`PomodoroService` owns:

```text
RunState
Phase
remaining duration
cycle count
phase transitions
generation
```

It must not depend on:

```text
Display
PomodoroApp
ApplicationShell
IndicatorService
ILEDAdapter
WS2812
Audio hardware
keyboard
```

Conceptually:

```text
app_main
   ↓ every firmware loop
PomodoroService::update(elapsed)
```

The Service runs independently of which Mini App is active.

---

# 16. Snapshot Generation

Expose a monotonically increasing:

```text
generation
```

Increase generation whenever semantically observable Pomodoro state changes.

Examples:

```text
start
pause
resume
reset
skip
phase transition
remaining time changes sufficiently to affect presentation
```

Consumers may use generation to avoid unnecessary recomputation.

Do not require LCD redraw on every firmware loop.

---

# 17. LCD Design

Pomodoro is a timer-first direct-manipulation screen.

Suggested resting layout:

```text
┌──────────────────────────────┐
│ FOCUS                  1 / 4 │
│                              │
│            24:37             │
│                              │
│  ████████████████······      │
│                              │
│         SPACE  PAUSE         │
└──────────────────────────────┘
```

Break:

```text
┌──────────────────────────────┐
│ SHORT BREAK            2 / 4 │
│                              │
│            04:38             │
│                              │
│  █████████████·········      │
│                              │
│         SPACE  PAUSE         │
└──────────────────────────────┘
```

Long Break:

```text
LONG BREAK
14:51
```

Paused:

```text
FOCUS
18:42

PAUSED
```

The LCD communicates all essential state independently of LED color.

---

# 18. LCD Progress

Display a discrete progress meter derived from:

```text
remaining / duration
```

It does not need 64 segments.

The LCD meter is presentation-only.

The LED and LCD may use different segment counts while deriving progress from the same Pomodoro snapshot.

---

# 19. Typography and Visual Language

Follow the existing Hub UI language.

Use:

```text
Bone
Ink
Blue
Leaf
Pale
Ordinal
```

Prominent timer digits should use Micro 5 where practical.

Small labels use the existing compact M5 bitmap font.

Prefer:

```text
full-bleed surface
square geometry
1 px separators
minimal chrome
```

Do not add:

```text
rounded cards
gradients
decorative shadows
large permanent footer
```

---

# 20. Pomodoro LED Colors

Pomodoro uses exactly two semantic LED colors:

```text
WORK
→ Blue

SHORT BREAK
→ Leaf

LONG BREAK
→ Leaf
```

These colors follow the existing Cardputer Hub palette.

Do not introduce a separate decorative color for Long Break.

Short and Long Break are distinguished through:

```text
phase semantics
LCD label
cycle state
duration
```

not through LED color.

Do not use:

```text
Vermilion
```

for normal Pomodoro progression.

Vermilion remains reserved for error/action-required semantics.

---

# 21. Pomodoro LED Brightness

All Pomodoro LED output uses a fixed production brightness of:

```text
10%
```

This includes:

```text
normal Work progress frames
normal Short Break progress frames
normal Long Break progress frames
phase-transition frames
paused frames
restored frames after arbitration
```

There must be **no temporary brightness boost** during transitions.

For example:

```text
Work progress
→ Blue @ 10%

Work → Short Break transition
→ Leaf @ 10%

Short Break progress
→ Leaf @ 10%
```

Pomodoro must never request:

```text
20%
50%
100%
```

or any other brightness value.

---

# 22. Brightness Ownership

Brightness does not belong to `PomodoroService` or `PomodoroApp`.

Pomodoro provides only:

```text
semantic color
logical frame / lit pixels
```

Brightness policy is applied at the:

```text
IndicatorService / LED adapter boundary
```

Conceptually:

```text
PomodoroLedController
    ↓
Blue frame / Leaf frame
    ↓
IndicatorService
    ↓
Pomodoro brightness policy = 10%
    ↓
ILEDAdapter
```

This prevents feature code from directly driving raw LED intensity.

---

# 23. Pomodoro Brightness Invariant

For every Pomodoro-owned indicator frame:

```text
effective brightness <= 10%
```

The normal expected value is exactly:

```text
10%
```

The implementation may use an internal integer representation appropriate for the LED driver, but that conversion must remain inside the indicator/hardware layer.

If an adapter uses an integer scale such as:

```text
0..255
```

the Pomodoro feature must not know or depend on that scale.

---

# 24. Indicator Foundation

Plan 033 implements the first production `IndicatorService`.

Suggested files:

```text
src/services/indicator/
    indicator_service.h
    indicator_service.cpp
    led_adapter.h
```

Hardware implementation:

```text
src/hardware/cardputer/
    cardputer_puzzle_ws2812_adapter.h
    cardputer_puzzle_ws2812_adapter.cpp
```

Dependency:

```text
Service / Mini App
        ↓
IndicatorService
        ↓
ILEDAdapter
        ↓
PuzzleWs2812Adapter
```

No Service or Mini App other than `IndicatorService` may manipulate WS2812 hardware directly.

---

# 25. LED Arbitration

IndicatorService supports multiple concurrent logical users.

Do not implement the LED as one mutable global frame where the last writer wins.

Use claims.

Conceptually:

```cpp
struct IndicatorClaim {
    IndicatorOwner owner;
    IndicatorPriority priority;
    IndicatorFrame frame;
};
```

Multiple claims may exist simultaneously.

Only the highest-priority active claim is rendered.

---

# 26. Indicator Priority Model

Use:

```text
CRITICAL
WARNING
NOTIFICATION
FOREGROUND_APPLICATION
BACKGROUND_APPLICATION
CONNECTION
IDLE
```

Order is strict from highest to lowest.

Pomodoro uses:

```text
BACKGROUND_APPLICATION
```

A foreground Mini App that actively owns the matrix uses:

```text
FOREGROUND_APPLICATION
```

Notifications may temporarily override both.

---

# 27. Indicator Claim Lifecycle

Prefer a scoped/token-style API.

Conceptually:

```cpp
auto claim = indicator.acquire(
    "pomodoro",
    IndicatorPriority::BackgroundApplication
);

claim.setFrame(frame);

claim.release();
```

Equivalent explicit handles are acceptable if RAII is impractical.

Invariant:

```text
one owner may only update/release its own claim
```

Do not expose:

```text
indicator.clear()
```

as a generic operation capable of deleting unrelated claims.

---

# 28. Claim Arbitration

Example:

```text
Pomodoro running

claim:
    BACKGROUND_APPLICATION
    Blue
    37 / 64
    brightness 10%
```

Foreground LED app opens:

```text
claim:
    FOREGROUND_APPLICATION
```

IndicatorService renders the foreground claim.

Pomodoro continues counting and may continue updating its hidden claim.

When the foreground app closes:

```text
foreground claim released
```

IndicatorService selects Pomodoro again.

The matrix displays the **current** Pomodoro state at **10% brightness**.

Do not restore an old frozen frame.

---

# 29. Notifications During Pomodoro

Example:

```text
Pomodoro
BACKGROUND_APPLICATION

LED Control
FOREGROUND_APPLICATION

Telegram notification
NOTIFICATION
```

Visible priority:

```text
Telegram notification
```

After notification release:

```text
LED Control
```

After LED Control release:

```text
Pomodoro
```

When Pomodoro becomes visible again, its effective brightness remains:

```text
10%
```

regardless of what brightness the previous higher-priority owner used.

---

# 30. Pomodoro LED Controller

Add:

```text
src/services/pomodoro/
    pomodoro_led_controller.h
    pomodoro_led_controller.cpp
```

Dependency:

```text
PomodoroService snapshot
        ↓
PomodoroLedController
        ↓
IndicatorService
```

The controller owns:

```text
Pomodoro indicator claim lifecycle
progress → 64-pixel frame conversion
Pomodoro semantic phase color
phase-transition presentation
```

It does **not** own brightness.

It does not know raw LED intensity values.

---

# 31. Pomodoro LED Claim

When:

```text
Pomodoro == Idle
```

there is no Pomodoro claim.

When:

```text
Pomodoro == Running
or
Pomodoro == Paused
```

Pomodoro holds:

```text
BACKGROUND_APPLICATION
```

claim.

Reset to Idle:

```text
release claim
```

Pause:

```text
keep claim
keep current progress frame
```

Resume:

```text
continue updating claim
```

Whenever the claim is actually rendered, Pomodoro brightness is:

```text
10%
```

---

# 32. 8×8 Progress Model

Each phase starts with:

```text
64 lit pixels
```

and ends with:

```text
0 lit pixels
```

The number of lit pixels is derived directly from remaining time.

Conceptually:

```cpp
litPixels =
    ceil(remainingMs * 64 / durationMs);
```

Clamp to:

```text
0..64
```

Examples:

```text
100% remaining → 64
50% remaining  → 32
1% remaining   → at least 1
0 remaining    → 0
```

All lit pixels use the current phase's semantic color at 10% effective brightness.

---

# 33. No LED Timer Drift

Do not schedule:

```text
turn off one pixel every N milliseconds
```

Instead:

```text
PomodoroService owns time

PomodoroLedController reads remaining / duration

litPixels = derived value
```

Therefore:

```text
pause
loop delay
temporary higher-priority claim
foreground takeover
```

cannot desynchronize LED progress.

---

# 34. Pixel Order

The logical 8×8 frame uses:

```text
00 01 02 03 04 05 06 07
08 09 10 11 12 13 14 15
16 17 18 19 20 21 22 23
...
56 57 58 59 60 61 62 63
```

Pomodoro progression removes pixels:

```text
left-to-right
top-to-bottom
```

Do not expose physical WS2812 wiring order to Pomodoro.

---

# 35. Physical Matrix Mapping

`PuzzleWs2812Adapter` owns conversion from logical coordinates/indexes to physical LED order.

If the matrix uses serpentine wiring, that mapping remains entirely inside the adapter.

Neither:

```text
PomodoroLedController
IndicatorService
PomodoroApp
```

may know about physical wiring.

---

# 36. Optional Hardware Behavior

Pomodoro does not require the LED matrix.

When the matrix is absent:

```text
LCD timer works
session works
pause/resume works
phase transitions work
audio works
no crash
no blocking error
```

Do not add:

```text
requiredCapabilities = LED
```

to the AppDescriptor.

---

# 37. LED Hardware Availability

WS2812-style output does not provide a reliable application-level presence handshake.

Plan 033 must not depend on automatic physical presence detection.

A configured adapter may emit frames whether or not the matrix is physically attached.

Failure to see physical LEDs does not affect Pomodoro state.

---

# 38. General Indicator Brightness vs Pomodoro Brightness

The indicator infrastructure may later support different brightness policies for other consumers.

For example:

```text
Notification
Foreground LED app
Connection indicator
```

may eventually have their own allowed brightness behavior.

Plan 033 does **not** define those future policies.

It only defines the Pomodoro invariant:

```text
Pomodoro visible frame
→ exactly 10% production brightness
```

Therefore the implementation must not globally hardcode the entire IndicatorService to 10% merely because Pomodoro currently uses 10%.

The design should allow:

```text
claim / owner policy
→ effective brightness
```

while ensuring Pomodoro cannot exceed its fixed 10% policy.

---

# 39. Rendering Efficiency

Pomodoro LED output must not rewrite the physical matrix every firmware loop.

Update the Pomodoro claim when a visible logical frame changes:

```text
lit pixel count changed
phase changed
claim acquired
claim restored after arbitration if needed
```

IndicatorService should avoid redundant hardware writes when the resolved:

```text
frame
color
brightness
```

are unchanged.

---

# 40. Phase Transition LED Feedback

When a phase completes, briefly display a full 64-pixel frame in the color of the new phase.

Target duration:

```text
~300–500 ms
```

Examples:

```text
Work → Short Break
→ 64 × Leaf @ 10%

Short Break → Work
→ 64 × Blue @ 10%

Work #4 → Long Break
→ 64 × Leaf @ 10%

Long Break → Work
→ 64 × Blue @ 10%
```

There is **no brightness flash**.

Transition feedback must remain at:

```text
10%
```

for its entire duration.

After transition feedback, normal countdown begins.

The transition must be non-blocking.

Do not use:

```text
delay(...)
sleep(...)
blocking loops
```

---

# 41. Arbitration During Transition Feedback

Transition feedback remains part of the Pomodoro:

```text
BACKGROUND_APPLICATION
```

claim.

It does not gain notification priority.

Therefore:

```text
FOREGROUND_APPLICATION
NOTIFICATION
WARNING
CRITICAL
```

may override it normally.

Pomodoro never steals the matrix from a higher-priority owner.

---

# 42. Audio Feedback

Use the existing `AudioService`.

Emit a short semantic audio cue when:

```text
Work → Break
Break → Work
```

The cue still occurs if the Pomodoro Mini App is closed.

Audio feedback does not require LED hardware.

Do not put audio hardware calls in `PomodoroService`.

---

# 43. Background Behavior

The following continue outside the Pomodoro Mini App:

```text
PomodoroService countdown
automatic phase changes
cycle count
Pomodoro LED claim updates
phase-change sound
```

The following do not:

```text
Pomodoro LCD rendering
Pomodoro-specific input handling
```

---

# 44. Main Loop Composition

Conceptually:

```text
runtime.update(elapsed)

hosts.update(elapsed)
companion.update(elapsed)
hostControl.update()
network.update(elapsed)
battery.update(elapsed)

pomodoro.update(elapsed)
pomodoroLed.update(...)
indicator.update(...)

applicationShell.update(...)
```

Ordering should ensure:

```text
PomodoroService computes latest state
↓
PomodoroLedController derives latest claim
↓
IndicatorService resolves visible owner + brightness policy
↓
hardware receives final frame
```

No Mini App owns Service update calls.

---

# 45. Ownership & Boundaries

| Component | Owns | May call/read | Must not call |
| --- | --- | --- | --- |
| `PomodoroService` | timer, phase, cycle | monotonic elapsed | Display, Indicator, hardware |
| `PomodoroApp` | UI/input | PomodoroService API/snapshot, Display | WS2812 adapter |
| `PomodoroLedController` | Pomodoro claim/frame | Pomodoro snapshot, IndicatorService | raw LED hardware/intensity |
| `IndicatorService` | claims, priority, brightness policy, resolved frame | `ILEDAdapter` | Mini App internals |
| `PuzzleWs2812Adapter` | physical mapping/output | WS2812 driver | PomodoroService |
| `app_main` | composition/update lifecycle | Services/controllers | feature business logic |

---

# 46. Architecture Invariants

```text
PomodoroApp can be closed without stopping an active session.

PomodoroService never depends on PomodoroApp.

PomodoroService never depends on IndicatorService.

PomodoroService never accesses LED hardware.

PomodoroApp never accesses LED hardware.

PomodoroLedController does not know raw hardware brightness scales.

Pomodoro is launchable without the external matrix.

Only IndicatorService writes frames through ILEDAdapter.

Pomodoro uses BACKGROUND_APPLICATION priority.

Pomodoro Work uses Blue.

Pomodoro Short Break uses Leaf.

Pomodoro Long Break uses Leaf.

Every visible Pomodoro LED frame uses 10% brightness.

Pomodoro transition frames also use 10% brightness.

Pomodoro never temporarily boosts brightness.

A foreground claim overrides Pomodoro.

A notification claim overrides foreground application claims.

Releasing a higher-priority claim automatically reveals the latest lower-priority frame.

Restored Pomodoro output is rendered at 10% regardless of the previous owner's brightness.

LED progress is derived from Pomodoro time; it is not an independent timer.

MiniAppRuntime requires no Pomodoro-specific behavior.
```

---

# 47. BDD — Start Session

### Scenario: Start the first Work session

Given:

```text
Pomodoro is Idle
phase = Work
remaining = 25:00
completedWorkSessions = 0
```

When:

```text
user opens POMODORO
user presses Space
```

Then:

```text
runState becomes Running
Work countdown starts
screen shows FOCUS
screen shows 1 / 4
Pomodoro acquires BACKGROUND_APPLICATION LED claim
LED contains 64 Blue pixels
effective LED brightness is 10%
```

---

# 48. BDD — Close Mini App

### Scenario: Pomodoro continues after Escape

Given:

```text
Pomodoro Work is Running
remaining = 20:00
POMODORO Mini App is active
```

When:

```text
user presses Escape
5 minutes elapse
```

Then:

```text
POMODORO Mini App is no longer active
PomodoroService remains Running
remaining becomes approximately 15:00
LED progress continues
LED remains Blue @ 10%
```

---

# 49. BDD — Short Break

### Scenario: Normal Work completion

When Work completes:

```text
completed Work count increments
phase becomes ShortBreak
remaining becomes 05:00
runState remains Running
next phase starts automatically
LED becomes Leaf
effective brightness remains 10%
audio cue occurs once
```

No brightness increase occurs during the phase transition.

---

# 50. BDD — Long Break

### Scenario: Fourth Work completes

Given three Work sessions are already completed.

When the fourth Work reaches zero:

```text
phase becomes LongBreak
remaining becomes 15:00
runState remains Running
LED becomes Leaf
brightness remains 10%
```

No Short Break is inserted.

---

# 51. BDD — Long Break Completion

When Long Break reaches zero:

```text
completedWorkSessions resets to 0
phase becomes Work
remaining becomes 25:00
runState remains Running
LED becomes Blue
brightness remains 10%
```

---

# 52. BDD — Pause

Given Work is Running.

When the user presses Space:

```text
runState becomes Paused
remaining stops changing
LED progress stops changing
LED remains Blue @ 10%
screen displays PAUSED
```

Break pause behaves the same with:

```text
Leaf @ 10%
```

---

# 53. BDD — Reset

When Reset is invoked:

```text
runState = Idle
phase = Work
remaining = 25:00
completedWorkSessions = 0
Pomodoro LED claim is released
```

No Pomodoro frame remains visible unless another owner provides one.

---

# 54. BDD — Foreground LED Override

Given:

```text
Pomodoro Work is Running
Pomodoro owns BACKGROUND_APPLICATION
LED shows Blue @ 10%
```

When a foreground application acquires a higher-priority claim:

```text
foreground frame is rendered
Pomodoro continues counting
Pomodoro claim remains registered
```

When foreground releases:

```text
latest Pomodoro frame returns
color = Blue
brightness = 10%
```

Do not inherit the foreground application's brightness.

---

# 55. BDD — Notification Override

When a notification temporarily overrides Pomodoro:

```text
notification policy controls visible output
Pomodoro continues underneath
```

When the notification releases:

```text
Pomodoro returns if it is now highest priority
current progress is rendered
Pomodoro brightness is restored to 10%
```

---

# 56. BDD — No Physical Matrix

Without Unit Puzzle attached:

```text
Pomodoro timer works
LCD works
pause/resume works
phase transitions work
audio works
no crash occurs
```

---

# 57. Tests — PomodoroService

Test:

```text
initial state is Idle / Work / 25:00

start changes Idle → Running

pause changes Running → Paused

resume changes Paused → Running

paused elapsed does not modify remaining

reset returns complete session to initial state

natural Work completion selects Short Break

fourth Work selects Long Break

Short Break completion selects Work

Long Break completion resets cycle count

skip follows completion semantics

large elapsed crosses one boundary correctly

large elapsed crosses multiple boundaries correctly

remaining never becomes negative

update cadence does not change total elapsed behavior
```

---

# 58. Tests — PomodoroApp

Test:

```text
registered in AppRegistry

requires no capabilities

activates without LED

initial view shows FOCUS / 25:00 / 1 of 4

Space starts

Space pauses/resumes

R resets

Skip advances phase

Escape deactivates Mini App but not Service

reopening renders current Service snapshot

Paused shows PAUSED

Long Break label is distinct from Short Break
```

---

# 59. Tests — LED Progress

Verify:

```text
phase start → 64 pixels

approximately half remaining → 32 pixels

zero remaining → 0 pixels

lit pixel count is monotonic within one phase

pause freezes pixel count

resume continues

Work uses Blue

Short Break uses Leaf

Long Break uses Leaf

Work visible brightness = 10%

Short Break visible brightness = 10%

Long Break visible brightness = 10%

transition frame brightness = 10%

no transition temporarily exceeds 10%

new phase starts with full matrix

same visible frame/color/brightness is not redundantly written
```

---

# 60. Tests — Indicator Arbitration

Test:

```text
single claim becomes visible

higher priority replaces lower priority

hidden lower-priority claim may update

releasing higher priority reveals latest lower-priority frame

NOTIFICATION overrides FOREGROUND_APPLICATION

FOREGROUND_APPLICATION overrides BACKGROUND_APPLICATION

BACKGROUND_APPLICATION overrides CONNECTION

owner may update its own claim

owner may release its own claim

one consumer cannot clear unrelated claims

restored Pomodoro claim resolves to 10% brightness

previous owner's brightness never leaks into restored Pomodoro

unchanged frame/color/brightness does not cause redundant adapter write
```

---

# 61. Tests — Brightness Boundary

Test Pomodoro brightness policy independently of raw driver representation.

Verify:

```text
Pomodoro request cannot exceed 10%

Work resolves to 10%

Short Break resolves to 10%

Long Break resolves to 10%

transition resolves to 10%

pause remains 10%

arbitration restore remains 10%
```

If the hardware driver uses another numeric range, test the conversion at the adapter boundary.

Do not expose that numeric range to Pomodoro tests.

---

# 62. Tests — Physical Mapping

Verify:

```text
64 logical pixels map exactly once

no duplicate physical indices

all physical indices are 0..63

row orientation matches Unit Puzzle wiring

logical top-left remains stable regardless of serpentine order
```

---

# 63. Physical Acceptance

Use injectable development durations such as:

```text
Work = 64 s
Short Break = 16 s
Long Break = 32 s
```

Verify physically:

```text
1. Start Work.
2. Matrix begins fully Blue.
3. Confirm brightness is the configured Pomodoro 10% level.
4. Confirm there is no full-brightness flash at start.
5. With 64-second Work, approximately one pixel disappears per second.
6. Pause.
7. Matrix stops changing and remains Blue @ 10%.
8. Resume.
9. Countdown continues.
10. Exit Pomodoro to Home.
11. Matrix continues progressing.
12. Reopen Pomodoro.
13. LCD matches actual remaining time.
14. Complete Work → Short Break.
15. Matrix becomes Leaf @ 10%.
16. Confirm transition never becomes brighter than normal Pomodoro output.
17. Verify automatic Break → Work.
18. Work returns to Blue @ 10%.
19. Complete four Work phases.
20. Verify fourth Work → Long Break.
21. Long Break uses Leaf @ 10%.
22. Verify Long Break → first Work of new cycle.
23. Activate a higher-priority test LED claim.
24. Verify Pomodoro is hidden but continues progressing.
25. Release higher-priority claim.
26. Verify current Pomodoro frame returns at exactly its normal 10% brightness.
27. Disconnect Unit Puzzle.
28. Reboot.
29. Verify Pomodoro remains fully usable from LCD alone.
```

---

# 64. Suggested File Layout

```text
src/apps/pomodoro/
    pomodoro_app.h
    pomodoro_app.cpp
    pomodoro_graphics.h
    pomodoro_graphics.cpp

src/services/pomodoro/
    pomodoro_service.h
    pomodoro_service.cpp
    pomodoro_led_controller.h
    pomodoro_led_controller.cpp

src/services/indicator/
    indicator_service.h
    indicator_service.cpp
    led_adapter.h

src/hardware/cardputer/
    cardputer_puzzle_ws2812_adapter.h
    cardputer_puzzle_ws2812_adapter.cpp
```

---

# 65. Documentation Updates

Update:

```text
docs/ARCHITECTURE.md
```

to document:

```text
implemented IndicatorService
claim arbitration
foreground/background priorities
brightness ownership boundary
Pomodoro fixed 10% indicator policy
```

Update `docs/UI_REQUIREMENTS.md` only with reusable rules.

Update `README.md` if necessary to mark Unit Puzzle as production-supported hardware.

---

# 66. Out of Scope

Do not add:

```text
editable Pomodoro durations

Pomodoro Settings screen

editable Pomodoro LED brightness

Pomodoro color customization

different LED color for Long Break

temporary bright transition flashes

persisted active timer across reboot

daily statistics

history

streaks

tasks / todo integration

Companion Pomodoro control

phone notifications

Telegram Pomodoro control

remote Pomodoro API

Home countdown widget

global Pomodoro keyboard shortcuts

generic background Mini App runtime

general LED brightness settings UI

custom LED animation editor

multiple external matrices

automatic WS2812 presence detection
```

---

# 67. Completion Criteria

Plan 033 is complete when:

```text
Pomodoro Mini App is registered and usable

25/5/15 cycle works

every fourth completed Work selects Long Break

all phase transitions auto-start

timer continues outside Pomodoro Mini App

pause/resume/reset/skip work

timer behavior is elapsed-time correct

IndicatorService supports prioritized claims

Pomodoro uses BACKGROUND_APPLICATION claim

higher-priority LED users override Pomodoro safely

Pomodoro resumes visually with current progress after override

Work LED color is Blue

Short Break LED color is Leaf

Long Break LED color is Leaf

every Pomodoro LED frame uses 10% brightness

every Pomodoro transition frame uses 10% brightness

Pomodoro never performs a brightness flash above 10%

brightness of another claim does not leak into Pomodoro after arbitration

8×8 progress derives from remaining time

LED matrix is optional

Pomodoro works without LED attached

no production code outside IndicatorService writes LED hardware

host/native tests pass

firmware build passes

architecture checks pass

physical acceptance passes on Cardputer-Adv + Unit Puzzle
```

---

# 68. Architectural Summary

```text
                     ┌─────────────────┐
                     │  PomodoroApp    │
                     │   LCD + input   │
                     └────────┬────────┘
                              │
                              ▼
                     ┌─────────────────┐
                     │ PomodoroService │
                     │ timer + cycle   │
                     └────────┬────────┘
                              │ snapshot
                              ▼
                ┌─────────────────────────┐
                │ PomodoroLedController   │
                │ Blue / Leaf + progress  │
                └────────────┬────────────┘
                             │
                             │ BACKGROUND_APPLICATION
                             ▼
┌───────────────┐   ┌───────────────────────────┐
│ Notifications │──▶│     IndicatorService      │
└───────────────┘   │                           │
                    │ priority arbitration      │
┌───────────────┐   │ brightness policy        │
│ Foreground App│──▶│                           │
└───────────────┘   │ Pomodoro → fixed 3%       │
                    └─────────────┬─────────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │  ILEDAdapter    │
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ Puzzle WS2812   │
                         │      8 × 8      │
                         └─────────────────┘
```

The key architectural rules are:

> A Pomodoro session may outlive the Pomodoro Mini App, but Pomodoro does not become a global application mode.

> LED access is arbitrated through IndicatorService, so background Pomodoro progress never owns the physical matrix exclusively.

> Pomodoro specifies semantic color and progress, while IndicatorService owns effective LED brightness. Every Pomodoro frame — including phase transitions — is rendered at a fixed 3% production brightness.

---

# Addendum — Pomodoro Phase Transition Display Wake

Status: **Software implemented; physical Cardputer-Adv acceptance pending**

## Goal

On every Pomodoro phase transition, play the existing phase-change sound and
request programmatic display wake. Do not keep the display permanently awake
while Pomodoro runs. Do not steal navigation focus.

```text
Pomodoro phase finishes
→ existing audio cue
→ DisplayPowerController::requestWake()
→ whatever UI is already active becomes visible
```

## Ownership

```text
PomodoroService
        │
        ▼
PomodoroLedController
   /        |         \
  ▼         ▼          ▼
Indicator  Audio   DisplayPowerController
Service    Service
```

`takeTransitions` remains consumptive and has one consumer. Display wake is
one `requestWake()` per controller update, independent of LED success and of
whether `AudioService::play()` starts immediately. `requestWake()` is not
physical input: it synthesizes no `InputEvent` and does not participate in
wake-only key consumption.

`DisplayPowerController` stays Pomodoro-agnostic. `SystemRuntime` gains no
Pomodoro dependency.

## `requestWake()` states

| Current | Effect |
| --- | --- |
| Awake | remain Awake; reset idle timer |
| Dimming / Dimmed / TurningOff / Off | enter Waking from current brightness |
| Waking | continue the existing ramp; do not restart or extend it |

After wake completes, the normal 15 s idle → dim → off policy resumes.