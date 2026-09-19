# 032 — Display Idle Dimming, Off & Wake Consumption

Status: **Software implemented; physical Cardputer ADV acceptance pending**

Suggested branch:

```text
feat/032-display-power-and-wake
```

## 1. Goal

Implement the shared display idle-power policy already defined by the UI architecture:

```text
AWAKE
  |
  | 15 s without physical input
  v
DIMMING
  |
  | 300 ms
  v
DIMMED
  |
  | full 2 min hold
  v
TURNING_OFF
  |
  | 400 ms
  v
OFF
```

Any intentional physical key press while the display is not fully `Awake` must wake the display and be consumed before normal input routing.

Wake behavior:

```text
DIMMING
DIMMED
TURNING_OFF
OFF
WAKING
   |
   | physical key press
   v
WAKING
   |
   | 200 ms
   v
AWAKE
```

The display-power policy is global and independent of the currently visible System UI screen or Mini App.

Plan 032 does **not** put the ESP32 into light sleep or deep sleep. Only LCD backlight power is managed.

Connectivity, Services, Companion communication, timers, Mini App background services, and keyboard polling remain active while the screen is dark.

---

## 2. Scope

Plan 032 delivers:

- global idle detection based on physical keyboard activity;
- a hardware-neutral backlight abstraction;
- Cardputer ADV backlight implementation;
- deterministic display-power state machine;
- non-blocking brightness ramps;
- 15-second idle threshold;
- dimming to 10% of the normal backlight level;
- a full 2-minute hold at the dimmed level;
- final fade to backlight level zero;
- wake from any intentional physical key press;
- consumption of all wake-only input before normal input routing;
- modifier-only wake support, including `Fn`;
- no key-click sound for wake-only input;
- no Action, navigation, text-entry, confirmation, Companion, or HID side effect from wake-only input;
- Home ambient-wave pause while the display is Off;
- host-side tests for timing, state transitions, input consumption, and ownership boundaries;
- physical Cardputer ADV validation;
- architecture and UI requirement documentation updates.

The policy becomes active only after startup splash handoff. The 15-second idle timer therefore begins from the normal application UI, not from firmware boot.

### Fixed Plan 032 timings

| Parameter | Value |
| --- | ---: |
| Idle before dim | 15 s |
| Dim target | 10% of normal brightness |
| Awake → Dim ramp | 300 ms |
| Dimmed hold | 120 s |
| Dim → Off ramp | 400 ms |
| Wake ramp | 200 ms |

The 120-second dim hold starts **after the 300 ms dim ramp has completed**.

Therefore the nominal no-input sequence is:

```text
Home becomes active
        |
        | 15.000 s
        v
start dim
        |
        | 0.300 s
        v
10% brightness
        |
        | 120.000 s
        v
start off fade
        |
        | 0.400 s
        v
backlight 0
```

Total time from policy activation to fully Off:

```text
135.7 seconds
```

---

## 3. Architecture

### 3.1 High-level flow

```text
TCA8418 keyboard
      |
      v
CardputerKeyboardAdapter
      |
      | InputEvents
      | physicalPress
      v
SystemRuntime
      |
      +-----------> DisplayPowerController
      |                  |
      |                  v
      |           IBacklightAdapter
      |                  |
      |                  v
      |        CardputerBacklightAdapter
      |                  |
      |                  v
      |             M5.Display
      |
      | filtered InputEvents
      v
ApplicationShell
      |
      +--> System UI
      +--> Mini Apps
      +--> ActionBus
      +--> key feedback audio
      +--> Host / Companion control
```

Wake consumption happens before `ApplicationShell`.

A wake-only key therefore cannot accidentally become:

```text
navigation
text input
Enter confirmation
Escape
Launcher activation
MAC CONTROL slot activation
ActionBus dispatch
BLE HID command
Companion command
key-click sound
```

### 3.2 New backlight abstraction

Add a hardware-neutral backlight adapter under System Core:

```text
src/core/power/
    battery_adapter.h
    backlight_adapter.h
    display_power_controller.h
    display_power_controller.cpp
```

Suggested interface shape:

```cpp
class IBacklightAdapter {
  public:
    virtual ~IBacklightAdapter() = default;

    virtual std::uint8_t level() const = 0;
    virtual void setLevel(std::uint8_t level) = 0;
};
```

The normalized level is:

```text
0   = off
255 = maximum adapter level
```

No UI code calls this adapter directly.

### 3.3 Cardputer implementation

Add:

```text
src/hardware/cardputer/
    cardputer_backlight_adapter.h
    cardputer_backlight_adapter.cpp
```

`CardputerBacklightAdapter` is the only new component that knows the M5Unified backlight API.

It translates the hardware-neutral 8-bit level to the Cardputer display implementation.

### 3.4 Normal brightness

Plan 032 does not add a Settings brightness control.

After platform/display initialization, `DisplayPowerController` captures the current non-zero backlight level as its normal level.

This preserves the existing firmware brightness instead of silently changing the device to maximum brightness.

The controller should expose a clean future boundary for changing normal brightness later without redesigning the idle policy.

Conceptually:

```text
normalLevel = existing configured/current backlight level
dimLevel    = 10% of normalLevel
offLevel    = 0
```

For a non-zero normal level, integer quantization must keep the dim target non-zero.

Example:

```text
normal = 128

dim = approximately 13
off = 0
```

### 3.5 Brightness ramps

Brightness changes are:

- monotonic;
- elapsed-time driven;
- non-blocking;
- deterministic;
- independent of UI frame rate.

No `delay()` or sleep is allowed.

A simple linear ramp is sufficient.

Example:

```text
normal -----------------> dim
        300 ms

dim --------------------> 0
        400 ms

current ----------------> normal
        200 ms
```

If wake happens during an active fade, wake begins from the **current actual brightness**, not from a fixed endpoint.

Example:

```text
100%
 |
 | dimming
 v
55%
 |
 | user presses key
 v
55% -----------> 100%
        200 ms
```

There must be no jump:

```text
55% -> 10% -> 100%   WRONG
55% -------> 100%    CORRECT
```

### 3.6 Physical activity versus semantic input

The current keyboard pipeline produces semantic `InputEvents`, but that is not sufficient for idle detection.

A modifier such as `Fn` may be a real physical press without producing an application input event.

Extend the keyboard polling boundary so one poll reports both:

```text
semantic InputEvents
physical press activity
```

Suggested shape:

```cpp
struct KeyboardPollResult {
    bool physicalPress = false;
};
```

with:

```cpp
KeyboardPollResult poll(InputEvents& events);
```

`CardputerKeyboardAdapter` sets `physicalPress = true` for any newly decoded physical **press edge**, including modifier keys.

A release edge does not count as new user activity.

A continuously held key does not repeatedly reset the idle timer.

### 3.7 Input wake gate

`SystemRuntime` remains the shared pre-application input boundary.

After keyboard polling:

```text
keyboard poll
    |
    v
DisplayPowerController update
    |
    +-- Awake?
    |      |
    |      +--> reset idle timer
    |      +--> keep InputEvents
    |
    +-- not Awake?
           |
           +--> start/continue wake
           +--> clear InputEvents
```

This ensures every screen and Mini App gets identical behavior.

`ApplicationShell` must never receive the semantic events associated with a wake-only press.

### 3.8 Background operation while Off

`Off` means:

```text
LCD backlight = 0
```

It does **not** mean:

```text
ESP sleep
Wi-Fi off
BLE off
Companion off
Services suspended
keyboard polling suspended
Mini App services suspended
```

Normal firmware loop ownership remains unchanged.

The screen framebuffer may continue to receive state-driven updates while the backlight is zero. This prevents wake from revealing stale Wi-Fi, BLE, battery, host, or Companion state.

The Home ambient wave is the exception: its phase must stop advancing while display state is `Off`.

After wake it continues from the previous phase rather than jumping forward by the time spent Off.

Normal application timers and Service state continue to advance.

---

## 4. Ownership & Boundaries

| Component | Owns | May call | Must not call |
| --- | --- | --- | --- |
| `DisplayPowerController` | idle timer, display-power state, brightness ramps, wake decision | `IBacklightAdapter` | ApplicationShell, Actions, Services, Companion, concrete M5 APIs |
| `IBacklightAdapter` | hardware-neutral backlight boundary | none | UI, Actions, Services |
| `CardputerBacklightAdapter` | physical Cardputer backlight level | M5Unified display API | input routing, Actions, UI policy |
| `CardputerKeyboardAdapter` | physical key edges and semantic keyboard translation | keyboard controller, translator | display-power policy, UI navigation |
| `SystemRuntime` | common platform/input update boundary and wake-input consumption | keyboard adapter, `DisplayPowerController` | Application Actions, Mini App behavior |
| `ApplicationShell` | normal UI routing and presentation | Actions, UI components, Mini Apps | deciding whether a physical key is wake-only |
| Home presentation | Home rendering and ambient-wave phase | read-only display-power state/snapshot | changing brightness or resetting idle policy |
| `app_main` | construction and normal runtime lifecycle | normal component updates | implementing timing/state-machine policy inline |

### Critical ownership rule

Wake consumption belongs above application routing:

```text
physical keyboard
    ↓
display-power gate
    ↓
ApplicationShell
```

and never:

```text
physical keyboard
    ↓
ApplicationShell
    ↓
individual screens decide whether to wake
```

Mini Apps must not know how to consume wake input.

---

## 5. State Model

Use explicit runtime states:

```cpp
enum class DisplayPowerState {
    Awake,
    Dimming,
    Dimmed,
    TurningOff,
    Off,
    Waking,
};
```

### `Awake`

Backlight target:

```text
normalLevel
```

Behavior:

- idle elapsed time advances;
- physical press resets idle elapsed to zero;
- semantic input routes normally;
- after exactly 15 seconds without a physical press, transition to `Dimming`.

### `Dimming`

Ramp:

```text
current normalLevel -> dimLevel
duration: 300 ms
```

Behavior:

- any physical press starts `Waking`;
- the associated semantic input batch is consumed;
- wake begins from the current brightness;
- after the ramp reaches `dimLevel`, transition to `Dimmed`;
- dim-hold timer starts at zero only at that point.

### `Dimmed`

Backlight:

```text
dimLevel = 10% normalLevel
```

Behavior:

- remain here for a full 120 seconds;
- any physical press starts `Waking`;
- the associated input is consumed;
- after the full hold, transition to `TurningOff`.

### `TurningOff`

Ramp:

```text
dimLevel -> 0
duration: 400 ms
```

Behavior:

- any physical press reverses toward `normalLevel`;
- wake begins from the current actual level;
- the associated input is consumed;
- after reaching zero, transition to `Off`.

### `Off`

Backlight:

```text
0
```

Behavior:

- Connectivity and Services continue;
- keyboard polling continues;
- semantic application input is not generated from the wake press;
- first physical press transitions to `Waking`.

### `Waking`

Ramp:

```text
currentLevel -> normalLevel
duration: 200 ms
```

Behavior:

- all physical presses received while Waking are wake-only and consumed;
- repeated input does not restart or extend the 200 ms wake ramp;
- no wake-only key click is played;
- after normal brightness is reached, transition to `Awake`;
- reset the idle timer to zero;
- the **next new physical press** is routed normally.

### Large elapsed updates

State timing must remain correct even if one `update()` receives enough elapsed time to cross a boundary.

For example, the controller must preserve:

```text
15 s idle
+
300 ms dim ramp
+
120 s dim hold
+
400 ms off ramp
```

rather than starting the 120-second hold from the initial 15-second idle threshold.

Elapsed processing must therefore account for state boundaries explicitly rather than resetting or discarding excess elapsed time incorrectly.

---

## 6. BDD Scenarios

### Scenario 1: Display begins dimming after 15 seconds

Given:

- display-power policy is active;
- state is `Awake`;
- normal brightness is active;
- no physical input occurs.

When:

- 14.999 seconds elapse.

Then:

- state remains `Awake`;
- normal brightness remains unchanged.

When:

- the total idle time reaches 15.000 seconds.

Then:

- state becomes `Dimming`;
- a 300 ms fade toward the dim target begins.

---

### Scenario 2: Dim hold begins only after the fade completes

Given:

- idle threshold has been reached;
- the display is `Dimming`.

When:

- the 300 ms dim ramp completes.

Then:

- brightness is 10% of normal;
- state becomes `Dimmed`;
- dim hold elapsed time is exactly zero.

When:

- 119.999 seconds of `Dimmed` time elapse.

Then:

- display remains at the dim target;
- state remains `Dimmed`.

When:

- the full 120 seconds complete.

Then:

- state becomes `TurningOff`.

---

### Scenario 3: Display reaches Off

Given:

- state is `TurningOff`;
- no physical input occurs.

When:

- 400 ms elapse.

Then:

- backlight reaches exactly zero;
- state becomes `Off`;
- Connectivity and Services remain active.

---

### Scenario 4: Normal Awake input resets inactivity

Given:

- state is `Awake`;
- 10 seconds of idle time have elapsed.

When:

- the user presses a normal application key.

Then:

- idle elapsed time resets to zero;
- the semantic input is not consumed;
- ApplicationShell processes it normally;
- normal key feedback behavior remains unchanged.

---

### Scenario 5: Dimmed input wakes but does not act

Given:

- state is `Dimmed`;
- Enter would normally activate the focused item.

When:

- the user presses Enter.

Then:

- state becomes `Waking`;
- wake starts from the current dim brightness;
- the Enter event is consumed;
- no navigation or Action occurs;
- no HID/Companion command occurs;
- no key-click sound occurs.

When:

- wake reaches `Awake`;
- the user releases Enter and presses Enter again.

Then:

- the second Enter is processed normally.

---

### Scenario 6: Off input wakes but does not act

Given:

- state is `Off`;
- the current view is MAC CONTROL;
- slot `1` has a valid host action.

When:

- the user presses `1`.

Then:

- the backlight begins waking;
- the `1` input is consumed;
- MAC CONTROL dispatches no Action;
- no Companion request is sent;
- no key feedback sound is played.

When:

- state becomes `Awake`;
- the user presses `1` again.

Then:

- MAC CONTROL handles the new press normally.

---

### Scenario 7: Input during a fade reverses smoothly

Given:

- display is midway through `Dimming` or `TurningOff`;
- current brightness is between its source and target.

When:

- the user presses any physical key.

Then:

- the input is consumed;
- the existing fade is cancelled;
- state becomes `Waking`;
- wake begins from the current actual brightness;
- brightness never jumps to the previous target first.

---

### Scenario 8: Modifier-only input wakes the display

Given:

- state is `Dimmed` or `Off`.

When:

- the user presses `Fn` without another key.

Then:

- physical activity is detected;
- display begins waking;
- no semantic application input is required;
- no application behavior occurs.

---

### Scenario 9: Repeated input during Waking remains consumed

Given:

- one wake press has already started `Waking`.

When:

- the user presses additional keys before the 200 ms wake ramp completes.

Then:

- those presses are consumed;
- no application Actions occur;
- the wake ramp does not restart from zero;
- the wake duration is not extended.

When:

- state has reached `Awake`;
- another new press occurs.

Then:

- that press routes normally.

---

### Scenario 10: Background activity does not reset idle

Given:

- state is `Awake`;
- no user presses a key.

When:

- Wi-Fi state updates;
- BLE reconnect activity occurs;
- Companion traffic occurs;
- battery state changes;
- Home redraws;
- the Home ambient animation advances;
- Mini App or Service background state changes.

Then:

- none of those events reset the display inactivity timer.

---

### Scenario 11: Off does not suspend the firmware

Given:

- display is `Off`.

When:

- normal runtime loops continue.

Then:

- HostService continues updating;
- CompanionService continues updating;
- HostControlService continues updating;
- NetworkService continues updating;
- BatteryService continues updating;
- keyboard polling continues;
- no service lifecycle is moved under display-power ownership.

---

### Scenario 12: Home ambient wave pauses while Off

Given:

- Home is visible;
- ambient wave is at a known phase.

When:

- display reaches `Off`;
- time passes.

Then:

- Home ambient-wave phase does not advance.

When:

- display wakes.

Then:

- the wave resumes from its previous phase;
- it does not jump forward by the time spent Off.

---

### Scenario classes not applicable

**Unavailable dependency:** not applicable to the core policy because display-power operation is local and synchronous; there is no remote dependency.

**Reconnect:** not applicable to the controller itself. BLE/Wi-Fi/Companion reconnect behavior must remain independent and is covered by the invariant that Services continue while Off.

**Stale completion:** there are no asynchronous display-power requests or completion callbacks. Ramp state is advanced synchronously from injected monotonic elapsed time.

---

## 7. Architecture Invariants

- Display-power policy is global and independent of navigation state.
- `DisplayPowerController` is the only owner of idle, dim, off, and wake timing.
- Brightness timing is driven only by injected monotonic elapsed time.
- `CardputerBacklightAdapter` is the only new code that calls the concrete M5 backlight API.
- UI screens and Mini Apps never set physical backlight brightness directly.
- Mini Apps never decide whether input is wake-only.
- Wake consumption happens before `ApplicationShell`.
- Wake-only input never reaches ActionBus.
- Wake-only input never reaches host-control dispatch.
- Wake-only input never produces UI key-click audio.
- Any physical press edge can count as user activity even when no semantic `InputEvent` exists.
- Key release does not count as a new activity event.
- Holding a key does not continuously reset inactivity.
- All input received while `Waking` remains consumed.
- Repeated wake presses do not restart the wake ramp.
- Awake input remains behaviorally identical to current firmware input.
- The dim hold begins only after the dim ramp reaches its target.
- Ramp reversal begins from current actual brightness.
- No brightness transition blocks the main loop.
- No `delay()` is introduced for display-power timing.
- Display Off means backlight zero only.
- Plan 032 introduces no ESP32 light sleep or deep sleep.
- Display Off does not suspend Connectivity or Services.
- Service updates remain outside UI scheduling exactly as today.
- Background state changes never count as user activity.
- Home ambient motion pauses while Off.
- Other domain timers continue to advance while Off.
- Normal brightness is not persisted or changed by this plan.
- Existing host selection, BLE bonds, Wi-Fi configuration, sound volume, and Mini App state are not modified by display-power transitions.

---

## 8. Tests Mapped to Scenarios

Add a focused native test target, for example:

```text
test/test_display_power_controller/test_main.cpp
```

Extend:

```text
test/test_system_runtime/test_main.cpp
```

and the relevant Home/ApplicationShell tests for ambient-wave behavior.

| Scenario | Test |
| --- | --- |
| 15 s idle threshold | `test_display_starts_dimming_at_idle_threshold` |
| Full dim ramp before hold | `test_dim_hold_starts_only_after_dim_ramp_completes` |
| Full 120 s dim hold | `test_display_stays_dimmed_for_full_hold_duration` |
| Final Off fade | `test_display_reaches_off_after_off_ramp` |
| Awake input resets idle | `test_awake_physical_press_resets_idle_without_consumption` |
| Dimmed wake consumption | `test_dimmed_input_wakes_and_is_consumed` |
| Off wake consumption | `test_off_input_wakes_and_is_consumed` |
| Fade reversal | `test_wake_reverses_active_fade_from_current_level` |
| Modifier-only wake | `test_modifier_only_physical_press_wakes_display` |
| Repeated wake input | `test_input_during_waking_is_consumed_without_restarting_ramp` |
| Background activity | `test_background_updates_do_not_reset_display_idle` |
| Home wave pause | `test_home_wave_does_not_advance_while_display_off` |
| Large elapsed interval | `test_large_elapsed_update_preserves_all_state_durations` |
| Service ownership | `make architecture-check` |
| No wake key sound | `test_wake_consumed_frame_plays_no_cue_and_dispatches_no_action` |
| No host command on wake | `test_wake_consumed_input_does_not_activate_a_slot` |
| Preserved normal brightness | `test_normal_level_is_captured_from_existing_firmware_brightness` |
| Policy starts after splash handoff | `test_display_power_policy_starts_only_after_splash_handoff` |
| Delayed update carrying a press | `test_physical_press_after_delayed_update_resets_idle_to_zero` |
| Delayed update carrying a wake press | `test_delayed_wake_press_starts_ramp_without_consuming_prior_elapsed` |
| Reversal during the final off fade | `test_wake_reverses_turning_off_fade_from_current_level` |
| Full wake path to MAC CONTROL | `test_off_mac_control_key_wakes_without_dispatching_host_action` |

Delivered locations:

```text
test/test_display_power_controller/test_main.cpp   state machine and ramps
test/test_system_runtime/test_main.cpp             wake gate and consumption
test/test_application_shell/test_main.cpp          Home wave pause, silent wake frame
test/test_mac_control/test_main.cpp                no slot activation on wake
```

### Fake backlight

Use a deterministic host fake:

```cpp
class FakeBacklightAdapter : public IBacklightAdapter {
  public:
    std::uint8_t level() const override;
    void setLevel(std::uint8_t level) override;

    std::vector<std::uint8_t> writes;
};
```

Tests should be able to verify:

```text
target endpoints
monotonic direction
no endpoint jump on reversal
no writes outside valid range
no blocking behavior
```

Do not write timing tests against wall-clock sleep.

Use injected elapsed time only.

### SystemRuntime input tests

Extend fake keyboard support so tests can independently express:

```text
physicalPress = true
InputEvents   = {}
```

for modifier-only wake.

And:

```text
physicalPress = true
InputEvents   = { Enter }
```

for consumed semantic wake input.

This distinction is a required part of Plan 032, not a test-only shortcut.

---

## 9. Physical Acceptance

Validate on a real Cardputer ADV.

### Timing

Confirm approximately:

```text
15 s idle
→ 300 ms fade
→ 10%
→ 2 min
→ 400 ms fade
→ Off
```

The exact behavior must remain based on monotonic timing; manual stopwatch validation is only physical confirmation.

### Dim readability

At the actual captured normal brightness:

- 10% must remain readable;
- Home text must remain recognizable;
- Settings rows must remain recognizable;
- Launcher must remain recognizable;
- MAC CONTROL tiles must remain recognizable.

If hardware PWM quantization makes the calculated 10% effectively unreadable, adjust the hardware mapping rather than changing the logical policy silently.

### Wake behavior

Validate wake from:

- Home;
- Launcher;
- Settings;
- Bluetooth;
- Wi-Fi;
- SYSTEM;
- MAC CONTROL.

For both `Dimmed` and `Off`:

1. focus an action that would visibly change state;
2. allow the screen to dim/off;
3. press the action key;
4. confirm only wake occurs;
5. confirm no click sound occurs;
6. press the same key again after wake;
7. confirm the second press performs the action.

### MAC CONTROL safety check

With MAC CONTROL open and slot `1` bound:

```text
screen Off
press 1
```

must:

```text
wake display
not activate application
not send Companion request
```

A second press after wake must behave normally.

### Modifier wake

Confirm at least:

```text
Fn
Shift
```

can wake a dimmed/off display without triggering application behavior.

### Wake during transition

Press a key:

- midway through the first dim fade;
- midway through the final off fade.

Confirm:

- fade reverses smoothly;
- there is no visible brightness flash;
- no application command is triggered.

### Connectivity while Off

While the display is dark:

- BLE remains connected or follows its existing reconnect policy;
- Wi-Fi remains connected;
- Companion remains live;
- waking the screen does not require reconnecting Services.

### Fresh framebuffer

Allow Service state to change while Off, then wake.

Confirm the screen does not briefly reveal stale host/network/battery state before showing the current state.

### Existing brightness

Confirm Plan 032 does not make normal Awake brightness unexpectedly brighter or darker than the firmware before the change.

---

## 10. Out of Scope

Plan 032 does not implement:

- ESP32 light sleep;
- ESP32 deep sleep;
- CPU frequency scaling;
- Wi-Fi power-save policy;
- BLE shutdown while idle;
- Service suspension;
- Companion suspension;
- automatic device shutdown;
- battery-based dynamic dim timing;
- charging-based dynamic dim timing;
- brightness Settings UI;
- persistent user brightness configuration;
- configurable dim timeout;
- configurable off timeout;
- per-Mini-App power policies;
- host-controlled display-brightness policy;
- wake from remote Companion events;
- wake from notifications;
- LED 8×8 power policy;
- Pomodoro-specific display exceptions;
- splash-specific framebuffer color transformation.

These may be added later without moving the core idle policy into individual apps.

---

## 11. Implementation Sequence

### Step 1 — Backlight abstraction

Add:

```text
IBacklightAdapter
FakeBacklightAdapter
CardputerBacklightAdapter
```

Verify basic level read/write behavior.

### Step 2 — RED: core power state machine

Write failing tests for:

```text
Awake
Dimming
Dimmed
TurningOff
Off
Waking
```

including exact boundary timing.

### Step 3 — GREEN: DisplayPowerController

Implement elapsed-time state transitions and linear ramps.

Keep it completely independent of:

```text
UI
keyboard implementation
ActionBus
Services
M5Unified
```

### Step 4 — RED: physical activity distinction

Add tests proving that:

```text
physicalPress=true + no InputEvents
```

still resets/wakes the display.

### Step 5 — Keyboard polling contract

Extend the keyboard adapter boundary to report physical press edges separately from translated semantic events.

Cardputer implementation must derive this from decoded TCA8418 press edges.

### Step 6 — RED: wake consumption

Add SystemRuntime tests proving that input from Dimmed/Off/Waking never reaches the caller.

### Step 7 — Runtime integration

Compose the controller and backlight adapter in `main.cpp`.

Integrate the power gate into the shared runtime input path.

Do not implement wake filtering separately in screens.

### Step 8 — Audio/Action integration verification

Add integration tests proving wake input does not produce:

```text
key sound
Action
MAC CONTROL host activation
```

### Step 9 — Home ambient-wave integration

Expose only the minimum read-only display-power state needed by Home/ApplicationShell.

Pause Home ambient-wave elapsed progression only while fully Off.

Do not let Home command the controller.

### Step 10 — Hardware adapter

Wire real backlight control through M5Unified.

Confirm startup normal brightness is captured only after platform initialization.

### Step 11 — Documentation

Update architecture and UI requirements in the same PR.

### Step 12 — Final validation

Run the applicable repository final gate:

```text
native tests
Python/static architecture tests
formatting
static analysis
production firmware build
```

Then complete physical Cardputer ADV acceptance.

---

## 12. Documentation Updates

### `docs/UI_REQUIREMENTS.md`

Change the current display-power requirement from:

```text
15 seconds
→ 10%
→ full 3 minutes
→ Off
```

to:

```text
15 seconds
→ 10%
→ full 2 minutes
→ Off
```

Keep the rule that the hold begins after the dim target has actually been reached.

After implementation, update the current-firmware boundary so dim/off/wake is no longer described as pending.

### `docs/ARCHITECTURE.md`

Document:

- `DisplayPowerController`;
- `IBacklightAdapter`;
- Cardputer hardware adapter boundary;
- physical-press activity reporting;
- shared wake-input consumption point;
- display Off not suspending Services.

Mark the phase-checklist item:

```text
Idle dim/off, brightness policy and wake-input consumption
```

complete only after software and required physical validation.

### `docs/plans/README.md`

Add Plan 032 with current status.

### User-facing documentation

Any manual section that describes display/device operation must match the delivered automatic dim/off/wake behavior.

Do not describe ESP sleep, configurable timeout, or brightness settings because Plan 032 does not provide them.

---

## 13. Completion Checklist

Plan 032 is software-complete when:

- [x] `IBacklightAdapter` exists.
- [x] Cardputer backlight adapter exists.
- [x] normal brightness is preserved from the existing firmware baseline.
- [x] `DisplayPowerController` is deterministic and host-testable.
- [x] 15-second idle threshold is implemented.
- [x] 300 ms dim ramp is implemented.
- [x] 10% dim target is implemented.
- [x] full 120-second dim hold is implemented.
- [x] 400 ms final fade is implemented.
- [x] 200 ms wake ramp is implemented.
- [x] fade reversal starts from current brightness.
- [x] physical keyboard activity is distinct from semantic events.
- [x] modifier-only presses can wake the display.
- [x] wake input is consumed before ApplicationShell.
- [x] wake input produces no key-click sound.
- [x] wake input produces no Action.
- [x] wake input produces no host/Companion command.
- [x] repeated input during Waking remains consumed.
- [x] Home ambient wave pauses while Off.
- [x] Services and Connectivity continue while Off.
- [x] no ESP sleep behavior is introduced.
- [x] native tests pass.
- [x] architecture/static tests pass.
- [x] formatting passes.
- [x] static analysis passes.
- [x] production firmware builds.
- [x] `UI_REQUIREMENTS.md` is updated from 3 minutes to 2 minutes.
- [x] `ARCHITECTURE.md` is updated.
- [x] `docs/plans/README.md` is updated.

Plan 032 is physically accepted when:

- [ ] real 10% brightness is readable;
- [ ] all fades look smooth;
- [ ] there is no brightness flash on reversal;
- [ ] Dimmed first press only wakes;
- [ ] Off first press only wakes;
- [ ] second press after wake works normally;
- [ ] modifier-only wake works;
- [ ] wake-only input is silent;
- [ ] MAC CONTROL wake does not activate a slot;
- [ ] BLE/Wi-Fi/Companion continue while Off;
- [ ] wake does not expose a stale framebuffer;
- [ ] normal Awake brightness matches the pre-Plan-032 firmware behavior.
