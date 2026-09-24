# 034 — LED Gallery

Status: **Software implemented; physical Unit Puzzle acceptance pending**

Suggested branch:

```text
feat/034-led-gallery
```

Suggested PR:

```text
[034] Add LED Gallery mini app
```

Suggested plan file:

```text
docs/plans/034-led-gallery.md
```

---

# 0. Goal

Add a production Mini App:

```text
LED GALLERY
```

for the optional 8×8 Unit Puzzle RGB matrix.

The app exists purely to display attractive procedural LED animations.

The intended usage is:

```text
open LED GALLERY
    ↓
choose an effect
    ↓
leave it running
    ↓
watch it indefinitely
```

There is **no automatic effect rotation**.

The selected effect continues indefinitely until the user explicitly changes it or exits the app.

No effect timeout.

No slideshow.

No automatic next effect.

No random automatic switching.

---

# 1. User Experience

Launcher entry:

```text
LED GALLERY
```

Internal app id:

```text
led-gallery
```

On activation:

```text
LED GALLERY
    ↓
restore last effect selected during this firmware session
    ↓
or effect #1 on first activation
    ↓
acquire foreground LED claim
    ↓
start animation
```

The app instance survives while firmware is running, so closing and reopening the app restores the previously selected effect.

Persistence across reboot is not required in Plan 034.

After reboot:

```text
effect #1
```

is selected.

---

# 2. Controls

Global controls:

```text
←
previous effect

→
next effect

1–9
select effects 1–9 directly

0
select effect 10

Escape
close LED GALLERY
```

`←` from effect #1 wraps to effect #10.

`→` from effect #10 wraps to effect #1.

There is no pause control.

Effects are intended to keep moving continuously.

## Space

`Space` is reserved for effect-specific interaction.

For effects without interactive behavior:

```text
Space
→ no effect
```

It must not unexpectedly switch effects, pause the gallery, or change global state.

---

# 3. The Ten Effects

## 1 — PLASMA

Classic procedural RGB plasma.

Visual character:

```text
smooth moving color fields
multiple overlapping sine waves
slow hue cycling
no obvious repeating direction
```

Conceptually:

```text
wave X
+
wave Y
+
radial wave
+
time phase
    ↓
HSV hue
    ↓
RGB pixel
```

Every pixel participates.

Movement should be smooth rather than fast.

Initial target:

```text
cycle character:
~8–15 seconds

frame rate:
~20 FPS
```

This should be one of the most colorful effects in the gallery.

---

## 2 — LAVA

Several soft metaball-like blobs drift around the 8×8 matrix.

Visual character:

```text
organic
slow
liquid
rounded
```

Approximately:

```text
3–4 invisible moving sources
```

Each pixel calculates its contribution from those sources.

Nearby sources merge visually:

```text
blob + blob
→ larger organic shape
```

Palette:

```text
purple
magenta
orange
red
```

or similarly rich warm combinations.

Movement must not depend on frame count.

Blob position derives from elapsed-time phases.

---

# 3 — KALEIDOSCOPE

A colorful, continuously morphing geometric pattern centered on the 8×8 matrix.

For each pixel, derive center-relative radius and angle, rotate the angular
orientation slowly, and mirror the angle into six symmetric sectors. Combine
radial and folded-angular waves with different elapsed-time speeds. The moving
orientation, breathing radial frequency, and secondary wave must change the
geometry as well as the colors. Map the result through a smoothly drifting HSV
palette. The pattern should show obvious symmetry and strong color variation,
without looking like a moving gradient or settling into a short static loop.

This is a pure procedural effect with no particle pool, allocation, or
catch-up simulation. The same seed and elapsed timeline produce the same frame.

---

# 4 — AURORA

Soft flowing ribbons crossing the matrix.

Visual character:

```text
slow
calm
wide
layered
```

Suggested palette:

```text
cyan
green
blue
violet
```

Use approximately:

```text
2–3 flowing bands
```

with different:

```text
phase
speed
width
vertical offset
```

Unlike PLASMA, most pixels remain relatively dark and ribbons travel through them.

This should be one of the best effects for leaving on for a long time.

---

# 5 — WARP

Starfield / hyperspace effect.

Visual model:

```text
center
  ↓
stars spawn near center
  ↓
move outward
  ↓
accelerate
  ↓
fade/reset at edge
```

Use a fixed particle pool.

Suggested:

```text
10–16 stars
```

Each star stores:

```text
x
y
velocity
age
color
```

Stars leave short trails.

Color may gradually vary between:

```text
blue
cyan
white
violet
```

Do not simulate a full 3D scene.

A simple radial velocity model is sufficient.

---

# 6 — COMETS

Several colored comets continuously travel across the matrix.

Each comet:

```text
bright head
+
2–4 pixel fading tail
```

Movement directions may include:

```text
horizontal
vertical
diagonal
```

Comets enter from an edge and leave through another.

There should usually be approximately:

```text
2–4 active comets
```

at a time.

Colors are independently selected from a rich palette.

The animation should feel energetic without becoming random pixel noise.

---

# 7 — FIREFLIES

Sparse glowing particles wander slowly through darkness.

Visual character:

```text
mostly dark
few moving lights
smooth brightness pulses
```

Maintain approximately:

```text
5–8 fireflies
```

Each firefly owns:

```text
position
velocity
phase
hue
```

Movement uses very low velocity with gradual direction drift.

Brightness follows an independent slow pulse.

Some fireflies may occasionally fade almost completely before becoming visible again.

Colors:

```text
yellow
warm green
cyan
```

This should be another long-running calm mode.

---

# 8 — VORTEX

Particles orbit a moving central attractor.

Visual concept:

```text
particles
   ↘
    spiral
      ↘
       center
```

Particles gradually spiral inward.

When a particle reaches the center:

```text
respawn near edge
```

The center itself may drift slightly in a small circle.

Use:

```text
12–20 particles
```

with different:

```text
radius
angular velocity
radial velocity
hue
```

The result should resemble:

```text
galaxy
black hole
energy vortex
```

rather than random motion.

---

# 9 — RIPPLE

Interactive water/ripple animation.

This is the first keyboard-reactive effect.

Without interaction it must still look good:

```text
occasional subtle ambient ripple
```

but ambient events do not switch effects.

## Space interaction

Press:

```text
Space
```

to inject a strong ripple.

Each press creates:

```text
origin
radius = 0
energy
age
```

The wave expands outward:

```text
radius += elapsed × velocity
```

and fades over time.

Several ripples may coexist.

Maximum simultaneous ripples must be bounded, for example:

```text
4
```

If the pool is full:

```text
replace oldest ripple
```

No dynamic allocation.

### Ripple position

For manual `Space` activation, choose the origin deterministically from the current seed/sequence so repeated presses create waves from different matrix positions.

Each ripple may use a different hue.

Overlapping waves combine visually.

---

# 10 — PARTICLE STORM

Second keyboard-reactive effect.

Without input:

```text
a small number of particles drift through the matrix
```

so the effect remains interesting when left alone.

Printable keyboard input injects energy into the system.

Reserved controls:

```text
0–9
← →
Escape
Space
```

retain their gallery meanings.

Other printable keys, including R/r, may trigger particle bursts.

Example:

```text
A
→ small cyan burst

K
→ small magenta burst

?
→ orange burst
```

Do not hardcode a color for every keyboard key.

Instead derive:

```text
key character
    ↓ hash
position
color
direction
```

so different keys naturally generate different-looking bursts.

## Space interaction

`Space` triggers a larger central burst.

Suggested:

```text
8–12 particles
```

Particles:

```text
move
slow down
fade
eventually disappear
```

Use a fixed bounded particle pool.

If full:

```text
replace oldest / weakest particles
```

No heap allocation during animation.

---

# 4. Effect Registry

Effects must have stable IDs:

```text
0  PLASMA
1  LAVA
2  KALEIDOSCOPE
3  AURORA
4  WARP
5  COMETS
6  FIREFLIES
7  VORTEX
8  RIPPLE
9  PARTICLE STORM
```

Suggested enum:

```cpp
enum class LedGalleryEffect : std::uint8_t {
    Plasma,
    Lava,
    Kaleidoscope,
    Aurora,
    Warp,
    Comets,
    Fireflies,
    Vortex,
    Ripple,
    ParticleStorm,
    Count,
};
```

Keep metadata centrally:

```cpp
struct LedGalleryEffectInfo {
    LedGalleryEffect id;
    const char* name;
};
```

Do not scatter:

```text
effect == 3
effect == 7
```

through application code.

---

# 5. Architecture

Use the existing LED ownership model.

Target:

```text
LedGalleryApp
      │
      ├── input/navigation
      │
      └── LedGalleryEngine
                │
                ├── effect state
                ├── effect update
                ├── frame generation
                └── RNG
                         ↓
                  IndicatorClaim
                         ↓
                  IndicatorService
                         ↓
                    ILEDAdapter
                         ↓
               PuzzleWs2812Adapter
```

No new LED hardware abstraction.

No direct WS2812 access.

No new LED service parallel to `IndicatorService`.

---

# 6. Responsibilities

## LedGalleryApp

Owns:

```text
Mini App lifecycle
keyboard controls
current effect selection
LCD presentation
IndicatorService foreground claim
```

Does not own:

```text
raw LED hardware
individual effect simulation math
```

## LedGalleryEngine

Owns:

```text
effect states
effect initialization on selection
procedural animation
particle pools
simulation timing
RGB frame generation
```

Does not know about:

```text
AppRegistry
navigation
LCD
IndicatorService claim ownership
```

The engine simply produces:

```cpp
services::IndicatorFrame
```

from:

```text
selected effect
elapsed time
optional interaction event
```

---

# 7. Effect Implementation Structure

Avoid creating ten unrelated Mini Apps.

Prefer:

```text
src/apps/led_gallery/
    led_gallery_app.h
    led_gallery_app.cpp

    led_gallery_engine.h
    led_gallery_engine.cpp

    led_gallery_effects.h
    led_gallery_effects.cpp

    led_gallery_graphics.h
    led_gallery_graphics.cpp
```

If `led_gallery_effects.cpp` becomes too large, it may later be split by effect.

For Plan 034, keeping the effect implementations together is acceptable if boundaries remain clear.

---

# 8. Fixed State — No Animation Heap Churn

Do not allocate vectors/objects every frame.

Use bounded structures such as:

```cpp
std::array<Firefly, 8>
std::array<Star, 16>
std::array<Particle, 24>
std::array<Ripple, 4>
```

The effect engine may allocate its complete state once during construction.

Animation update/render paths should perform:

```text
0 dynamic allocations
```

---

# 9. Randomness

Add a tiny deterministic PRNG local to LED Gallery.

For example:

```text
xorshift32
```

or equivalent.

Requirements:

```text
fast
deterministic
no heap
easy to seed
```

The initial app session may derive its first seed from an existing monotonic source.

Tests should be able to provide a known seed.

Do not use randomness directly inside tests without seed control.

---

# 10. Timing

The global UI scheduler currently provides updates roughly every:

```text
20 ms
```

LED Gallery must still maintain its own rendering cadence.

Target output:

```text
~20 FPS
```

Suggested LED interval:

```text
50 ms
```

Some simulations may use a slower internal cadence while the renderer remains smooth.

Animation state must advance using:

```text
elapsed time
```

not:

```text
frame number
```

Do not implement:

```cpp
phase += 0.1f;
```

per update.

Use:

```cpp
phase += elapsedSeconds * speed;
```

---

# 11. Large Elapsed Values

A suspended or delayed frame must not cause a burst of catch-up LED writes.
Procedural effects, including KALEIDOSCOPE, derive their current frame directly
from elapsed time. Stateful particle and ripple aging may cap the simulated
delta to keep positions bounded. The 50 ms output cadence publishes at most one
current frame per update and retains only the interval remainder.

---

# 12. LED Ownership

On activation:

```text
IndicatorService::acquire(
    ledGalleryIndicatorOwner,
    ForegroundApplication
)
```

Suggested owner:

```cpp
inline constexpr char ledGalleryIndicatorOwner[] =
    "led-gallery";
```

On deactivation:

```text
release claim
```

This means:

```text
Pomodoro background LED
    ↓
open LED GALLERY
    ↓
LED GALLERY visible
    ↓
close LED GALLERY
    ↓
latest Pomodoro frame returns
```

Do not manually restore old frames.

Let `IndicatorService` arbitration resolve them.

Higher-priority:

```text
Notification
Warning
Critical
```

claims must continue to override LED Gallery normally.

---

# 13. Brightness

LED Gallery is intended for continuous ambient viewing.

Use:

```text
maximum effective LED brightness = 3%
```

through `IndicatorService`.

Add owner policy:

```cpp
ledGalleryIndicatorOwner
    → 3%
```

Effect code emits logical full-range RGB.

Effects must not manually multiply every RGB channel by:

```text
0.03
```

Brightness remains infrastructure-owned.

---

# 14. Color System

The LCD continues to use the normal Cardputer Hub UI palette.

The external 8×8 LED matrix is different: the generated color itself is the content of this Mini App.

Plan 034 therefore explicitly allows LED Gallery effects to use:

```text
full RGB
HSV hue generation
procedural palettes
smooth hue transitions
```

This is a scoped exception for:

```text
LED GALLERY matrix content
```

It does not change the semantic LCD color rules elsewhere in Cardputer Hub.

---

# 15. LCD Presentation

The LCD is secondary.

Do not duplicate the LED animation on the LCD.

Resting screen:

```text
LED GALLERY

03
KALEIDOSCOPE
```

plus small controls:

```text
→ NEXT EFFECT
1–0 DIRECT
```

The right arrow is drawn with LCD pixels next to `NEXT EFFECT` because the
current Font0 text path does not require a Unicode arrow glyph. RIPPLE adds
`SPACE RIPPLE`; PARTICLE STORM adds `SPACE BURST`; other effects show no Space
hint. The contextual action sits to the right of `1-0 DIRECT` on the same LCD
row, for example `1-0 DIRECT    SPACE RIPPLE`. Both control rows sit at the
bottom of the 240×135 LCD, with about six pixels below the lower text row;
there is no reserved empty third row when Space is inactive. There is no reset hint.

No persistent:

```text
ESC BACK
```

footer is required because Mini Apps already use global Escape behavior.

## Effect switch feedback

After switching effects:

```text
03
KALEIDOSCOPE
```

is immediately updated.

The LCD itself does not need continuous animation.

This keeps CPU/display work low and makes the external LED panel the visual focus.

---

# 16. Display Power

LED Gallery does not count animation as user activity.

Existing display idle policy remains unchanged.

Therefore:

```text
open LED GALLERY
do nothing
    ↓
LCD dims
    ↓
LCD eventually turns off
```

while:

```text
8×8 LED animation continues
```

This is desirable.

The user can therefore leave:

```text
KALEIDOSCOPE
AURORA
PLASMA
...
```

running as an ambient desk display with the Cardputer LCD off.

Any real key press follows the existing global display-wake policy.

A wake-only key must not:

```text
change effect
reset effect
spawn particles
spawn ripple
```

until the next actual input event is delivered normally by the shell.

Do not add a DisplayWakeLock.

---

# 17. Effect Switching

When switching:

```text
PLASMA
→ KALEIDOSCOPE
```

the new effect resets to a clean initial state.

Do not retain stale internal state for every effect.

Only the currently selected effect needs active runtime state.

Switch flow:

```text
input
    ↓
change effect id
    ↓
reset selected effect
    ↓
render initial frame
```

The gallery does not crossfade between effects in Plan 034.

Immediate switching is acceptable.

---

# 18. Effect Initialization

The selected effect resets when the user switches to a different effect. Closing
and reopening the Mini App restores the selected effect within the same firmware
session and starts its animation from the initial state. The initial seed may
influence procedural phase and particle placement; tests can inject a known seed.
There is no reset or reseed key. `R` and `r` have no special gallery meaning;
within PARTICLE STORM they are ordinary printable keys and may create a small
key-derived burst.

---

# 19. Keyboard Interaction Rules

Order of handling inside LED Gallery:

```text
Escape
→ handled globally by shell / close app

Left / Right
→ effect navigation

0–9
→ direct effect selection

Space
→ effect-specific interaction

other printable keys
→ only Particle Storm may consume them
```

RIPPLE must not steal letters.

PARTICLE STORM may consume unreserved printable characters.

Modifiers should not accidentally trigger effects.

Use plain key events only unless explicitly required.

---

# 20. No Automatic Effect Switching

Hard invariant:

```text
time passing
must never change currentEffect
```

The only paths allowed to modify selected effect are:

```text
Left
Right
0–9
```

Not:

```text
timer
random event
animation completion
app idle time
display dim
display off
```

Add an explicit automated test for this.

Example:

```text
select AURORA
advance simulated time by 24 hours
current effect == AURORA
```

---

# 21. Suggested Files

New:

```text
src/apps/led_gallery/
    led_gallery_app.h
    led_gallery_app.cpp
    led_gallery_engine.h
    led_gallery_engine.cpp
    led_gallery_effects.h
    led_gallery_effects.cpp
    led_gallery_graphics.h
    led_gallery_graphics.cpp
```

Potentially modified:

```text
src/main.cpp

src/apps/launcher/assets/app_icons.h

src/services/indicator/
    indicator_service.h
    indicator_service.cpp

main/CMakeLists.txt

docs/ARCHITECTURE.md
docs/UI_REQUIREMENTS.md
docs/manuals/device-guide.md
docs/plans/README.md
```

Tests:

```text
test/test_led_gallery/
    test_main.cpp
```

---

# 22. App Registration

Register:

```text
id:
led-gallery

displayName:
LED GALLERY

iconId:
led-gallery

entryRoute:
led-gallery

requiredCapabilities:
none
```

The Mini App must be available even when Unit Puzzle hardware is absent.

The firmware currently treats LED hardware as an optional output path.

Absence of the matrix must not crash or block navigation.

---

# 23. BDD Scenarios

### Scenario 1 — Open gallery

Given:

```text
LED GALLERY inactive
```

When:

```text
user opens LED GALLERY
```

Then:

```text
one ForegroundApplication indicator claim is acquired
effect #1 is selected on first session
animation begins
LCD identifies PLASMA
```

---

### Scenario 2 — Leave one effect running

Given:

```text
AURORA selected
```

When:

```text
10 minutes pass
```

Then:

```text
AURORA remains selected
animation continues
no automatic effect switch occurs
```

The same must remain true for arbitrarily large elapsed test intervals.

---

### Scenario 3 — Next / previous

Given:

```text
KALEIDOSCOPE selected
```

When:

```text
Right
```

Then:

```text
AURORA selected
AURORA state is reset
```

When:

```text
Left
```

Then:

```text
KALEIDOSCOPE selected
KALEIDOSCOPE starts from a new initial state
```

---

### Scenario 4 — Wrap around

Given:

```text
PLASMA selected
```

When:

```text
Left
```

Then:

```text
PARTICLE STORM selected
```

Given:

```text
PARTICLE STORM selected
```

When:

```text
Right
```

Then:

```text
PLASMA selected
```

---

### Scenario 5 — Direct numeric selection

When:

```text
1
```

Then:

```text
PLASMA
```

When:

```text
9
```

Then:

```text
RIPPLE
```

When:

```text
0
```

Then:

```text
PARTICLE STORM
```

---

### Scenario 6 — No reset key

Given:

```text
KALEIDOSCOPE selected and animating
```

When:

```text
R or r is pressed
```

Then:

```text
KALEIDOSCOPE remains selected
its seed, phase, and frame are not reset
```

In PARTICLE STORM, those printable letters may still create the same small
key-derived burst as other unreserved letters.

---

### Scenario 7 — Ripple interaction

Given:

```text
RIPPLE selected
```

When:

```text
Space
```

Then:

```text
one strong ripple is inserted
animation continues normally
effect remains RIPPLE
```

Repeated Space presses:

```text
never exceed ripple pool capacity
```

---

### Scenario 8 — Particle Storm keyboard interaction

Given:

```text
PARTICLE STORM selected
```

When:

```text
user presses A
```

Then:

```text
particle burst is injected
effect remains PARTICLE STORM
```

When:

```text
user presses Space
```

Then:

```text
larger burst is injected
```

---

### Scenario 9 — Other effects ignore Space

Given:

```text
PLASMA selected
```

When:

```text
Space
```

Then:

```text
effect remains PLASMA
no gallery global state changes
```

---

### Scenario 10 — Pomodoro arbitration

Given:

```text
Pomodoro background claim exists
```

When:

```text
LED GALLERY opens
```

Then:

```text
LED GALLERY becomes resolved owner
```

When:

```text
LED GALLERY closes
```

Then:

```text
latest Pomodoro frame immediately becomes resolved owner
```

---

### Scenario 11 — Higher-priority notification

Given:

```text
LED GALLERY foreground claim active
```

When:

```text
Notification/Warning/Critical claim appears
```

Then:

```text
higher-priority claim becomes visible
LED Gallery keeps advancing its effect internally
```

When higher-priority claim disappears:

```text
current LED Gallery frame returns
```

---

### Scenario 12 — LCD display turns off

Given:

```text
LED GALLERY running
```

When:

```text
global idle policy turns LCD off
```

Then:

```text
LED effect continues
effect selection remains unchanged
animation does not count as activity
```

---

### Scenario 13 — Contextual LCD controls

Given PLASMA is selected, the LCD shows a right-arrow `NEXT EFFECT` hint and
`1-0 DIRECT`, with no Space action. Selecting RIPPLE immediately adds
`SPACE RIPPLE` to the right of `1-0 DIRECT` on the same row; selecting PARTICLE
STORM shows `SPACE BURST` in that position. Selecting any other effect removes
the Space hint. `R RESET`
never appears.

---

# 24. Tests

Add deterministic tests covering:

```text
10 effects exist with stable IDs

effect names map correctly

Left / Right navigation

wrap-around

1–0 direct selection

R/r do not reset or reseed the selected effect

elapsed time never changes selected effect

PLASMA frame changes with elapsed time

LAVA frame changes with elapsed time

KALEIDOSCOPE morphs symmetrically and deterministically with elapsed time

AURORA evolves with elapsed time

WARP particle pool stays bounded

COMETS particle pool stays bounded

FIREFLIES remain bounded

VORTEX remains bounded

RIPPLE Space injects ripple

RIPPLE pool never exceeds capacity

PARTICLE STORM printable key injects particles

PARTICLE STORM Space injects larger burst

non-interactive effects ignore Space

large elapsed values do not produce unbounded catch-up

normal LCD shows a right-arrow NEXT EFFECT hint and 1–0 DIRECT

RIPPLE shows SPACE RIPPLE; PARTICLE STORM shows SPACE BURST

other effects show no Space hint

effect output always contains exactly 64 pixels

RGB values remain valid

gallery claim is ForegroundApplication

gallery brightness policy is 3%

deactivation releases the claim

background LED owner returns correctly

repeated activation/deactivation creates no claim leak
```

Effect tests should use known seeds.

Do not assert exact complete 64-pixel frames unless necessary.

Prefer behavioral invariants such as:

```text
frame changed
pool remains bounded
color range valid
same seed + same elapsed → same result
```

---

# 25. Performance Requirements

While LED Gallery is active:

```text
keyboard remains responsive
BLE remains responsive
Wi-Fi remains responsive
Pomodoro continues
no watchdog reset
no growing heap usage
```

Do not:

```text
allocate memory every frame
construct std::vector every frame
sleep/delay inside Mini App
run unbounded simulation catch-up loops
```

Target:

```text
~20 LED FPS
```

At 8×8 this should leave significant CPU headroom.

---

# 26. Physical Acceptance

Test all 10 effects on the real Unit Puzzle matrix.

For every effect:

```text
run at least ~2 minutes
verify no obvious frozen state
verify motion is smooth
verify pattern remains visually interesting
verify no distracting flicker
verify 3% brightness is comfortable
```

Specifically verify:

```text
PLASMA
→ rich smooth colors

LAVA
→ visually organic blobs

KALEIDOSCOPE
→ obvious geometric symmetry
→ continuously morphing pattern
→ strong color variation
→ smooth motion without a short repetitive static loop

AURORA
→ calm flowing ribbons

WARP
→ convincing outward movement

COMETS
→ visible heads and tails

FIREFLIES
→ sparse, smooth, calm movement

VORTEX
→ visible rotational/spiral motion

RIPPLE
→ Space produces obvious wave response

PARTICLE STORM
→ keyboard presses produce obvious bursts
```

Run:

```text
Pomodoro
→ LED GALLERY
→ Pomodoro
```

and verify arbitration.

Turn the LCD off through normal idle behavior and verify:

```text
LED Gallery continues indefinitely
```

---

# 27. Out of Scope

Plan 034 does not implement:

```text
accelerometer interaction
microphone interaction
music visualization
network-controlled effects
Web UI control
Telegram control
automatic slideshow
automatic random effect changes
effect playlists
crossfades between effects
user-created effects
persistent effect selection across reboot
brightness settings
speed settings
palette settings
custom effect configuration
LCD versions of the LED animations
```

These can be added later without changing the core effect engine.

---

# 28. Implementation Order

1. Add `LedGalleryEffect` enum and effect metadata.

2. Add deterministic gallery PRNG.

3. Add `LedGalleryEngine` and fixed effect state.

4. Implement PLASMA.

5. Implement LAVA.

6. Implement KALEIDOSCOPE.

7. Implement AURORA.

8. Implement WARP.

9. Implement COMETS.

10. Implement FIREFLIES.

11. Implement VORTEX.

12. Implement RIPPLE with `Space`.

13. Implement PARTICLE STORM with keyboard interaction.

14. Add deterministic engine/effect tests.

15. Add `LedGalleryApp`.

16. Add Left/Right, 1–0, and contextual Space handling.

17. Add minimal LCD presentation.

18. Add `led-gallery` IndicatorService brightness policy at 3%.

19. Add ForegroundApplication claim lifecycle.

20. Register the Mini App and launcher icon.

21. Add no-auto-switch regression test.

22. Add large-elapsed/performance tests.

23. Run focused tests.

24. Run:

```bash
make check
```

25. Perform physical acceptance on Unit Puzzle.

26. Tune effect constants only after physical testing.

27. Update architecture, UI requirements, device guide and plans index.

---

# 29. Definition of Done

Plan 034 is complete when:

```text
LED GALLERY appears in Apps

all 10 effects are implemented

each effect runs indefinitely

effects never switch automatically

Left/Right switch effects manually

1–0 select effects directly

R/r do not reset or reseed the current effect

LCD shows a right-arrow NEXT EFFECT hint and 1–0 DIRECT

RIPPLE responds to Space and shows SPACE RIPPLE

PARTICLE STORM responds to keyboard input and shows SPACE BURST

other effects show no Space hint

all effects remain visually active without user input

LED Gallery uses one ForegroundApplication claim

LED Gallery never bypasses IndicatorService

effective matrix brightness never exceeds 3%

closing the app releases its claim

background Pomodoro LED state returns correctly

LCD may dim/off while LED animation continues

no effect uses accelerometer

no effect uses microphone

no effect requires network access

no per-frame dynamic allocation exists

large elapsed values cannot block the firmware loop

automated tests pass

make check passes

all 10 effects pass physical Unit Puzzle acceptance
```
