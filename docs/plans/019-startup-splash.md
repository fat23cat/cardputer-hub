# Startup Splash Plan

Status: **Implemented**

This plan defines the short branded startup presentation added after the
initial System Core and application-shell UI delivered by plan 017.

## 1. Objective

Replace the transient black build-information screen with a deliberate startup
screen that keeps the firmware version readable for approximately two seconds.

## 2. Scope

### In scope

* Render the product identity and embedded firmware version with the approved
  Cardputer Hub palette.
* Use restrained registration marks and a twelve-segment progress treatment.
* Fill the progress segments over 1.8 seconds and retain the completed state
  for 0.2 seconds before presenting Home.
* Advance time from injected monotonic elapsed time.
* Keep platform, keyboard, Connectivity, and Service updates running while the
  splash is visible.
* Consume input sampled on the frame that hands presentation to Home.
* Update architecture, UI requirements, and user manuals.

### Non-goals

* Report individual boot-operation completion through the progress segments.
* Add startup sound, display dimming, or wake behavior.
* Change Bluetooth initialization, persistence, host profiles, or plan 018.
* Erase NVS or saved Bluetooth bonds during installation.

## 3. Architecture

`SystemRuntime` owns the startup presentation and its elapsed-time state. The
display remains behind `IDisplayAdapter`, and the composition root delays the
first `ApplicationShell` presentation until the splash reports completion.

The progress animation performs bounded incremental drawing only when another
segment becomes visible. It does not use a blocking delay. Input continues to
be polled so hardware state remains current, but the composition root does not
route startup input into the application shell.

## 4. Visual Treatment

The 240x135 screen uses:

* Bone as the full-bleed surface;
* Ink for the product identity and version;
* Ordinal for quiet startup and version labels;
* Blue for the left rail, one registration accent, and completed segments;
* Pale for empty progress segments.

The firmware-provided product name and version remain data-driven. No release
number is hardcoded in presentation logic.

## 5. Verification

Behavioral tests cover:

* product and version rendering from `BuildInfo`;
* approved palette use for the primary splash surface and identity;
* segment boundaries at 150 ms intervals;
* a fully filled bar at 1.8 seconds;
* splash completion at exactly two seconds;
* continued platform and keyboard polling;
* idempotent startup and safe pre-start updates.

Implementation acceptance requires the repository host checks and an ESP-IDF
5.5.5 production firmware build. Final visual judgment remains an on-device
review because host tests validate drawing commands and geometry rather than
the physical LCD.
