# 035 — Device display and LED brightness settings

## Current status

Software implemented with host tests. Physical device testing was confirmed by
the user on 2026-09-24.

## 0. Goal

Extend the existing global `SETTINGS` screen with persistent device-level display and Unit Puzzle LED settings.

Add:

- screen timeout profile;
- screen maximum brightness;
- Unit Puzzle maximum brightness.

The settings must:

- persist across reboot;
- apply immediately;
- reuse the existing Settings interaction model;
- preserve the current display wake/input-consumption behavior;
- prevent any Unit Puzzle owner from driving LEDs above the user-configured safe limit.

---

# 1. Final UX

Extend the existing Settings list from 3 rows to 6 rows:

```text
SETTINGS
──────────────────────────────────────
01  Bluetooth
02  Wi-Fi
03  Sound volume                 60%
04  Screen timeout             Normal
05  Screen brightness           100%
06  LED brightness                3%
```

Controls remain consistent with the current Settings screen:

```text
↑ / ↓     select row
← / →     change value
Enter     open Bluetooth / Wi-Fi
Tab       toggle Settings
Esc       back
```

Changes to numeric/device settings apply immediately.

There is no separate Save/Apply action.

---

# 2. Screen timeout

Add three timeout profiles:

```text
Normal
Long
Never
```

## Normal

Preserve the current display-power timing:

```text
awake
  │
  ├─ 15 seconds idle
  ▼
dim
  │
  ├─ 120 seconds
  ▼
off
```

Equivalent:

```text
15s → dim → 2min → off
```

## Long

Provide a substantially less aggressive desktop mode:

```text
60s → dim → 5min → off
```

## Never

Disable automatic display power transitions:

```text
awake forever
```

In `Never`:

- no automatic dim;
- no automatic off;
- explicit/programmatic wake requests remain harmless;
- screen stays at configured `Screen brightness`.

The ESP32 itself is not put into sleep by any of these modes.

This setting controls only the existing display/backlight idle policy.

---

# 3. Screen brightness

Add:

```text
Screen brightness
```

Range:

```text
minimum: 20%
maximum: 100%
step:    10%
default: 100%
```

Examples:

```text
20%
30%
40%
...
90%
100%
```

Changing the value must immediately change the visible backlight level.

No additional confirmation screen is required.

---

# 4. Dimming relative to configured brightness

`Screen brightness` becomes the normal/awake brightness used by `DisplayPowerController`.

The dim level remains:

```text
10% of configured normal brightness
```

Examples:

```text
Screen brightness = 100%
awake = 100%
dim   = 10%

Screen brightness = 70%
awake = 70%
dim   = 7%

Screen brightness = 40%
awake = 40%
dim   = 4%
```

Do not introduce a separate user-configurable dim brightness.

The existing `dimPercent = 10` policy remains internal.

---

# 5. DisplayPowerController policy model

Replace the currently hard-coded timeout constants used as the active policy with configurable runtime policy.

Introduce a small display policy model, conceptually:

```cpp
enum class ScreenTimeoutMode : std::uint8_t {
    Normal,
    Long,
    Never,
};

struct DisplayPowerPolicy {
    ScreenTimeoutMode timeoutMode;
    std::uint8_t brightnessPercent;
};
```

`DisplayPowerController` remains responsible for:

```text
Awake
Dimming
Dimmed
TurningOff
Off
Waking
```

Do not duplicate the state machine elsewhere.

The controller should expose configuration methods rather than becoming dependent on `ConfigurationService`.

For example, conceptually:

```cpp
void setTimeoutMode(ScreenTimeoutMode mode);
void setBrightnessPercent(std::uint8_t percent);

ScreenTimeoutMode timeoutMode() const;
std::uint8_t brightnessPercent() const;
```

Exact API naming may follow existing project conventions.

---

# 6. Timeout implementation

Internally map profiles to timing policy.

Conceptually:

```text
Normal
idle threshold: 15 s
dim hold:       120 s

Long
idle threshold: 60 s
dim hold:       300 s

Never
idle threshold: disabled
dim hold:       disabled
```

Keep existing ramp timings unchanged:

```text
dim ramp:   300 ms
off ramp:   400 ms
wake ramp:  200 ms
```

Do not make ramp timings configurable.

---

# 7. Runtime policy changes

Changing display settings while the firmware is running must have deterministic behavior.

When `Screen brightness` changes:

- persist the new value first;
- apply the new normal brightness;
- update the physical backlight immediately;
- reset the idle timer because the user just interacted with Settings.

When `Screen timeout` changes:

- persist the new value first;
- apply the new timeout policy immediately;
- treat the Settings interaction as activity;
- reset the old timeout progress.

When switching to `Never`:

- restore/wake the display to configured normal brightness;
- cancel any existing dim/off progression;
- remain `Awake`.

When switching from `Never` back to `Normal` or `Long`:

- start a fresh idle interval from zero.

Do not carry elapsed idle time across policy changes.

---

# 8. Preserve wake-only input behavior

The existing wake contract must remain unchanged.

When the display is:

```text
Dimming
Dimmed
TurningOff
Off
Waking
```

the first physical key press remains wake-only and must not trigger the underlying UI action.

Example:

```text
display off
↓
user presses →
↓
display wakes
↓
Settings value does NOT change
↓
user presses → again
↓
Settings value changes
```

Do not special-case Settings to bypass this behavior.

---

# 9. LED brightness UX

Add:

```text
LED brightness
```

Range:

```text
minimum: 1%
maximum: 10%
step:    1%
default: 3%
```

Allowed values:

```text
1%
2%
3%
4%
5%
6%
7%
8%
9%
10%
```

The configured value is the global physical maximum for the Unit Puzzle LED matrix.

Changing it must update active LED output immediately.

No reboot or Mini App restart should be required.

---

# 10. Global LED safety cap

The 10% limit must be enforced below individual Mini Apps.

Do not rely only on Settings validation.

Even malformed callers or future Mini Apps must not be able to produce physical output above:

```text
10%
```

The final LED output path must clamp brightness to:

```text
1% .. 10%
```

for normal configured operation.

The hard upper bound is:

```text
10%
```

This belongs in the shared Indicator/output policy rather than in individual apps.

---

# 11. Refactor current per-owner brightness

Current behavior contains owner-specific physical brightness values such as:

```text
LED Gallery = 3%
Pomodoro    = 3%
other owner = potentially 100%
```

Remove this as the primary physical-brightness policy.

The user setting becomes the global physical brightness.

Default:

```text
LED brightness = 3%
```

therefore preserving the current visual intensity for LED Gallery and Pomodoro after migration.

Future applications should not independently choose arbitrary physical brightness percentages.

---

# 12. Optional per-animation intensity model

Keep room for applications to control relative animation intensity without bypassing the global cap.

Conceptually:

```text
logical RGB frame
      ×
optional app intensity
      ×
global LED brightness
      =
physical frame
```

For example:

```text
global LED brightness = 8%
animation intensity   = 50%

effective maximum ≈ 4%
```

This is useful for:

- breathing animations;
- calm background indicators;
- warning effects;
- different visual states.

It is acceptable for the first implementation to use:

```text
animation intensity = 100%
```

for all existing owners.

Do not require this extended intensity API unless it makes the Indicator refactor cleaner.

The important requirement is that no owner bypasses the global 10% maximum.

---

# 13. DeviceSettingsService

Introduce a small service responsible for persisted device-level settings.

Suggested location:

```text
src/services/device_settings/
    device_settings_service.h
    device_settings_service.cpp
```

Responsibilities:

```text
ConfigurationService
       │
       ▼
DeviceSettingsService
       │
       ├── DisplayPowerController
       │
       └── IndicatorService
```

It should:

- expose current screen timeout;
- expose current screen brightness;
- expose current LED brightness;
- validate requested changes;
- persist changes through `ConfigurationService`;
- only apply live state after persistence succeeds;
- provide Actions consumed by the Settings UI.

Do not let `ApplicationShell` write NVS directly.

Do not make `DisplayPowerController` depend on `ConfigurationService`.

Do not make `IndicatorService` depend on `ConfigurationService`.

---

# 14. Settings Actions

Add explicit actions.

Suggested IDs:

```text
display.timeout.step
display.brightness.step
indicator.brightness.step
```

Examples:

```text
display.timeout.step
delta = -1 / +1

display.brightness.step
delta = -10 / +10

indicator.brightness.step
delta = -1 / +1
```

Alternatively, explicit set-actions are acceptable if they fit the project's conventions better.

Invalid deltas or out-of-range requests must be rejected.

UI code should not directly mutate service state.

---

# 15. Configuration model

Extend `SystemConfiguration`.

Conceptually:

```cpp
struct DeviceConfiguration {
    ScreenTimeoutMode screenTimeout = ScreenTimeoutMode::Normal;
    std::uint8_t screenBrightness = 100;
    std::uint8_t ledBrightness = 3;
};
```

Then:

```cpp
struct SystemConfiguration {
    HostConfiguration host;
    WifiConfiguration wifi;
    std::uint8_t soundVolume = 60;
    DeviceConfiguration device;
};
```

Equivalent flat fields are acceptable if preferred, but grouping device-level values is cleaner for future settings.

Defaults:

```text
screenTimeout    = Normal
screenBrightness = 100
ledBrightness    = 3
```

---

# 16. Configuration schema version

The current persisted writer uses schema version 4.

The loader also supports a leftover legacy version-5 Companion record.

Do not repurpose version 5.

Introduce:

```text
schema version 6
```

Version 6 appends the new device configuration to the existing persisted model.

Conceptually:

```text
existing v4 payload
+
screen timeout
+
screen brightness
+
LED brightness
```

Preserve compatibility with readable versions:

```text
v1
v2
v3
v4
legacy v5
```

Older records load with the new defaults:

```text
screen timeout    = Normal
screen brightness = 100%
LED brightness    = 3%
```

A successful subsequent settings write upgrades the record to v6.

Do not erase or reset:

- hosts;
- selected host;
- Bluetooth enabled state;
- host metadata;
- Wi-Fi configuration;
- sound volume.

Update `maximumSerializedSize` accordingly.

---

# 17. Configuration validation

Add validation rules.

## Screen timeout

Only valid enum values:

```text
Normal
Long
Never
```

Unknown persisted values are invalid data.

## Screen brightness

Valid only when:

```text
20 <= value <= 100
value % 10 == 0
```

## LED brightness

Valid only when:

```text
1 <= value <= 10
```

Persisted invalid configuration must follow the existing safe configuration-loading behavior.

Do not silently clamp corrupt persisted records.

Runtime actions may clamp step navigation at boundaries, but persisted records themselves must validate strictly.

---

# 18. Startup application

After:

```text
configuration.ensureLoaded()
```

device settings must be applied before normal interactive operation.

Startup order should ensure:

```text
configuration loaded
        ↓
DeviceSettingsService applies config
        ↓
display policy active
LED brightness policy active
        ↓
normal UI operation
```

The display should not briefly switch to an incorrect brightness during normal startup if it can reasonably be avoided.

Preserve splash behavior and existing `captureNormalLevel()` sequencing.

If necessary, separate:

```text
hardware brightness baseline discovery
```

from:

```text
configured user brightness application
```

rather than removing the existing initialization safety.

---

# 19. ApplicationShell integration

Increase Settings rows:

```cpp
settingsRowCount = 6;
```

Suggested row mapping:

```text
0 Bluetooth
1 Wi-Fi
2 Sound volume
3 Screen timeout
4 Screen brightness
5 LED brightness
```

Keep the current list style.

No submenu is required for the three new settings.

---

# 20. Settings rendering

Target UI:

```text
SETTINGS
──────────────────────────────────────
01  Bluetooth
02  Wi-Fi
03  Sound volume                 60%
04  Screen timeout             Normal
05  Screen brightness           100%
06  LED brightness                3%
```

All six rows fit in the current 135 px screen using the existing row spacing:

```text
24
42
60
78
96
114
```

Do not add scrolling in this plan.

Scrolling can be introduced later if Settings grows beyond the available viewport.

---

# 21. Horizontal input behavior

Extend the current `← / →` Settings behavior.

## Sound volume

Existing:

```text
0–100%
step 10%
```

## Screen timeout

```text
Normal ↔ Long ↔ Never
```

Boundary behavior:

```text
Normal + Left  -> Normal
Never  + Right -> Never
```

Do not wrap around.

## Screen brightness

```text
20% ↔ 30% ↔ ... ↔ 100%
```

Do not wrap.

## LED brightness

```text
1% ↔ 2% ↔ ... ↔ 10%
```

Do not wrap.

---

# 22. Input sound feedback

Preserve existing Settings key-feedback behavior.

For successful left/right value changes:

- use the existing `StepLeft`;
- use the existing `StepRight`.

At a boundary where the value cannot change:

- play normal `KeyPress`;
- do not persist anything.

Apply this consistently to:

```text
Sound volume
Screen timeout
Screen brightness
LED brightness
```

Avoid duplicating separate audio logic per row.

Refactor the existing volume-specific detection if necessary into generic adjustable-setting handling.

---

# 23. Settings render cache

Extend `SettingsFrame`.

Current frame tracks approximately:

```text
selection
volume
```

Add:

```text
screenTimeout
screenBrightness
ledBrightness
```

Redraw only rows whose:

- selection state changed; or
- displayed value changed.

Preserve the current incremental rendering behavior.

Do not redraw the entire screen every frame.

---

# 24. IndicatorService changes

Move physical brightness ownership into `IndicatorService`.

Conceptually expose:

```cpp
void setMaximumBrightnessPercent(std::uint8_t percent);
std::uint8_t maximumBrightnessPercent() const;
```

Valid runtime range:

```text
1..10
```

`IndicatorService::update()` must re-render the currently resolved logical frame when brightness changes even if the RGB frame itself did not change.

Example:

```text
current frame unchanged
brightness 3% → 6%
↓
hardware write required
```

Therefore brightness changes must mark output dirty.

---

# 25. Preserve logical frames

Do not permanently scale/store logical animation frames according to brightness.

Keep:

```text
IndicatorFrame
```

at full logical RGB values.

Apply brightness only while producing:

```text
LedHardwareFrame
```

This ensures changing:

```text
3% → 8%
```

can immediately regenerate output from the original frame instead of scaling an already-scaled frame.

---

# 26. LED disconnected / unavailable behavior

Changing LED brightness must remain safe when the Puzzle is not attached or initialization failed.

The persisted setting must still be accepted if configuration storage succeeds.

Do not make changing the setting depend on the physical LED matrix being present.

The next successfully initialized LED output should use the configured value.

---

# 27. Persistence failure semantics

Follow the same model used by audio settings.

For each setting:

```text
requested value
      ↓
validate
      ↓
save configuration
      ↓
if save succeeds
      ↓
apply runtime state
```

If persistence fails:

- return rejected/failure;
- leave existing live setting unchanged;
- do not partially apply the requested value.

This keeps persisted and runtime state consistent.

---

# 28. Tests — configuration

Extend configuration tests for:

```text
v6 round trip
```

Verify:

```text
ScreenTimeoutMode
screenBrightness
ledBrightness
```

round-trip correctly.

Add migration coverage:

```text
v1 → defaults
v2 → defaults
v3 → defaults
v4 → defaults
legacy v5 → defaults
```

Verify all older fields remain intact.

Add invalid-record tests for:

```text
unknown timeout enum
screen brightness < 20
screen brightness > 100
screen brightness not divisible by 10
LED brightness = 0
LED brightness > 10
truncated v6 payload
future schema
```

---

# 29. Tests — DisplayPowerController

Preserve all existing display-power tests.

Add tests for `Normal`:

```text
15 s idle
dim ramp
120 s dim hold
off ramp
off
```

Add tests for `Long`:

```text
60 s idle
dim ramp
300 s dim hold
off
```

Add tests for `Never`:

```text
large elapsed duration
state remains Awake
backlight remains normal brightness
```

Add brightness tests:

```text
100% → dim 10%
70%  → dim 7%
40%  → dim 4%
20%  → dim >= non-zero valid hardware level
```

Keep integer-rounding behavior deterministic.

---

# 30. Tests — runtime policy change

Test:

```text
Normal → Long
Long → Normal
Normal → Never
Never → Normal
Never → Long
```

Verify policy changes:

- reset old idle progress;
- restore Awake state;
- use configured brightness.

Test changing brightness during:

```text
Awake
Dimming
Dimmed
TurningOff
Off
Waking
```

The resulting state should converge safely to visible `Awake` output because the user is actively editing Settings.

---

# 31. Tests — wake input contract

Keep explicit regression coverage that the first key used to wake the display is consumed.

For example:

```text
LED brightness = 3%
display off
press Right
display starts waking
LED brightness remains 3%
press Right after awake
LED brightness becomes 4%
```

Repeat at least once for a display setting as well.

---

# 32. Tests — DeviceSettingsService

Test successful persistence + application:

```text
timeout change
screen brightness change
LED brightness change
```

Test boundary stepping:

```text
Normal cannot decrement
Never cannot increment

20% screen cannot decrement
100% screen cannot increment

1% LED cannot decrement
10% LED cannot increment
```

Test persistence failures:

```text
configuration save fails
runtime value remains unchanged
```

---

# 33. Tests — IndicatorService

Add tests proving:

```text
configured LED brightness affects hardware frame
```

Test:

```text
1%
3%
5%
10%
```

Verify:

```text
>10% cannot become physical output
```

Also test that a brightness-only change causes a hardware rewrite:

```text
same logical frame
3% → 4%
adapter write count increases
```

Test future/unknown owners:

```text
owner brightness cannot default to 100%
```

The global user setting must remain authoritative.

---

# 34. Tests — Settings UI

Verify row navigation across all six rows.

Test:

```text
Bluetooth Enter
Wi-Fi Enter
Sound ← →
Screen timeout ← →
Screen brightness ← →
LED brightness ← →
```

Verify displayed values update immediately.

Verify selected-row styling remains correct.

Verify boundary presses do not produce invalid actions.

---

# 35. Documentation

Update:

```text
README.md
docs/ARCHITECTURE.md
docs/manuals/device-guide.md
docs/manuals/installing-firmware.md
docs/plans/README.md
```

Document Settings controls:

```text
Screen timeout:
Normal = 15s → dim → 2min → off
Long   = 60s → dim → 5min → off
Never  = always awake

Screen brightness:
20–100%, step 10%

LED brightness:
1–10%, step 1%
```

Document schema v6 migration and continued readability of the legacy v5 Companion record.

---

# 36. Out of scope

Do not add:

- ESP32 deep sleep;
- CPU power management;
- configurable dim percentage;
- configurable ramp durations;
- arbitrary timeout seconds;
- separate LED brightness per Mini App;
- Settings submenus;
- animated brightness overlays;
- Settings scrolling;
- web/companion configuration;
- automatic brightness based on ambient light.

Those can be separate plans if needed.

---

# 37. Acceptance criteria

Implementation is complete when all of the following are true.

```text
[ ] Settings contains 6 rows.

[ ] Screen timeout supports Normal / Long / Never.

[ ] Normal keeps the existing:
    15s → dim → 120s → off behavior.

[ ] Long uses:
    60s → dim → 300s → off.

[ ] Never never automatically dims or switches off the display.

[ ] Screen brightness supports:
    20–100%, step 10%.

[ ] Dim brightness remains 10% of configured screen brightness.

[ ] LED brightness supports:
    1–10%, step 1%.

[ ] LED physical output can never exceed 10%.

[ ] Default LED brightness remains 3%.

[ ] Existing LED Gallery and Pomodoro appearance remains approximately unchanged
    with default settings.

[ ] Screen and LED brightness changes apply immediately.

[ ] All three settings persist across reboot.

[ ] Configuration uses schema v6.

[ ] v1–v4 and legacy v5 records remain readable.

[ ] Migrating old configuration preserves hosts, Wi-Fi and audio settings.

[ ] Persistence failure does not change live runtime state.

[ ] First key press while the screen is not Awake remains wake-only.

[ ] Existing display ramps remain unchanged.

[ ] Existing Bluetooth, Wi-Fi and sound Settings behavior is preserved.

[ ] Existing tests continue to pass.

[ ] New configuration, display-power, indicator and Settings tests pass.

[ ] Firmware build passes.

[ ] Physical smoke test passes on Cardputer ADV + Unit Puzzle.
```

---

# 38. Physical smoke test

On actual Cardputer ADV:

```text
1. Boot firmware.
2. Open Settings.
3. Confirm all 6 rows render correctly.

4. Set Screen brightness:
   100% → 70% → 40%.
5. Confirm visible brightness changes immediately.

6. Set timeout Normal.
7. Leave device untouched.
8. Confirm dim starts after ~15 seconds.
9. Confirm eventual display off.

10. Wake with one key.
11. Confirm that key does not trigger UI behavior.

12. Set timeout Long.
13. Confirm screen remains fully bright beyond 15 seconds.

14. Set timeout Never.
15. Confirm display does not dim automatically.

16. Open LED Gallery.
17. Return to Settings while LEDs remain active if supported.
18. Change LED brightness:
    1% → 3% → 5% → 10%.
19. Confirm matrix brightness changes immediately.

20. Confirm 10% remains visually safe.

21. Reboot.

22. Confirm:
    timeout persists;
    screen brightness persists;
    LED brightness persists.

23. Verify Pomodoro LEDs use the same configured global LED brightness.

24. Verify Bluetooth, Wi-Fi, audio, Launcher and wake behavior still work.
```

---

# 39. Suggested implementation order

```text
1. Extend configuration model + schema v6.
2. Add configuration migration/validation tests.
3. Make DisplayPowerController runtime-configurable.
4. Add display-power tests.
5. Make IndicatorService use configurable global LED brightness.
6. Remove unsafe owner-default 100% brightness behavior.
7. Add DeviceSettingsService.
8. Register device-setting Actions in main.cpp.
9. Inject DeviceSettingsService into ApplicationShell.
10. Extend Settings to six rows.
11. Generalize left/right setting feedback.
12. Add UI and integration tests.
13. Update documentation.
14. Run full host test suite.
15. Build firmware.
16. Perform Cardputer ADV physical smoke test.
```
