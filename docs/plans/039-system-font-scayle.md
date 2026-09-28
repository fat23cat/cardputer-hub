# 039 — System Font Scale 1.10×

## Status

**Software implemented; physical display review pending.**

The user found the 1.10× visual difference too subtle, so the current firmware
uses the separate [1.20× trial](040-system-font-scale-120.md). The 1.10× captures
remain as reference; this plan's physical acceptance was not performed.

The 1.10× implementation, scaled layout metrics, bounds tests, fractional UI
capture format, and 11 paired reference captures are present. Native captures
are an approximation of the M5GFX raster. Device installation and reading-distance
review in section 27 have not yet been performed, so the decision gate in section
28 remains open. See [the capture comparison](039-captures/comparison.png).

## Goal

Slightly increase the readability of the Cardputer Hub UI by making the normal system font approximately **10% larger** across the firmware.

The first implementation target is:

```text
System Font0 scale: 1.00× → 1.10×
```

The purpose of this plan is deliberately conservative:

- make normal UI text noticeably easier to read;
- preserve the current visual density of the 240×135 display;
- avoid redesigning every screen;
- generate visual captures so the 1.10× result can be reviewed before increasing the scale further.

This plan is primarily a **typography/layout infrastructure change**, not a visual redesign.

---

## 1. Scope

Apply the new 1.10× scale to normal firmware UI text rendered through the shared display adapter.

Examples include:

- Home status labels;
- connected host name;
- Home `APPS` / `SETTINGS`;
- Launcher labels;
- Settings;
- Bluetooth / Host Settings;
- Wi-Fi Settings;
- System Mini App;
- Mac Status labels;
- Mac Control labels;
- Pomodoro labels and instructions;
- LED Gallery labels and instructions;
- contextual footer text;
- splash-screen secondary text where appropriate.

Do not blindly scale custom graphics or bitmap assets.

---

## 2. Out of Scope

Do not change:

- display resolution;
- overall UI style;
- palette;
- icons;
- Home ambient animation;
- row count unless required by a real layout regression;
- Mini App behavior;
- BLE / Wi-Fi / Companion behavior;
- LED Puzzle rendering;
- macOS Companion UI.

### Micro 5

Existing Micro 5 bitmap graphics must keep their current physical size.

This includes, where applicable:

- Bluetooth pairing digits;
- Pomodoro timer digits;
- Mac Control numeric graphics;
- other custom bitmap glyphs.

The goal is to enlarge **normal system text**, not every graphical text asset.

---

## 3. Current Problem

Normal text currently uses:

```cpp
fonts::Font0
```

with:

```cpp
TextStyle.scale = 1
```

`TextStyle.scale` is currently an integer:

```cpp
std::uint8_t scale;
```

The project also assumes Font0 metrics in several places:

```text
glyph width  = 6 px
glyph height = 8 px
```

Examples include:

- right alignment;
- centered labels;
- Home action labels;
- host-name truncation;
- Settings values;
- Wi-Fi values;
- Launcher truncation;
- Mac Control centering;
- native UI bounds tests;
- UI capture rendering.

A simple global change from:

```text
1 → 2
```

is therefore not appropriate.

---

## 4. Target Typography

Introduce a shared normal system scale:

```cpp
constexpr float systemTextScale = 1.10f;
```

Conceptually:

```text
Font0 native glyph
6 × 8 px

↓

1.10×

↓

approximately
6.6 × 8.8 px
```

Actual rasterization remains owned by M5GFX/LovyanGFX.

Do not manually rescale glyph bitmaps.

---

## 5. TextStyle

Change:

```cpp
struct TextStyle {
    RgbColor foreground;
    RgbColor background;
    std::uint8_t scale;
};
```

to a representation capable of fractional scaling.

Preferred:

```cpp
struct TextStyle {
    RgbColor foreground;
    RgbColor background;
    float scale;
};
```

Existing initializers such as:

```cpp
{palette::ink, palette::bone, 1}
```

must remain valid.

However, normal UI text should migrate to the shared system scale instead of continuing to hard-code `1`.

---

## 6. Shared Typography Metrics

Do not spread expressions such as:

```cpp
text.size() * 6
```

or:

```cpp
8 * style.scale
```

throughout the UI.

Extend the shared display/text-layout layer with explicit system typography metrics.

For example:

```cpp
constexpr float systemTextScale = 1.10f;

constexpr float systemGlyphNativeWidth = 6.0f;
constexpr float systemGlyphNativeHeight = 8.0f;
```

Provide helpers conceptually equivalent to:

```cpp
systemTextWidth(text)
systemTextHeight()
rightAlignedTextX(text)
centeredTextX(text, width)
```

The exact API can follow existing project conventions.

### Requirement

Layout code should not need to know that Font0 is historically `6×8`.

---

## 7. Rendering

Update:

```text
src/hardware/cardputer/cardputer_display_adapter.cpp
```

so fractional `TextStyle.scale` values are forwarded directly to M5GFX:

```cpp
target.setTextSize(style.scale);
```

M5GFX already supports floating-point text scale.

Damage tracking must continue using the actual rendered metrics:

```cpp
target.textWidth(text)
target.fontHeight()
```

Do not replace those with estimated dimensions.

---

## 8. Normal System Text Style

Avoid changing every UI call to arbitrary floating-point literals such as:

```cpp
1.10f
```

Introduce a shared normal style convention.

For example, either:

```cpp
constexpr float systemTextScale = 1.10f;
```

and:

```cpp
TextStyle{foreground, background, systemTextScale}
```

or shared helpers/styles if that fits the architecture better.

Do not introduce a large theme framework solely for this plan.

The goal is simply to make future font-size changes centralized.

---

## 9. Existing Scale 2 Text

Existing intentional large Font0 text must be reviewed individually.

For example:

```cpp
TextStyle{..., 2}
```

must not automatically become:

```text
2.2×
```

unless that produces the intended result.

Treat existing explicit large text as a separate semantic size.

Prefer keeping intentional `2×` elements visually unchanged in Plan 039 unless they clearly belong to normal system typography.

---

## 10. Layout Audit

Audit every screen that renders normal Font0 text.

At minimum:

```text
src/apps/shell/
src/apps/launcher/
src/apps/network/
src/apps/hosts/
src/apps/system/
src/apps/pomodoro/
src/apps/mac_control/
src/apps/mac_status/
src/apps/led_gallery/
src/core/display/
src/core/lifecycle/
```

For each screen verify:

- text stays inside 240×135;
- labels do not overlap values;
- rows do not collide vertically;
- centered labels remain centered;
- right-aligned values remain right-aligned;
- selected-row backgrounds still contain the glyphs;
- footer labels remain visible;
- truncation reflects the real scaled width.

---

## 11. Home

Review specifically:

```text
WiFi
BT
battery percentage
connected device name
APPS
SETTINGS
```

Replace assumptions such as:

```cpp
label.size() * 6
```

with the shared typography helpers.

### Connected device name

Current truncation assumes six pixels per character.

Update truncation to use the new scaled system width.

Do not horizontally scroll the device name.

Preserve ellipsis behavior.

---

## 12. Launcher

Check:

- title/header;
- app labels;
- selection rows;
- app-number alignment;
- unavailable overlay;
- footer/instructions.

Existing `fitName()` and overlay fitting must reflect scaled text width.

Do not reduce font size locally just to preserve the existing maximum character count.

Prefer correct truncation.

---

## 13. Settings

Verify all rows:

```text
Bluetooth
Wi-Fi
Sound volume
Screen timeout
Screen brightness
LED brightness
```

Check both labels and right-aligned values.

Existing 16 px row backgrounds may remain unchanged if 1.10× text fits comfortably.

Do not enlarge row height pre-emptively.

Only change row geometry if captures or tests demonstrate an actual issue.

---

## 14. Bluetooth / Host Settings

This is one of the most layout-sensitive areas.

Verify:

- host list;
- status text;
- selected rows;
- host detail page;
- rename UI;
- delete confirmation;
- pairing instructions;
- errors;
- footer actions.

Long instructions must be checked carefully.

Examples include text similar to:

```text
Delete this host and its pairing?
Please wait for the code or READY
Choose Cardputer Hub
in computer Bluetooth settings
```

At 1.10×, retain existing copy wherever it still fits.

If a string no longer fits:

1. prefer a small wording adjustment;
2. otherwise use explicit multi-line layout;
3. do not shrink that individual string back to 1.00× unless there is a strong UI reason.

Micro 5 pairing digits remain unchanged.

---

## 15. Wi-Fi Settings

Review:

- list labels;
- values;
- SSID;
- connection states;
- credential screens;
- errors;
- footer hints.

Replace hard-coded right-alignment based on:

```cpp
value.size() * 6
```

with shared text-width handling.

---

## 16. System Mini App

Review all rows:

```text
BATTERY
BLUETOOTH
HOST
HOST STATUS
WI-FI
NETWORK
VERSION
```

Pay particular attention to combinations where both the label and right-side value are long.

Existing truncation limits may need to be recalculated.

---

## 17. Mac Status

Mac Status has dense telemetry and therefore must receive explicit visual review.

Verify:

- CPU;
- RAM;
- SSD;
- battery;
- network;
- thermal state;
- values;
- bars.

Do not enlarge custom graphical elements merely because text becomes 1.10×.

Preserve the current information density unless actual collisions appear.

---

## 18. Mac Control

Normal labels should use the new system scale.

Custom Micro 5 slot digits remain unchanged.

Update any label-centering calculation currently using:

```cpp
systemGlyphWidth
```

so centering reflects the 1.10× text size.

---

## 19. Pomodoro

Keep the large timer digits unchanged.

Scale normal text such as:

- phase label;
- cycle information;
- controls;
- footer/instructions.

Verify that larger labels do not compete visually with the timer.

---

## 20. LED Gallery

Scale normal LCD labels and hints to 1.10×.

Review especially the bottom controls, because horizontal space is already limited.

Examples:

```text
< > EFFECT
1-0 / FN+1-0
SPACE ...
```

If necessary, abbreviate control text rather than reducing the global system scale.

Do not alter LED Puzzle animations.

---

## 21. Contextual Footer

Update:

```text
src/core/display/contextual_footer.h
```

to use the shared system scale.

Verify combinations such as:

```text
ESC CANCEL                  ENTER APPLY
ESC CANCEL                 ENTER DELETE
```

Both sides must remain visible and non-overlapping.

Right alignment must use real scaled width.

---

## 22. Splash / Boot UI

Review the splash screen separately.

Existing:

```text
productNameStyle scale 2
quietStyle scale 1
versionStyle scale 1
```

should not be mechanically multiplied.

Use the new normal system scale for normal secondary text where appropriate.

Keep the primary product title visually equivalent to the current intended large size unless a capture shows a reason to change it.

---

## 23. UI Capture Format

Current UI capture tooling assumes integer scale and 6×8 metrics.

Update the capture format so fractional scaling can be represented.

Do not serialize scale using:

```cpp
unsigned(style.scale)
```

because `1.10f` would become `1`.

The capture representation may use either:

```text
1.10
```

or an integer fixed-point form such as:

```text
110
```

as long as it is deterministic.

Update:

```text
scripts/render_ui_capture.py
```

accordingly.

The renderer should reproduce the same approximate Font0 scaling used on the device closely enough for layout review.

---

## 24. Before / After Captures

A major deliverable of Plan 039 is the ability to visually inspect the 1.10× result.

Generate representative **1.00× baseline** and **1.10× candidate** captures.

At minimum capture:

1. Home
2. Launcher
3. Settings
4. Bluetooth list
5. Bluetooth pairing screen
6. Wi-Fi Settings
7. System
8. Mac Status
9. Mac Control
10. Pomodoro
11. LED Gallery

Prefer equivalent UI state for before/after comparisons.

Store or generate them in a way that makes side-by-side inspection easy.

The implementation is not considered visually accepted merely because tests pass.

---

## 25. Tests

Update native display assertions currently based on:

```cpp
6 * style.scale
8 * style.scale
```

Prefer shared metric helpers where practical.

Tests must cover fractional scale.

Add at least one regression proving that:

```text
scale = 1.10
```

does not get truncated to:

```text
scale = 1
```

during rendering or capture serialization.

---

## 26. Bounds Testing

Existing UI tests should continue checking that rendered text remains inside:

```text
240 × 135
```

However, bounds calculations must support fractional scale correctly.

Avoid integer truncation that could hide a one-pixel overflow.

Use conservative rounding where necessary.

---

## 27. Physical Validation

After software validation:

1. build production firmware;
2. install it on Cardputer Adv;
3. inspect normal reading distance;
4. compare directly against the current 1.00× version.

Review specifically:

- readability;
- glyph quality at fractional scaling;
- visual consistency;
- row spacing;
- perceived density;
- clipping;
- long labels;
- alignment.

Fractional bitmap scaling may look different on the actual LCD than in screenshots, so physical validation is required.

---

## 28. Decision Gate

The first implementation must stop at:

```text
1.10×
```

Do not automatically continue to 1.15× or 1.20×.

After UI captures and physical review, choose one of:

```text
A. Keep 1.10×
B. Return selected surfaces to 1.00×
C. Try 1.15× as a separate follow-up
D. Investigate a different bitmap font
```

Plan 039 itself targets **1.10× only**.

---

## 29. Suggested Implementation Order

### Step 1 — typography infrastructure

- change `TextStyle.scale` to fractional;
- add `systemTextScale = 1.10f`;
- centralize Font0 metrics;
- add width/alignment helpers.

### Step 2 — display adapter

- confirm fractional scale reaches M5GFX;
- preserve correct damage bounds.

### Step 3 — shared layout

- update `text_layout.h`;
- update contextual footer;
- remove hard-coded `* 6` assumptions from shared paths.

### Step 4 — system UI

Update:

- Home;
- Launcher;
- Settings;
- Bluetooth;
- Wi-Fi.

### Step 5 — Mini Apps

Update:

- System;
- Mac Status;
- Mac Control;
- Pomodoro;
- LED Gallery.

### Step 6 — tests and capture tooling

- fractional scale tests;
- layout bounds tests;
- capture serialization;
- capture renderer.

### Step 7 — visual comparison

Generate 1.00× / 1.10× reference captures.

### Step 8 — device validation

Install on Cardputer Adv and review before deciding on any larger scale.

---

## 30. Acceptance Criteria

Plan 039 is complete when:

- normal Font0 UI text renders at **1.10×**;
- Micro 5 custom graphics retain their intended size;
- font scale is centrally configurable;
- no important UI code relies on incorrect hard-coded 6 px alignment assumptions;
- no text is clipped outside 240×135;
- no major labels overlap;
- right alignment and centering remain correct;
- Launcher and host-name truncation use scaled metrics;
- contextual footers remain readable;
- all native tests pass;
- production firmware builds successfully;
- UI capture tooling supports fractional scale;
- representative 1.00× and 1.10× captures are produced;
- the 1.10× firmware is reviewed on physical Cardputer Adv.

---

## 31. Follow-Up

Do not commit to another font size in this plan.

After physical review of 1.10×, a follow-up may decide whether to:

```text
1.10× → keep
```

or experiment with:

```text
1.15×
```

The typography infrastructure introduced by Plan 039 should make that follow-up a small configuration/layout-tuning change rather than another cross-project refactor.
