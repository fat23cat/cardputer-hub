# 034 — Sound Reactive Visualizer

Status: **Software implemented; physical Cardputer-Adv acceptance pending**

The first physical SOUND test completed recording windows but returned constant
`-8` PCM. Direct ESP-IDF I²S RX with 16-bit stereo frames still returned
constant `-1` PCM. The current adapter follows a published working
Cardputer-Adv configuration: 30 dB microphone PGA gain, 32-bit stereo I²S DMA,
and high-16-bit extraction for mono analysis. A physical test of this revision
still showed constant PCM and no sound response. Speaker handover and the rest
of physical acceptance remain pending.

The production Launcher name is **SOUND**; the internal app and
indicator owner id remains `sound-reactive`.

Suggested branch:

```text
feat/034-sound-reactive-visualizer
```

Suggested PR:

```text
[034] Add sound-reactive visualizer
```

Suggested plan file:

```text
docs/plans/034-sound-reactive-visualizer.md
```

---

# 0. Starting Point

Current `main` already provides:

```text
AppRegistry
MiniAppRuntime
ApplicationShell
UiScheduler
AudioService
CardputerAudioAdapter
IndicatorService
ILEDAdapter
PuzzleWs2812Adapter
DisplayPowerController
```

Production Mini Apps currently include:

```text
SYSTEM
MAC CONTROL
POMODORO
```

The Mini App lifecycle already provides:

```cpp
onActivate()
onDeactivate()
update(input, elapsed)
```

and therefore supports elapsed-time-driven animation without blocking the firmware loop.

The external M5Stack Unit Puzzle 8×8 matrix is already abstracted as:

```text
IndicatorService
    ↓
ILEDAdapter
    ↓
PuzzleWs2812Adapter
```

with claim-based arbitration.

Pomodoro already proves the intended model:

```text
feature/controller
    ↓
IndicatorService claim
    ↓
logical 8×8 frame
    ↓
brightness policy
    ↓
physical matrix
```

The new feature must reuse this infrastructure.

## Existing audio constraint

The current output path is:

```text
AudioService
    ↓
IAudioAdapter
    ↓
CardputerAudioAdapter
    ↓
M5.Speaker
```

`CardputerAudioAdapter::begin()` currently explicitly disables:

```cpp
M5.Mic
```

before starting the speaker.

Therefore microphone capture must not be introduced by independently calling:

```cpp
M5.Mic.begin()
```

from the Mini App.

Speaker and microphone ownership must be coordinated.

---

# 1. Goal

Add a production Mini App:

```text
SOUND
```

that turns the Cardputer display and optional 8×8 RGB LED matrix into a real-time sound-reactive ambient visualizer.

The built-in microphone continuously measures relative sound level.

The visualizer reacts in two primary ways:

```text
sound level
    ↓
animation speed

sound level
    ↓
color
GREEN → YELLOW → RED
```

Expected feel:

```text
quiet room
→ slow, calm green animation

normal conversation
→ moderate green/yellow movement

loud environment
→ faster yellow/orange movement

very loud sound
→ fast red movement

short loud transient
→ immediate acceleration followed by smooth decay
```

The app is not intended to be a calibrated sound-pressure meter.

It measures:

```text
relative microphone signal level
```

not certified:

```text
dBA / dB SPL
```

---

# 2. Scope

Plan 034 delivers:

```text
microphone hardware adapter
microphone capture service
speaker ↔ microphone handover
RMS sound-level analysis
attack/release smoothing
adaptive ambient baseline
normalized sound level 0.0 .. 1.0
green → yellow → red color mapping
sound-driven animation speed
LCD sound-reactive animation
8×8 LED sound-reactive animation
IndicatorService arbitration
Mini App registration
unit tests
physical Cardputer-Adv acceptance
```

Production LED brightness:

```text
maximum 3%
```

The app owns the matrix only while SOUND is active.

When SOUND closes:

```text
microphone stops
SOUND LED claim is released
speaker output is restored
previous IndicatorService owner becomes visible again
```

---

# 3. Architecture

Target dependency graph:

```text
                    ┌──────────────────────┐
                    │  SoundReactiveApp    │
                    │ UI + visual motion   │
                    └──────────┬───────────┘
                               │ snapshot
                               ↓
                    ┌──────────────────────┐
                    │ MicrophoneService    │
                    │ capture + analysis   │
                    └───────┬──────┬───────┘
                            │      │
                 suspend /  │      │ samples
                  restore   │      ↓
                            │   IMicrophoneAdapter
                            │      ↓
                            │ CardputerMicrophoneAdapter
                            │      ↓
                            │ ES8311 + ESP-IDF I²S RX
                            ↓
                       AudioService
                            ↓
                       IAudioAdapter
                            ↓
                   CardputerAudioAdapter
                            ↓
                         M5.Speaker
```

LED path:

```text
SoundReactiveApp
        ↓
SoundReactiveLedController
        ↓
IndicatorService
        ↓
ILEDAdapter
        ↓
PuzzleWs2812Adapter
```

Alternatively `SoundReactiveApp` may own the foreground indicator claim directly if the LED state has no lifecycle outside the app.

Preferred structure:

```text
SoundReactiveApp
    owns:
        active visualizer lifecycle
        LCD presentation
        animation phase
        foreground LED claim

MicrophoneService
    owns:
        microphone capture
        signal processing
        normalized level

AudioService
    owns:
        speaker lifecycle
        output suspension / restoration

CardputerMicrophoneAdapter
    owns:
        ES8311 ADC and I²S RX hardware interaction
```

There is no background SOUND session.

Closing the Mini App stops capture.

---

# 4. Ownership & Boundaries

| Component                    | Owns                                             | May call                                           | Must not call                           |
| ---------------------------- | ------------------------------------------------ | -------------------------------------------------- | --------------------------------------- |
| `SoundReactiveApp`           | UI, animation phase, active LED claim            | `MicrophoneService`, `IndicatorService`, display   | `M5.Mic`, `M5.Speaker`, raw WS2812      |
| `MicrophoneService`          | capture lifecycle, RMS, normalization, smoothing | `IMicrophoneAdapter`, `AudioService` lifecycle API | display, LED hardware, Mini App methods |
| `AudioService`               | speaker output lifecycle and restoration         | `IAudioAdapter`                                    | microphone hardware directly            |
| `CardputerMicrophoneAdapter` | ES8311 ADC and I²S RX hardware                   | ESP-IDF I²S, codec I²C                             | speaker, display, LED                   |
| `CardputerAudioAdapter`      | speaker hardware                                 | `M5.Speaker`                                       | microphone lifecycle                    |
| `IndicatorService`           | LED arbitration and brightness policy            | `ILEDAdapter`                                      | microphone/audio                        |
| `PuzzleWs2812Adapter`        | physical pixel mapping                           | ESP-IDF LED driver                                 | application logic                       |

The Mini App must never coordinate:

```text
M5.Speaker.end()
M5.Mic.begin()
```

itself.

---

# 5. Audio Input Foundation

Add:

```text
src/core/audio/microphone_adapter.h

src/hardware/cardputer/
    cardputer_microphone_adapter.h
    cardputer_microphone_adapter.cpp

src/services/microphone/
    microphone_service.h
    microphone_service.cpp
```

Conceptual adapter:

```cpp
class IMicrophoneAdapter {
public:
    virtual ~IMicrophoneAdapter() = default;

    virtual bool begin(std::uint32_t sampleRate) = 0;
    virtual void end() = 0;

    virtual MicrophoneReadResult read(
        std::int16_t* samples,
        std::size_t sampleCount
    ) = 0;
};
```

`MicrophoneReadResult` distinguishes `Pending`, `Ready`, and `Error`. A failed
re-arm is reported as `Error` immediately; the completed window is discarded,
the microphone is stopped, the speaker is restored, and the service enters
`Failed` with no active level snapshot.

Production sampling rate:

```text
16 kHz
mono
16-bit PCM
```

Initial analysis window:

```text
256 samples
```

At 16 kHz:

```text
256 samples ≈ 16 ms
```

Do not keep long audio recordings.

Only a small bounded analysis buffer is needed.

Raw microphone audio must not be:

```text
persisted
sent over network
written to SD
exposed to the Mini App
```

after analysis.

---

# 6. Speaker / Microphone Handover

Extend the existing audio output boundary so speaker hardware can be deliberately released.

Conceptually extend:

```cpp
IAudioAdapter
```

with:

```cpp
virtual void end() = 0;
```

and add explicit lifecycle to `AudioService`:

```text
Running
SuspendedForInput
```

Suggested API shape:

```cpp
bool AudioService::suspend();
bool AudioService::resume();
bool AudioService::suspended() const;
```

Exact naming may differ.

## SOUND activation

```text
SoundReactiveApp::onActivate()
        ↓
MicrophoneService::start()
        ↓
AudioService::suspend()
        ↓
CardputerAudioAdapter::end()
        ↓
CardputerMicrophoneAdapter::begin()
```

## SOUND deactivation

```text
SoundReactiveApp::onDeactivate()
        ↓
MicrophoneService::stop()
        ↓
CardputerMicrophoneAdapter::end()
        ↓
AudioService::resume()
        ↓
CardputerAudioAdapter::begin(savedVolume)
```

The current configured volume must survive the switch.

No volume setting may be modified by SOUND.

---

# 7. Audio Cue Behavior While SOUND Is Active

Because microphone and speaker cannot be active simultaneously, normal UI audio cues are unavailable while SOUND owns the microphone.

While:

```text
MicrophoneService == Capturing
```

calls such as:

```cpp
audio.play(...)
```

must fail cleanly or become a no-op.

They must not:

```text
stop microphone capture temporarily
play the sound
restart microphone
```

for every keypress.

That would produce:

```text
audio glitches
capture gaps
codec churn
animation jumps
```

SOUND therefore operates silently.

After SOUND closes, ordinary audio behavior is restored.

---

# 8. Microphone Service State Model

Suggested state:

```cpp
enum class MicrophoneState {
    Idle,
    Starting,
    Capturing,
    Failed,
};
```

Snapshot:

```cpp
struct MicrophoneSnapshot {
    MicrophoneState state;

    float rawLevel;
    float normalizedLevel;
    float peakLevel;

    float ambientLevel;
};
```

Valid normalized range:

```text
0.0 .. 1.0
```

The UI and LED code consume only the normalized snapshot.

They do not process PCM.

---

# 9. Signal Analysis

For each PCM window:

```text
PCM samples
    ↓
remove / ignore DC bias
    ↓
RMS
    ↓
logarithmic level
    ↓
normalize
    ↓
attack/release envelope
```

RMS:

```text
rms = sqrt(
    sum(sample²) / sampleCount
)
```

Convert to relative logarithmic magnitude:

```text
dBFS = 20 * log10(rms / fullScale)
```

The exact floating-point implementation may be optimized later if required.

No DSP library is required for Plan 034.

Do not add FFT.

---

# 10. Adaptive Baseline

A fixed raw microphone threshold will behave poorly between:

```text
quiet office
busy room
music playing
outdoor environment
```

Maintain a slowly moving ambient baseline.

Conceptually:

```text
ambientLevel
    ↓ slow moving average

currentLevel - ambientLevel
    ↓
normalized reactive level
```

The baseline must adapt slowly enough that:

```text
continuous conversation
```

does not immediately become the new definition of silence.

Initial target adaptation:

```text
~10–30 seconds
```

Exact coefficients may be tuned during physical acceptance.

---

# 11. Attack / Release

Direct microphone values must not drive rendering.

Use an envelope:

```text
target > current
→ fast attack

target < current
→ slow release
```

Initial targets:

```text
attack:
~50–100 ms

release:
~400–700 ms
```

Example:

```cpp
if (target > level)
    level += (target - level) * attack;
else
    level += (target - level) * release;
```

This should create:

```text
clap
→ immediate reaction
→ smooth return

speech
→ lively motion
→ no frame-by-frame flicker
```

All timing must be based on injected elapsed time, not assumed frame count.

---

# 12. Reactive Level

Expose one primary value:

```text
reactiveLevel
0.0 .. 1.0
```

Interpretation:

```text
0.00
quiet

0.25
low activity

0.50
normal/moderate sound

0.75
loud

1.00
very loud / saturated
```

This is a presentation scale.

It is not:

```text
dBA
dB SPL
hearing safety measurement
```

---

# 13. Color Mapping

The main visual color is derived continuously from:

```text
reactiveLevel
```

Mapping:

```text
0.00
GREEN

0.50
YELLOW

1.00
RED
```

Use continuous interpolation rather than discrete thresholds.

Conceptually:

```text
0.00 ───────── 0.50 ───────── 1.00
GREEN          YELLOW          RED
```

Suggested logical colors:

```cpp
GREEN  = {  0, 255,   0}
YELLOW = {255, 220,   0}
RED    = {255,   0,   0}
```

Interpolation:

```text
0.0 .. 0.5
GREEN → YELLOW

0.5 .. 1.0
YELLOW → RED
```

Color transitions must be smoothed.

Do not allow speech to produce:

```text
green-red-green-red-green
```

every few milliseconds.

Color may use a slightly slower envelope than animation speed.

Suggested:

```text
speed envelope:
fast

color envelope:
slightly slower
```

---

# 14. Animation Speed

Map normalized level to animation velocity.

Suggested initial range:

```text
quiet:
0.15 cycles / second

moderate:
~0.8 cycles / second

loud:
~1.7 cycles / second

maximum:
~2.5 cycles / second
```

Do not linearly map the whole range if physical testing feels flat.

A mild ease-in curve is preferred:

```text
speed =
minSpeed +
pow(level, ~1.4) * speedRange
```

The exact curve is presentation tuning, not domain behavior.

---

# 15. LCD Visual Design

The screen is primarily an ambient visualization, not a diagnostic dashboard.

Resting appearance:

```text
dark / Ink background

multiple moving dotted / segmented waves

same global sound-reactive color

minimal or no permanent text
```

Conceptual representation:

```text
              ·       ·
        ·   ·   ·   ·
   ·  ·             ·  ·
 ·                       ·

        ·   ·   ·
   ·  ·         ·  ·
```

Use approximately:

```text
2–3 phase-shifted flowing waves
```

constructed from small:

```text
dots
squares
short line segments
```

rather than expensive per-pixel effects.

The result should feel like:

```text
slow flowing field
```

when quiet and:

```text
energetic flowing field
```

when loud.

Do not render a traditional audio waveform.

Do not make the primary UI:

```text
LEVEL 47%
PEAK 82%
```

The purpose is visual reaction, not measurement.

---

# 16. LCD Motion Model

`SoundReactiveApp` owns animation phase:

```cpp
float phase;
```

Each update:

```text
phase += elapsedSeconds * animationSpeed
```

The wave geometry is derived from:

```text
phase
reactiveLevel
```

No animation state depends on:

```text
number of update calls
```

Large elapsed values must advance correctly without loops.

---

# 17. LCD Frame Rate

The global UI scheduler may update more frequently than the visualizer needs.

Target SOUND rendering:

```text
~25–30 FPS
```

The app should accumulate elapsed time and redraw only when its own frame interval is reached.

Suggested:

```text
33–40 ms
```

Do not redraw the LCD on every firmware loop.

Input handling remains immediate.

Animation rendering is rate-limited independently.

---

# 18. LED Animation

SOUND owns an:

```text
FOREGROUND_APPLICATION
```

IndicatorService claim while active.

The logical LED animation should visually relate to the LCD animation.

Use a moving 8×8 wave/blob field.

Example low level:

```text
........
........
...GG...
..GG....
.GG.....
GG......
........
........
```

Later phase:

```text
........
....GG..
...GG...
..GG....
.GG.....
........
........
........
```

As sound becomes louder:

```text
same spatial motion
+ faster phase progression
+ GREEN → YELLOW → RED
```

The first implementation does not need sound-driven brightness.

Brightness remains fixed.

---

# 19. LED Brightness

SOUND has a production maximum effective brightness of:

```text
3%
```

Add an owner policy analogous to the existing Pomodoro policy.

Conceptually:

```cpp
inline constexpr char soundReactiveIndicatorOwner[] =
    "sound-reactive";

inline constexpr std::uint8_t
    soundReactiveBrightnessPercent = 3;
```

Then:

```text
IndicatorService
    owner = sound-reactive
        ↓
brightness = 3%
```

The Mini App must not manually scale RGB values to 3%.

It supplies full logical color values.

`IndicatorService` owns physical brightness scaling.

This is important because a full logical:

```text
RED = 255,0,0
```

still produces useful color resolution after 3% scaling.

---

# 20. LED Update Rate

The physical matrix does not need to match LCD FPS.

Target:

```text
~15–20 FPS
```

Suggested interval:

```text
50–66 ms
```

`IndicatorService` already avoids identical hardware writes.

The SOUND app should additionally avoid producing pointless new frames faster than its LED animation cadence.

---

# 21. LED Arbitration

Example:

```text
Pomodoro running
→ BACKGROUND_APPLICATION

SOUND opens
→ FOREGROUND_APPLICATION
```

Visible matrix:

```text
SOUND
```

Pomodoro continues normally underneath.

When SOUND closes:

```text
SOUND claim released
```

`IndicatorService` immediately resolves the latest Pomodoro frame.

Do not:

```text
clear the complete matrix globally
restore an old cached Pomodoro frame
stop Pomodoro
```

---

# 22. App Lifecycle

## Activation

```text
Launcher
    ↓
SOUND
    ↓
onActivate()
    ↓
start microphone session
    ↓
acquire foreground IndicatorService claim
    ↓
reset animation phase/envelopes
```

Do not display stale sound state from the previous session.

The initial visual should begin calm:

```text
green
slow movement
```

and converge toward the measured environment.

## Deactivation

Escape:

```text
release LED claim
stop microphone
restore speaker
clear presentation-only state
return to Launcher
```

No sound-reactive state continues in background.

---

# 23. App Registration

Register:

```text
id:
sound-reactive

displayName:
SOUND

iconId:
sound-reactive

entryRoute:
sound-reactive

requiredCapabilities:
none
```

The external Puzzle matrix must remain optional.

The app still works using the LCD when no LED hardware is attached.

---

# 24. Input

Plan 034 needs almost no controls.

Required:

```text
Escape
→ close app
```

Optional diagnostic overlay:

```text
D
→ toggle diagnostics
```

If implemented, diagnostics may display:

```text
LEVEL 0.42
PEAK  0.71
BASE  0.13
```

This overlay is for development/tuning only.

It must not become the primary resting presentation.

No settings menu is required.

---

# 25. Microphone Failure

If microphone startup fails:

```text
SOUND remains stable
LED claim is not acquired or is released
speaker is restored
```

Display:

```text
MIC UNAVAILABLE
```

Escape still works.

The app must not:

```text
crash
retry every frame
leave speaker disabled
leave stale LED claim
```

An in-flight capture or re-arm failure follows the same recovery path and
replaces any previously displayed active level with `MIC UNAVAILABLE`.

A future explicit retry action is out of scope.

---

# 26. Partial Handover Failure

Important failure sequence:

```text
speaker suspended
    ↓
microphone start fails
```

Recovery must be:

```text
microphone remains stopped
speaker resumes
service reports Failed
```

Never leave the device with both:

```text
speaker unavailable
microphone unavailable
```

because startup failed halfway through.

Likewise:

```text
microphone stop
    ↓
speaker restart fails
```

must produce a known `AudioService` state rather than pretending speaker output is available.

---

# 27. Display Power

Plan 034 does not change the global display dim/off policy.

SOUND is not allowed to fake user activity every frame.

The LED animation may continue while the Mini App remains active and the LCD backlight follows the existing power policy.

A generic:

```text
DisplayWakeLock / keep-awake claim
```

may be introduced in a future plan if persistent visualizer mode is desired.

Do not add SOUND-specific hacks to `DisplayPowerController`.

---

# 28. Main Loop Composition

Conceptually:

```text
runtime.update(elapsed)

hosts.update(elapsed)
companion.update(elapsed)
hostControl.update()
network.update(elapsed)
battery.update(elapsed)

pomodoro.update(elapsed)
pomodoroLed.update(elapsed)

microphone.update(elapsed)

indicator.update()

applicationShell.update(...)
```

`MicrophoneService` may remain idle while SOUND is inactive.

Do not permanently capture microphone samples from boot.

---

# 29. Rendering / Capture Separation

Do not couple microphone sampling frequency to LCD rendering frequency.

They are different concerns:

```text
microphone capture
≈ audio-window cadence

signal envelope
≈ every completed sample window

LCD
≈ 25–30 FPS

LED
≈ 15–20 FPS
```

A dropped LCD frame must not mean:

```text
dropped sound analysis state
```

and a microphone window must not automatically force a display transfer.

---

# 30. Suggested Files

New:

```text
src/core/audio/
    microphone_adapter.h

src/hardware/cardputer/
    cardputer_microphone_adapter.h
    cardputer_microphone_adapter.cpp

src/services/microphone/
    microphone_service.h
    microphone_service.cpp

src/apps/sound_reactive/
    sound_reactive_app.h
    sound_reactive_app.cpp
    sound_reactive_graphics.h
    sound_reactive_graphics.cpp
```

Potentially modified:

```text
src/core/audio/audio_adapter.h

src/hardware/cardputer/
    cardputer_audio_adapter.h
    cardputer_audio_adapter.cpp

src/services/audio/
    audio_service.h
    audio_service.cpp

src/services/indicator/
    indicator_service.h
    indicator_service.cpp

src/main.cpp

main/CMakeLists.txt

docs/ARCHITECTURE.md
docs/UI_REQUIREMENTS.md
docs/plans/README.md
```

Tests:

```text
test/test_microphone_service/
test/test_sound_reactive/
test/test_audio_service/
test/test_indicator_service/
```

---

# 31. BDD Scenarios

### Scenario 1: Open SOUND in a quiet room

Given:

* SOUND is inactive
* speaker output is available
* microphone hardware starts successfully
* current sound level is low

When:

* user opens SOUND

Then:

* AudioService suspends speaker output
* MicrophoneService starts capture
* SOUND acquires one foreground LED claim
* LCD animation begins
* color converges toward green
* animation converges toward minimum speed

---

### Scenario 2: Environment becomes louder

Given:

* SOUND is active
* normalized level is low
* visualizer is green and moving slowly

When:

* microphone RMS rises continuously

Then:

* normalized level increases
* animation speed increases smoothly
* color moves continuously from green toward yellow/red
* no hard color threshold produces flicker

---

### Scenario 3: Short loud transient

Given:

* SOUND is active at a quiet level

When:

* a short high-amplitude sound occurs

Then:

* animation reacts quickly
* color moves toward the loud end quickly
* level decays more slowly than it attacked
* subsequent frames return smoothly toward the ambient state

---

### Scenario 4: Close SOUND

Given:

* SOUND is active
* microphone is capturing
* SOUND owns the foreground LED claim

When:

* user presses Escape

Then:

* microphone capture stops exactly once
* SOUND LED claim is released
* speaker output is restored with the previous volume
* Launcher becomes visible
* no SOUND state remains active in background

---

### Scenario 5: Pomodoro exists underneath SOUND

Given:

* Pomodoro owns a background indicator claim
* SOUND is opened

When:

* SOUND acquires a foreground claim

Then:

* SOUND becomes the resolved LED owner
* Pomodoro continues updating its hidden claim

When:

* SOUND closes

Then:

* Pomodoro becomes the resolved LED owner
* its current frame is shown
* no stale pre-SOUND frame is restored

---

### Scenario 6: LED matrix is absent

Given:

* Puzzle output is unavailable physically
* LCD and microphone work

When:

* SOUND runs

Then:

* microphone analysis works
* LCD animation works
* no application behavior depends on LED presence
* closing SOUND restores speaker normally

---

### Scenario 7: Microphone startup fails

Given:

* speaker is initially active
* microphone adapter fails during start

When:

* SOUND activates

Then:

* MicrophoneService enters Failed
* speaker output is restored
* SOUND shows `MIC UNAVAILABLE`
* SOUND owns no active visualizer LED frame
* Escape remains functional

---

### Scenario 8: UI cue is requested during capture

Given:

* SOUND is active
* microphone owns audio input

When:

* a subsystem requests an AudioService cue

Then:

* the cue is not played
* microphone capture is not interrupted
* no speaker/microphone hardware thrashing occurs

---

### Scenario 9: Repeated activation/deactivation

Given:

* SOUND has successfully run once

When:

* user repeatedly opens and closes SOUND

Then:

* each activation creates exactly one microphone session
* each activation creates at most one indicator claim
* each deactivation releases both
* speaker state remains recoverable
* no claim/session/resource leaks accumulate

---

### Scenario 10: Large elapsed value

Given:

* SOUND animation phase is running

When:

* an update receives a large elapsed value

Then:

* animation phase advances according to elapsed time
* state remains bounded
* no loop is used to simulate missed frames
* application does not block while catching up

---

# 32. Architecture Invariants

* Mini Apps never call `M5.Mic`.
* Mini Apps never call `M5.Speaker`.
* `CardputerMicrophoneAdapter` is the only new component that owns ES8311 ADC capture and I²S RX.
* `CardputerAudioAdapter` remains the owner of `M5.Speaker`.
* Microphone capture and speaker playback are mutually exclusive.
* `MicrophoneService` owns sound analysis.
* `SoundReactiveApp` never receives raw PCM samples.
* SOUND does not store or transmit microphone audio.
* SOUND does not require network access.
* SOUND does not require the external LED matrix.
* SOUND LED output always uses `IndicatorService`.
* SOUND never manipulates raw WS2812 hardware.
* SOUND LED priority is `FOREGROUND_APPLICATION`.
* SOUND effective LED brightness never exceeds 3%.
* LCD animation uses elapsed time, not frame count.
* LED animation uses elapsed time, not frame count.
* LCD rendering frequency is independent from microphone capture cadence.
* Audio output volume survives microphone sessions.
* Closing SOUND always attempts to restore speaker availability.
* No blocking delay is introduced into presentation logic.

---

# 33. Tests Mapped to Scenarios

| Scenario                       | Test                                                      |
| ------------------------------ | --------------------------------------------------------- |
| Quiet activation               | `test_sound_app_activation_starts_microphone_and_claim`   |
| Loudness increases             | `test_level_increase_accelerates_and_shifts_color`        |
| Loud transient                 | `test_attack_is_faster_than_release`                      |
| Close SOUND                    | `test_deactivation_releases_mic_led_and_restores_audio`   |
| Pomodoro arbitration           | `test_sound_claim_temporarily_overrides_background_claim` |
| Matrix absent                  | adapter/service test proving LCD path is independent      |
| Microphone failure             | `test_microphone_start_failure_restores_speaker`          |
| Cue during capture             | `test_audio_play_is_rejected_while_microphone_active`     |
| Repeated sessions              | `test_repeated_activation_has_no_resource_leak`           |
| Large elapsed                  | `test_animation_uses_elapsed_time`                        |
| Green/yellow/red interpolation | `test_reactive_color_mapping`                             |
| 3% LED policy                  | `test_sound_indicator_brightness_is_three_percent`        |
| RMS normalization              | `test_pcm_windows_produce_expected_relative_order`        |
| Silence                        | `test_zero_samples_produce_zero_level_without_nan`        |
| Saturation                     | `test_full_scale_samples_clamp_level_to_one`              |

Pure DSP tests must use synthetic PCM arrays.

They must not require Cardputer hardware.

---

# 34. Physical Acceptance

Test on a physical Cardputer-Adv.

## Quiet room

Verify:

```text
animation remains slow
color remains mostly green
motion does not visibly jitter
```

## Speaking

Speak normally approximately:

```text
0.5–1 m
```

from the device.

Verify:

```text
movement visibly accelerates
green moves toward yellow
release is smooth when speech stops
```

## Clap / sharp transient

Verify:

```text
reaction is immediate
visualizer reaches orange/red
animation does not freeze
return is smooth
```

## Continuous loud audio

Play music nearby.

Verify:

```text
animation remains responsive
does not oscillate randomly
does not remain permanently saturated unless input really is saturated
```

## LED

Verify:

```text
green → yellow → red transition is visible
3% maximum brightness is comfortable
8×8 animation remains recognizable
no obvious flashing/flicker
```

## Audio handover

Verify sequence repeatedly:

```text
normal firmware
→ UI sounds work

open SOUND
→ no speaker cues
→ microphone reacts

close SOUND
→ UI sounds work again
```

Repeat at least:

```text
10 times
```

to catch codec/I2S lifecycle problems.

Physical shutdown acceptance remains pending. With continuous loud audio so
`record()` repeatedly has an active window, press Escape at random points and
confirm speaker cues work immediately after exit. Repeat 10–20 times. Also
try rapid open → close, open → wait → close, open during Pomodoro, and close
while the level is changing. Check for crashes, watchdog resets, a stuck
microphone, a permanently silent speaker, avoidable codec pops, and a stale
SOUND LED claim.

## Pomodoro arbitration

Verify:

```text
Pomodoro active
→ open SOUND
→ SOUND owns LED

close SOUND
→ current Pomodoro LED state returns
```

---

# 35. Performance Acceptance

While SOUND is active:

```text
keyboard remains responsive
Escape responds immediately
BLE/Wi-Fi service loops remain responsive
no watchdog reset
no visible display tearing
no repeated heap growth
```

No per-frame dynamic allocations should occur in:

```text
microphone analysis
LCD animation
LED frame generation
```

Buffers should be fixed-size or allocated once during construction/startup.

---

# 36. Tuning Constants

Keep presentation constants grouped so physical tuning does not require algorithm changes.

Conceptually:

```cpp
struct SoundReactiveTuning {
    float minimumSpeed;
    float maximumSpeed;

    float attackMs;
    float releaseMs;

    float colorAttackMs;
    float colorReleaseMs;

    float ambientAdaptMs;

    std::chrono::milliseconds lcdFrameInterval;
    std::chrono::milliseconds ledFrameInterval;
};
```

Production defaults remain compile-time constants in Plan 034.

No persistent settings UI yet.

---

# 37. Out of Scope

Plan 034 does not implement:

```text
calibrated dBA / SPL measurements
noise exposure warnings
audio recording
audio playback
voice recognition
speech-to-text
FFT
frequency spectrum
bass / mid / treble separation
beat detection
music BPM detection
network streaming
microphone history
SD-card recording
user-editable visualizer presets
custom color themes
custom brightness
background microphone monitoring
always-on microphone
automatic display keep-awake
```

These may be considered separately after the basic microphone/audio ownership and sound-reactive rendering are physically proven.

---

# 38. Implementation Order

Implement in this order:

```text
1. Add IMicrophoneAdapter.

2. Add CardputerMicrophoneAdapter.

3. Add explicit AudioService suspend/resume lifecycle
   and IAudioAdapter::end().

4. Add MicrophoneService state machine.

5. Verify speaker → microphone → speaker handover
   on physical Cardputer-Adv before building the UI.

6. Add RMS analysis and normalized level.

7. Add attack/release smoothing and ambient baseline.

8. Add deterministic unit tests for DSP.

9. Add green → yellow → red mapping.

10. Add SOUND Mini App with LCD animation.

11. Add IndicatorService SOUND owner policy @ 3%.

12. Add foreground 8×8 animation.

13. Add Mini App registration and main-loop composition.

14. Add failure/recovery tests.

15. Run focused tests throughout implementation.

16. Run final:
    make check

17. Perform physical acceptance:
    microphone
    animation
    LED
    repeated audio handover
    Pomodoro arbitration.

18. Update:
    docs/ARCHITECTURE.md
    docs/UI_REQUIREMENTS.md
    docs/plans/README.md
```

The critical milestone is step 5.

Do not build the visualizer first and discover afterward that audio hardware ownership is unstable.

---

# 39. Definition of Done

Plan 034 is complete when:

```text
SOUND appears in Launcher

opening SOUND activates microphone capture

quiet sound produces slow green motion

increasing sound accelerates animation

color continuously transitions:
green → yellow → red

sharp sounds react quickly

decay back to quiet state is smooth

LCD and 8×8 animations respond to the same normalized level

LED never exceeds 3% effective brightness

SOUND temporarily overrides background LED users correctly

closing SOUND releases its claim

closing SOUND stops microphone

speaker functionality returns automatically

repeated open/close does not break audio

microphone startup failure recovers speaker

no audio is recorded or transmitted

automated tests pass

make check passes

physical Cardputer-Adv acceptance passes
```
