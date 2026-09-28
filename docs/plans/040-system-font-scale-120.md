# 040 — System Font Scale 1.20× Visual Trial

## Status

**Software implemented; physical display review pending.**

The user could not see a useful difference at 1.10× in Plan 039's captures and
requested this 1.20× trial. [Three-way captures](040-captures/comparison.png)
compare equivalent synthetic states at 1.00×, 1.10×, and 1.20×.

## 1. Goal

Determine whether normal Font0 text at 1.20× is more legible on the Cardputer
Adv without compromising the 240×135 layout.

## 2. Scope

Set the shared normal text scale to 1.20×. Refit long Bluetooth instructions,
LED Gallery hints, the Wi-Fi credential viewport, and the startup version.
Retain the 1.00× and 1.10× images for comparison.

## 3. Architecture

The existing `TextStyle` float scale and `text_layout.h` metrics from Plan 039
remain the only typography path. The M5GFX display adapter receives the
fractional scale directly. No new subsystem is introduced.

## 4. Ownership & Boundaries

| Owner | Responsibility |
| --- | --- |
| Core display | Own the normal scale, metrics, and alignment helpers. |
| Mini Apps and shell | Fit their copy and rows to the shared metrics. |
| Hardware adapter | Rasterize with M5GFX and track actual damage bounds. |
| Native capture tools | Record and approximate the fractional raster for review. |

## 5. State Model

The scale is a compile-time UI constant. It has no persistent setting or
runtime transition. Device validation determines whether 1.20× remains the
chosen default or a later change is needed.

## 6. BDD Scenarios

### Scenario: Long Bluetooth instructions

Given the Bluetooth confirmation or waiting screen, when normal text renders
at 1.20×, then each instruction remains within the display and Micro 5 pairing
digits keep their original size.

### Scenario: Credential editing

Given a maximum-length Wi-Fi credential, when the user types or deletes at
1.20×, then the visible tail and cursor stay within the display while the full
credential remains stored.

### Scenario: LED Gallery controls

Given Tetris Dream is selected, when its control hint renders at 1.20×, then
the hint fits and continues to identify move, rotate, soft drop, and hard drop.

### Scenario: Long startup version

Given a build version longer than one text row, when the startup splash appears,
then the version wraps below its label without clipping or covering progress.

## 7. Architecture Invariants

- Normal Font0 text uses the one shared scale and width helpers.
- Micro 5 bitmap glyphs and intentional 2× Font0 elements keep their sizes.
- Text capture serialization preserves a fractional scale.
- No UI text or rectangle extends beyond 240×135 in covered native states.

## 8. Tests mapped to scenarios

| Scenario or invariant | Evidence |
| --- | --- |
| Shared 1.20× metrics and serialization | `test_fractional_system_text_metrics_and_capture` |
| Bluetooth copy and bounds | `test_host_settings` native fixture |
| Wi-Fi credential viewport | `test_editor_keeps_long_credentials_on_screen` |
| LED Gallery hint and bounds | `test_contextual_lcd_hints_and_no_r_reset` |
| Long startup version | `test_long_splash_version_wraps_without_clipping_or_covering_progress` |
| Other UI bounds | Native display fixtures and 11 paired 1.20× captures |

## 9. Physical Acceptance

Build the production image, install it through the repository's CRUB-safe
workflow, and compare 1.00×, 1.10×, and 1.20× on a physical Cardputer Adv at
normal reading distance. Check readability, raster quality, clipped pixels,
selected rows, and long labels. This step is pending because no device was
connected during software implementation.

## 10. Out of Scope

Changing display resolution, fonts, Micro 5 assets, Mini App behavior, or
selecting a scale beyond 1.20×.
