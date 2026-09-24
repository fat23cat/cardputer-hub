# 034/2 — LED Gallery Expansion

Status: **Software implemented; physical Unit Puzzle acceptance and tuning pending**

Parent plan:

```text
034 — LED Gallery
```

Suggested branch:

```text
feat/034-led-gallery
```

Suggested PR:

```text
[034/2] Expand LED Gallery with interactive effects
```

Suggested plan file:

```text
docs/plans/034-2-led-gallery-expansion.md
```

---

# 0. Relationship to Plan 034

Plan 034 remains the source of truth for:

- LED Gallery Mini App lifecycle
- `IndicatorService` ownership
- `ForegroundApplication` claim
- 3% effective LED brightness
- ~20 FPS output cadence
- deterministic RNG
- fixed-size simulation state
- no per-frame heap allocation
- LCD display-power behavior
- no automatic effect switching
- effect reset on explicit effect change
- session-only selected-effect persistence
- physical Unit Puzzle acceptance

Plan 034/2 extends the existing gallery.

It does not replace the architecture introduced by Plan 034.

Primary changes:

```text
10 effects
    ↓
20 effects

simple contextual Space action
    ↓
general contextual interaction row

1–0 direct selection
    ↓
1–0 + Fn+1–0 direct selection
```

---

# 1. Goal

Expand:

```text
LED GALLERY
```

with ten additional effects that are meaningfully different from the original procedural and particle effects.

The new effects should emphasize:

```text
cellular automata
simulation
emergent behavior
simple physics
keyboard interaction
```

rather than adding ten more variations of gradients, plasma, or generic particle fields.

The gallery must remain usable as an ambient display:

```text
open effect
    ↓
leave Cardputer alone
    ↓
effect continues indefinitely
```

Interactive effects must still look interesting without input.

---

# 2. Effect Registry

The existing effects remain unchanged:

```text
01  PLASMA
02  LAVA
03  KALEIDOSCOPE
04  AURORA
05  WARP
06  COMETS
07  FIREFLIES
08  VORTEX
09  RIPPLE
10  PARTICLE STORM
```

Add:

```text
11  GAME OF LIFE
12  REACTION DIFFUSION
13  FIRE
14  GRAVITY WELL
15  SWARM
16  FALLING SAND
17  LANGTON'S ANT
18  TETRIS DREAM
19  RULE MACHINE
20  ELECTRIC STORM
```

Stable IDs become:

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

    GameOfLife,
    ReactionDiffusion,
    Fire,
    GravityWell,
    Swarm,
    FallingSand,
    LangtonsAnt,
    TetrisDream,
    RuleMachine,
    ElectricStorm,

    Count,
};
```

Existing IDs `0–9` must not change.

---

# 3. Global Navigation

Global gallery controls become:

```text
←
previous effect

→
next effect

1–9
select effects 01–09

0
select effect 10

Fn+1–Fn+9
select effects 11–19

Fn+0
select effect 20

Escape
close LED GALLERY
```

Navigation wraps across all twenty effects:

```text
01 ← 20

20 → 01
```

There is still:

```text
no automatic rotation
no slideshow
no timeout
no random effect selection
```

Time passing must never modify `currentEffect`.

---

# 4. Interaction Model

Plan 034/2 introduces a general rule:

> Effect-specific controls are visible only when they currently do something.

Do not reserve permanent LCD space for controls that do not apply.

Examples:

```text
PLASMA
→ no effect-specific controls

RIPPLE
→ SPACE RIPPLE

PARTICLE STORM
→ SPACE BURST
→ printable key bursts

FIRE
→ A/D WIND
→ W/S HEAT
→ SPACE FLASH

GRAVITY WELL
→ WASD MOVE
→ SPACE REPULSE
```

`Space` retains the meaning established by Plan 034:

```text
Space
→ effect-specific primary interaction
```

If an effect has no Space action:

```text
the word SPACE must not appear on its resting LCD UI
```

---

# 5. LCD Layout

The current minimal visual character must be preserved.

The LCD remains secondary to the external 8×8 matrix.

Do not:

```text
mirror the LED animation
fill the empty center area with decorative UI
show every possible keyboard command
turn LED Gallery into a settings screen
```

## 5.1 Resting layout

Target structure:

```text
LED GALLERY


14/20       GRAVITY WELL




WASD MOVE         SPACE REPULSE
←/→ EFFECT        1-0 / FN+1-0
```

For passive effects:

```text
LED GALLERY


04/20       AURORA




←/→ EFFECT        1-0 / FN+1-0
```

The empty central area is intentional.

---

# 6. Effect Number

Replace:

```text
10
```

with:

```text
10/20
```

for all effects.

Examples:

```text
01/20
09/20
10/20
11/20
20/20
```

The purpose is to communicate gallery position without adding another label.

Do not add:

```text
PAGE 1
PAGE 2
EFFECTS 1-10
EFFECTS 11-20
```

---

# 7. Persistent Global Hint Row

The bottommost control row is global and stable:

```text
←/→ EFFECT        1-0 / FN+1-0
```

Exact spacing may be tuned on the real 240×135 LCD.

The row communicates:

```text
Left/Right
→ sequential navigation

1–0
→ effects 01–10

Fn+1–0
→ effects 11–20
```

Do not require two-digit numeric entry such as:

```text
1 then 4
→ effect 14
```

Do not use:

```text
Shift+digit
double tap
numeric entry mode
effect pages
```

`Fn+digit` is the only second-bank direct-selection mechanism.

---

# 8. Contextual Action Row

The row above the global controls belongs entirely to the selected effect.

It may contain:

```text
0 actions
1 action
2 actions
3 short actions
```

depending on the effect.

It must never display controls that are inactive.

Examples:

## PLASMA

```text
←/→ EFFECT        1-0 / FN+1-0
```

No second row.

## RIPPLE

```text
SPACE RIPPLE
←/→ EFFECT        1-0 / FN+1-0
```

## PARTICLE STORM

```text
KEY BURST         SPACE BURST
←/→ EFFECT        1-0 / FN+1-0
```

## FIRE

```text
A/D WIND   W/S HEAT   SPACE FLASH
←/→ EFFECT        1-0 / FN+1-0
```

## GRAVITY WELL

```text
WASD MOVE         SPACE REPULSE
←/→ EFFECT        1-0 / FN+1-0
```

## GAME OF LIFE

```text
SPACE ADD         G NEW WORLD
←/→ EFFECT        1-0 / FN+1-0
```

Labels should describe actions rather than implementation terminology.

Prefer:

```text
SPACE REPULSE
```

over:

```text
SPACE APPLY NEGATIVE GRAVITY
```

---

# 9. Transient Feedback Area

The large central LCD area remains empty during normal resting operation.

Interactive effects may use a small part of it for temporary feedback after user input.

Examples:

```text
WIND → 3
```

```text
HEAT 7
```

```text
RULE 90
```

```text
FEED 0.037
```

```text
17 ALIVE
```

Transient feedback:

```text
appears immediately after interaction
remains visible approximately 0.7–1.5 seconds
disappears automatically
does not block input
does not count as a modal screen
```

It must not prevent the normal global LCD idle/dim/off behavior.

The transient UI timeout affects only the LCD label.

It must never change:

```text
selected effect
simulation state
LED ownership
```

---

# 10. Optional Persistent Effect Status

Some simulations naturally expose meaningful live state.

A very small status label may remain visible continuously if it adds clear value.

Examples:

```text
GAME OF LIFE
GEN 148 · 17 ALIVE
```

```text
RULE MACHINE
RULE 90
```

```text
FALLING SAND
GRAVITY ↓
```

Persistent status is optional and effect-specific.

Do not show technical debugging information such as:

```text
dt
FPS
heap
particle pool size
RNG state
simulation tick
```

---

# 11. Effect 11 — GAME OF LIFE

Implement Conway's Game of Life on the physical 8×8 field.

Grid:

```text
8 × 8
64 cells
```

Classic rule:

```text
B3/S23
```

Each simulation step derives the next generation from the current generation.

## Visual treatment

Do not use only:

```text
alive = white
dead = black
```

Living cells should change hue according to age.

Suggested character:

```text
newborn
→ cyan

young
→ green

mature
→ yellow

old
→ orange/red
```

Dead cells are dark.

## Autonomous behavior

The effect must remain useful unattended.

If the simulation:

```text
dies completely
or
becomes unchanged for a bounded number of generations
or
enters a detected short trivial cycle
```

the engine may automatically seed a new world.

This is simulation recovery.

It does not count as automatic effect switching.

## Controls

```text
Space
→ inject a small living cluster

G
→ generate a new world
```

Optional persistent status:

```text
GEN 148 · 17 ALIVE
```

---

# 12. Effect 12 — REACTION DIFFUSION

Implement a small bounded reaction-diffusion simulation inspired by the Gray-Scott model.

Maintain two scalar fields:

```text
A[64]
B[64]
```

No dynamic allocation.

The simulation should produce:

```text
spots
splits
merging structures
organic moving boundaries
```

rather than simple animated noise.

## Controls

```text
Space
→ inject reagent B around a small region

W/S
→ adjust feed parameter

A/D
→ adjust kill parameter
```

Parameters must remain clamped to safe predefined ranges.

Temporary LCD feedback:

```text
FEED 0.037
```

or:

```text
KILL 0.061
```

Do not expose arbitrary floating-point configuration menus.

---

# 13. Effect 13 — FIRE

Implement a cellular fire / heat simulation.

Concept:

```text
heat source near bottom
    ↓
heat rises
    ↓
neighbor diffusion
    ↓
cooling
    ↓
colored flame
```

Palette should naturally move through something similar to:

```text
black
deep red
orange
yellow
pale hot center
```

Do not implement FIRE as a vertically scrolling precomputed gradient.

## Controls

```text
A
→ increase wind toward left

D
→ increase wind toward right

W
→ increase fire intensity

S
→ decrease fire intensity

Space
→ inject a strong temporary flare
```

Wind should influence horizontal heat propagation.

Transient status examples:

```text
WIND ← 2
WIND → 3
HEAT 6
```

The effect remains active without input.

---

# 14. Effect 14 — GRAVITY WELL

Simulate a bounded collection of glowing particles influenced by an attractor.

Suggested fixed pool:

```text
16–24 particles
```

Each particle stores:

```text
position
velocity
hue
age / lifetime if required
```

A gravity point exists in matrix coordinates.

For each simulation step:

```text
particle position
    ↓
vector to attractor
    ↓
bounded acceleration
    ↓
velocity
    ↓
new position
```

Use safe clamps to avoid numerical explosions near the attractor.

Particles may:

```text
orbit
slingshot
spiral
escape and respawn
```

## Controls

```text
WASD
→ move gravity well

Space
→ temporary repulsion pulse
```

The repulsion pulse must be visually obvious but bounded.

No real N-body simulation is required.

Particles do not need to attract each other.

---

# 15. Effect 15 — SWARM

Implement a simplified flocking / swarm simulation.

Suggested fixed pool:

```text
10–18 agents
```

Approximate behaviors:

```text
separation
alignment
cohesion
target attraction
```

Full desktop-quality boids simulation is unnecessary.

The target can slowly drift automatically while unattended.

## Controls

```text
WASD
→ move target

Space
→ scare / scatter swarm
```

After scatter:

```text
agents separate rapidly
    ↓
gradually recover
    ↓
form swarm again
```

This should make the effect enjoyable both passively and interactively.

---

# 16. Effect 16 — FALLING SAND

Implement a true discrete 8×8 cellular falling-sand simulation.

Each LED corresponds directly to one simulation cell.

Basic material for 034/2:

```text
SAND
```

Sand behavior:

```text
fall down
else fall down-left
else fall down-right
else remain
```

Movement order must avoid directional bias as much as practical.

## Controls

```text
Space
→ add sand near the gravity-upstream edge

A
→ rotate gravity counter-clockwise

D
→ rotate gravity clockwise
```

Supported gravity directions may be discrete:

```text
↓
←
↑
→
```

Transient status:

```text
GRAVITY ↓
```

The autonomous effect adds sand near the gravity-upstream edge. Grains remain
on the board as the pile grows. When all 64 cells are occupied, the whole pile
fades out, clears, and starts filling again. Do not remove individual grains
from settled rows to make room for new ones.

Future materials such as:

```text
water
fire
smoke
```

are explicitly out of scope for 034/2.

---

# 17. Effect 17 — LANGTON'S ANT

Implement Langton's Ant or a closely related bounded cellular automaton.

Each ant stores:

```text
x
y
direction
hue / identity if multiple ants are supported
```

Classic behavior:

```text
on one cell state
→ turn right

on other cell state
→ turn left

flip cell
move forward
```

The 8×8 world should use a defined edge policy.

Preferred:

```text
toroidal wrap
```

so ants do not disappear.

## Controls

```text
Space
→ add another ant
```

Optional:

```text
A/D
→ cycle between a small predefined set of turn rules
```

Do not expose arbitrary rule-string editing.

Suggested maximum ants:

```text
4
```

No heap allocation.

---

# 18. Effect 18 — TETRIS DREAM

Create an autonomous Tetris-inspired animation.

It is not intended to become a full game.

The effect should:

```text
spawn tetromino
move downward
place piece
clear completed rows
continue automatically
```

Autonomous behavior chooses movement/rotation using a simple bounded heuristic or deterministic strategy.

The user may temporarily influence the active piece.

## Controls

```text
A/D
→ move piece left/right

W
→ rotate

S
→ soft drop

Space
→ hard drop
```

After interaction stops, autonomous behavior resumes.

No:

```text
score screen
game-over menu
level system
pause menu
high score
competitive gameplay
```

If the board reaches an unrecoverable state:

```text
brief full-board flash
    ↓
clear board
    ↓
continue
```

---

# 19. Effect 19 — RULE MACHINE

Implement an elementary one-dimensional cellular automaton rendered over time.

A row generates the next row according to a selected Wolfram rule.

Include a small predefined set such as:

```text
Rule 30
Rule 54
Rule 90
Rule 110
Rule 150
```

The display scrolls generations through the 8×8 matrix.

Different cell ages or generations may use smoothly changing hues.

## Controls

```text
A/D
→ previous / next predefined rule

Space
→ generate a new initial row

W/S
→ increase / decrease generation speed
```

Persistent or transient status:

```text
RULE 90
```

Do not support numeric arbitrary rule entry in 034/2.

---

# 20. Effect 20 — ELECTRIC STORM

Create an animated electric-field / lightning effect.

Maintain a small bounded set of charge points.

Suggested:

```text
2–4 charges
```

Charges drift slowly while unattended.

Render:

```text
bright charge nodes
short branching electrical paths
brief lightning events
fading afterglow
```

The appearance should emphasize:

```text
electrical arcs
branching
flashes
```

rather than generic random particles.

## Controls

```text
WASD
→ move the primary charge

Space
→ force a strong lightning discharge
```

The discharge must remain bounded and must not create excessive rapid full-matrix flashing.

---

# 21. Input Precedence

Input handling order becomes:

```text
Escape
→ handled by shell

Left / Right
→ previous / next effect

Fn + 0–9
→ direct select effects 11–20

plain 0–9
→ direct select effects 01–10

effect-specific keys
→ current effect interaction
```

Effect-specific interactions must never intercept:

```text
Escape
Left
Right
plain 0–9
Fn+0–9
```

This means that effects using:

```text
WASD
G
Space
```

may consume those keys only after global gallery navigation has been resolved.

---

# 22. Fn Handling

The existing gallery currently treats Fn-modified printable input as non-effect input.

Plan 034/2 intentionally changes this only for:

```text
Fn + digit
```

Recognized mappings:

```text
Fn+1 → effect 11
Fn+2 → effect 12
...
Fn+9 → effect 19
Fn+0 → effect 20
```

Other Fn-modified keys remain ignored unless a future plan explicitly assigns them.

Do not pass:

```text
Fn+letter
```

into effect-specific controls.

---

# 23. Interaction Metadata

Do not hardcode LCD hint decisions throughout `LedGalleryApp`.

Extend central effect metadata.

Conceptually:

```cpp
struct LedGalleryEffectInfo {
    LedGalleryEffect id;
    const char* name;

    const char* actionHintLeft;
    const char* actionHintCenter;
    const char* actionHintRight;
};
```

Exact representation may differ if a cleaner typed approach fits the existing codebase.

Important requirement:

```text
effect identity
effect name
effect contextual control metadata
```

must remain centralized.

Avoid:

```cpp
if (effect == Fire) draw("A/D WIND...");
if (effect == GravityWell) draw("WASD MOVE...");
```

spread across unrelated UI code.

Dynamic status values such as:

```text
WIND → 3
RULE 90
GEN 148
```

remain runtime state and are not static metadata.

---

# 24. Engine Responsibilities

`LedGalleryEngine` continues to own:

```text
simulation state
effect initialization
effect timing
interaction effects
frame generation
deterministic RNG
```

Extend interaction handling beyond a raw single `Space`/character if needed.

Prefer a semantic internal interaction representation if the original `interact(char)` becomes awkward.

For example:

```cpp
enum class LedGalleryAction {
    Primary,
    Up,
    Down,
    LeftAction,
    RightAction,
    Character,
};
```

or an equivalent small input structure.

The app remains responsible for:

```text
keyboard interpretation
global gallery navigation
LCD hints
transient LCD feedback
```

The engine must not depend on:

```text
LCD
AppRegistry
shell navigation
IndicatorService ownership
```

---

# 25. Fixed State

All new effects must obey the existing no-animation-heap-churn requirement.

Examples:

```cpp
std::array<Cell, 64>
std::array<float, 64>
std::array<Particle, 24>
std::array<Boid, 16>
std::array<Ant, 4>
```

No:

```text
std::vector growth per frame
new/delete per frame
dynamic particle allocation
unbounded queues
recursive lightning structures with heap allocation
```

---

# 26. Timing

The gallery output target remains:

```text
~20 FPS
```

Individual simulations may update at lower logical rates.

Examples:

```text
GAME OF LIFE
~4–8 generations/sec

LANGTON'S ANT
multiple bounded ant steps/sec

RULE MACHINE
~2–10 rows/sec

TETRIS DREAM
piece motion slower than LED render cadence
```

Rendering and simulation cadence need not be identical.

Stateful effects must still handle large elapsed values without an unbounded catch-up loop.

---

# 27. Autonomous Requirement

Every new effect must remain visually alive without keyboard input.

This is a hard gallery requirement.

Examples:

```text
GAME OF LIFE
→ reseed if dead/stuck

REACTION DIFFUSION
→ simulation evolves

FIRE
→ flame continues

GRAVITY WELL
→ particles orbit

SWARM
→ target drifts automatically

FALLING SAND
→ bounded occasional autonomous material input if needed

LANGTON'S ANT
→ ants continue walking

TETRIS DREAM
→ autonomous placement

RULE MACHINE
→ generations continue

ELECTRIC STORM
→ charges drift and occasional arcs occur
```

Keyboard input enhances an effect.

Keyboard input must not be required for the effect to function.

---

# 28. No Automatic Effect Switching

The invariant from Plan 034 remains unchanged.

Even if:

```text
Game of Life world dies
Tetris board fills
Langton simulation becomes repetitive
particle system empties
```

the effect may reset or reseed its own simulation.

It must not:

```text
advance to another gallery effect
select a random gallery effect
return to effect 01
```

Simulation restart and effect selection are separate concepts.

---

# 29. LCD Wake Behavior

The existing display-wake contract remains unchanged.

If the LCD is Off:

```text
first physical key event
→ wakes LCD only
```

It must not simultaneously:

```text
change effect
move gravity well
change fire wind
drop Tetris piece
inject reagent
spawn Life cells
trigger lightning
```

The next normally delivered input event may perform the action.

LED simulation continues while LCD is Off.

---

# 30. BDD — Expanded Navigation

## Scenario 1 — Twenty effects exist

Given:

```text
LED Gallery registry initialized
```

Then:

```text
exactly 20 stable effect IDs exist
existing IDs 01–10 retain their previous mapping
new IDs 11–20 use the mappings from this plan
```

## Scenario 2 — Navigate across boundary

Given:

```text
PARTICLE STORM / 10 selected
```

When:

```text
Right
```

Then:

```text
GAME OF LIFE / 11 selected
```

When:

```text
Left
```

Then:

```text
PARTICLE STORM / 10 selected
```

## Scenario 3 — Twenty-effect wrap

Given:

```text
PLASMA / 01 selected
```

When:

```text
Left
```

Then:

```text
ELECTRIC STORM / 20 selected
```

---

# 31. BDD — Direct Selection

## Scenario 4 — Plain digits retain existing mappings

```text
1
→ PLASMA / 01

0
→ PARTICLE STORM / 10
```

## Scenario 5 — Fn digits select second bank

```text
Fn+1
→ GAME OF LIFE / 11

Fn+4
→ GRAVITY WELL / 14

Fn+0
→ ELECTRIC STORM / 20
```

## Scenario 6 — Other Fn keys remain ignored

Given:

```text
GRAVITY WELL selected
```

When:

```text
Fn+W
```

Then:

```text
gravity well does not move
selected effect does not change
```

---

# 32. BDD — Contextual UI

## Scenario 7 — Passive effect

Given:

```text
AURORA selected
```

Then LCD shows:

```text
04/20
AURORA
←/→ EFFECT
1-0 / FN+1-0
```

and does not show:

```text
SPACE
WASD
A/D
W/S
```

## Scenario 8 — Gravity Well

Given:

```text
GRAVITY WELL selected
```

Then LCD shows:

```text
14/20
GRAVITY WELL
WASD MOVE
SPACE REPULSE
```

## Scenario 9 — Fire

Given:

```text
FIRE selected
```

Then LCD shows:

```text
13/20
FIRE
A/D WIND
W/S HEAT
SPACE FLASH
```

## Scenario 10 — Context disappears after navigation

Given:

```text
FIRE selected
```

When:

```text
Right
```

Then:

```text
GRAVITY WELL controls replace FIRE controls immediately
no FIRE-specific hint remains
```

---

# 33. BDD — Transient Feedback

## Scenario 11 — Fire wind feedback

Given:

```text
FIRE selected
```

When:

```text
D pressed
```

Then:

```text
wind changes within allowed bounds
temporary LCD feedback appears
LED simulation continues
```

After feedback timeout:

```text
temporary status disappears
normal resting UI remains
```

## Scenario 12 — Rule Machine

Given:

```text
RULE MACHINE selected
```

When:

```text
D pressed
```

Then:

```text
next predefined rule becomes active
LCD identifies the selected rule
effect remains RULE MACHINE
```

---

# 34. BDD — Interactive Simulations

Add behavioral coverage for at least:

```text
GAME OF LIFE
Space changes cell state/population
G creates a new deterministic seeded world

REACTION DIFFUSION
Space changes reagent state
parameter controls remain bounded

FIRE
wind control changes simulation behavior
heat remains bounded
Space creates flare

GRAVITY WELL
WASD changes attractor position
attractor cannot leave allowed bounds
Space applies repulsion

SWARM
WASD changes target
Space increases dispersion temporarily

FALLING SAND
Space adds bounded material
gravity rotation changes fall direction

LANGTON'S ANT
ant motion follows selected rule
ant pool remains bounded

TETRIS DREAM
piece controls remain inside board
hard drop places current piece

RULE MACHINE
rule cycling uses only predefined rules
new seed row is deterministic for known RNG seed

ELECTRIC STORM
primary charge can move
forced discharge is bounded
```

---

# 35. Tests

Extend deterministic gallery tests to cover:

```text
20 stable effect IDs

original 01–10 mappings unchanged

new 11–20 names correct

Left/Right across 10↔11 boundary

wrap 01↔20

plain 1–0 direct selection unchanged

Fn+1–0 selects 11–20

Fn+letters do not trigger effect actions

LCD uses NN/20 numbering

global hint includes second direct-selection bank

passive effects show no contextual row

interactive effects expose correct contextual hints

transient status expires correctly

transient status does not affect currentEffect

all new effects produce exactly 64 pixels

all RGB output remains valid

same seed + same elapsed + same interaction sequence
→ deterministic result

all fixed pools stay within capacity

large elapsed values remain bounded

no new effect changes currentEffect internally

LCD Off does not stop LED simulations

wake-only input causes no effect interaction

existing 3% gallery brightness policy remains unchanged

existing claim arbitration remains unchanged

repeated effect changes create no heap growth attributable to frame processing
```

Do not test entire exact 64-pixel frames unless necessary.

Prefer behavioral invariants.

---

# 36. Performance

The new simulations must not compromise:

```text
keyboard responsiveness
BLE responsiveness
Wi-Fi responsiveness
Pomodoro background timing
shell update cadence
watchdog stability
```

Particularly watch:

```text
REACTION DIFFUSION
GRAVITY WELL
SWARM
ELECTRIC STORM
```

because they may perform more math per frame.

At only 64 pixels, implementations should still remain deliberately bounded.

Avoid overengineering physically accurate simulation.

Visual quality on the actual matrix is more important than mathematical fidelity.

---

# 37. Physical Acceptance

Test all twenty effects on the real Unit Puzzle.

For effects 11–20, verify:

```text
GAME OF LIFE
→ individual cells remain understandable
→ age colors are distinguishable
→ reseed does not feel like flicker

REACTION DIFFUSION
→ pattern looks organic at 8×8
→ not merely random noise

FIRE
→ upward flame motion is obvious
→ wind visibly changes behavior

GRAVITY WELL
→ attraction/orbits are visually understandable
→ WASD and repulsion are obvious

SWARM
→ group motion reads as coordinated rather than noise
→ scatter/recovery is obvious

FALLING SAND
→ falling and piling behavior is recognizable
→ rotated gravity is understandable

LANGTON'S ANT
→ cellular trail is visible
→ movement is not too fast to perceive

TETRIS DREAM
→ tetromino shapes remain recognizable
→ row clear is visually obvious

RULE MACHINE
→ different rules visibly produce different structures

ELECTRIC STORM
→ lightning reads as arcs/branches
→ flashes remain comfortable at 3%
```

Also verify the LCD layout physically:

```text
NN/20 fits cleanly

effect names fit

←/→ EFFECT fits

1-0 / FN+1-0 fits

all contextual action combinations fit

no action label touches LCD edges

empty central area remains visually balanced
```

Tune abbreviated labels if necessary.

Do not reduce font readability merely to preserve an exact proposed string.

---

# 38. Out of Scope

Plan 034/2 does not add:

```text
effect configuration menus
persistent parameters across reboot
user-defined rules
user-created effects
effect favorites
effect playlists
automatic slideshow
crossfades
accelerometer interaction
microphone interaction
network interaction
Web UI controls
Telegram controls
brightness configuration
FPS configuration
game scores
full standalone Tetris game
multi-material falling-sand sandbox
arbitrary reaction-diffusion parameter editor
arbitrary Wolfram rule entry
```

These may be considered separately later.

---

# 39. Suggested Implementation Order

1. Extend `LedGalleryEffect` from 10 to 20 while preserving IDs 0–9.
2. Extend effect metadata.
3. Change navigation/wrap logic to use `Count` rather than hardcoded `10`.
4. Implement `Fn+1–0` direct selection.
5. Change LCD number from `NN` to `NN/20`.
6. Replace the existing fixed bottom hints with a global navigation row plus an optional contextual action row.
7. Add transient-feedback support to `LedGalleryApp`.
8. Refactor interaction representation if `interact(char)` is no longer clean enough.
9. Implement GAME OF LIFE.
10. Implement REACTION DIFFUSION.
11. Implement FIRE.
12. Implement GRAVITY WELL.
13. Implement SWARM.
14. Implement FALLING SAND.
15. Implement LANGTON'S ANT.
16. Implement TETRIS DREAM.
17. Implement RULE MACHINE.
18. Implement ELECTRIC STORM.
19. Add deterministic effect tests.
20. Add expanded navigation tests.
21. Add Fn-bank tests.
22. Add contextual-LCD tests.
23. Add transient-feedback tests.
24. Add large-elapsed tests for all stateful simulations.
25. Run focused gallery tests.
26. Run:

```bash
make check
```

27. Perform physical Unit Puzzle acceptance.
28. Tune timing, palettes and simulation constants on hardware.
29. Tune LCD action-label spacing on hardware.
30. Update:

```text
ARCHITECTURE
UI_REQUIREMENTS
device guide
plans index
LED Gallery documentation
```

---

# 40. Definition of Done

Plan 034/2 is complete when:

```text
LED Gallery contains 20 stable effects

original effects 01–10 retain their IDs

new effects 11–20 are implemented

Left/Right navigate across all 20 effects

01 wraps backward to 20

20 wraps forward to 01

1–0 select effects 01–10

Fn+1–0 select effects 11–20

LCD displays NN/20

global navigation hint communicates both numeric banks

effect-specific controls appear only when applicable

SPACE is never shown for an effect without a Space action

passive effects retain the clean minimal LCD layout

interactive effects remain attractive without input

interactive effects respond immediately to their documented controls

transient feedback appears only when useful and disappears automatically

no effect changes selected effect automatically

no new effect requires network access

no new effect requires microphone input

no new effect requires accelerometer input

all simulation storage is bounded

no per-frame dynamic allocation is introduced

large elapsed values cannot create unbounded catch-up

gallery continues at the existing effective 3% LED brightness

LCD may dim/off while all effects continue

Pomodoro / IndicatorService arbitration behavior remains unchanged

automated tests pass

make check passes

all ten new effects pass physical Unit Puzzle acceptance

the final LCD layout is readable and balanced on the real Cardputer Adv
```
