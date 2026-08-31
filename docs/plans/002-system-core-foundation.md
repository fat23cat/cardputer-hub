# System Core Foundation Plan

Status: **Complete**

This plan describes the next implementation change after the project bootstrap.
It establishes hardware-independent boot lifecycle, logging, keyboard input,
and display boundaries without implementing later product features.

## 1. Objective

Create one reviewable foundation change for:

* System Core startup and update lifecycle;
* structured, level-filtered logging;
* semantic keyboard input events;
* basic hardware-independent display output;
* thin Cardputer-Adv adapters for those interfaces.

Implementation should use:

```text
branch:   feat/002-system-core-foundation
PR title: [002] Establish System Core foundation
```

The implementation branch must begin from an updated `main` after plan 001 is
merged.

## 2. Scope

### In scope

* A hardware-independent `SystemRuntime` composed from adapter interfaces.
* Idempotent platform initialization and a predictable update loop.
* Firmware build information represented as one immutable value.
* Structured log records, severity filtering, and a serial log sink.
* Hardware-neutral keyboard events for printable and named Cardputer keys.
* A minimal display interface and boot screen showing product name and version.
* Native unit and integration tests using fakes.
* Thin Cardputer-Adv platform, keyboard, display, and serial-log adapters.
* README updates describing the new observable boot behavior.

### Non-goals

Do not implement or stub:

* configuration or persistence;
* navigation, Launcher, or the application shell;
* capabilities or AppRegistry;
* Actions or the Action Bus;
* connectivity, Services, or Mini Apps;
* global shortcuts or input routing to applications;
* a general UI toolkit, layout engine, or theme system;
* key repeat or key-release events.

## 3. Architecture and Runtime Behavior

Preserve the documented dependency direction and keep Arduino, ESP32, M5Stack,
and M5GFX headers inside hardware implementations. System Core and its tests
must compile on the native host without physical-device dependencies.

`main.cpp` remains the composition root. It should construct statically lived
Cardputer adapters and inject non-owning references into `SystemRuntime`.

Startup behavior:

1. Initialize the Cardputer platform once.
2. Emit informational log records for product name, version, commit, and build
   type.
3. Clear the display to black.
4. Draw the product name and firmware version in white using a simple fixed
   boot layout.

Repeated startup calls must not initialize adapters, log metadata, or redraw
the screen again. An update requested before startup must be safe and must not
touch hardware.

Update behavior after startup:

1. Update the Cardputer hardware state.
2. Poll the keyboard adapter.
3. Return the input events produced during that update to the caller.

No consumer is added in this change; the composition root may intentionally
discard the returned events until input routing is implemented.

## 4. Public Interfaces and Types

### Build information

Replace the separate build-information accessors with an immutable `BuildInfo`
value containing:

```text
name
version
commit
build type
```

The values continue to come from the existing PlatformIO build definitions and
metadata script.

### Platform lifecycle

Define an `IPlatformAdapter` contract with:

```text
begin()
update()
```

`SystemRuntime::start()` owns initialization ordering. `SystemRuntime::update()`
owns per-loop ordering and returns the current input-event collection.

### Logging

Define:

```text
LogLevel: Debug, Info, Warning, Error
LogRecord: level, component, message
ILogSink::write(LogRecord)
Logger: minimum level plus log operations
```

The logger must drop records below its configured threshold and otherwise pass
the record to its sink unchanged. The firmware composition root should default
to `Info`; configuration-driven levels are deferred.

The serial sink should format each record as:

```text
[LEVEL] component: message
```

Logging must not include keyboard characters, credentials, or other user data.

### Display

Define an `IDisplayAdapter` with only the primitives needed for the boot screen:

```text
clear(color)
drawText(position, text, style)
```

Color is represented as hardware-neutral RGB components. Position uses signed
pixel coordinates. Text style contains foreground and background colors plus a
positive integer scale. The Cardputer display adapter translates these values
to M5GFX calls.

This contract is not a general application UI API and may be extended when the
application shell is planned.

### Keyboard input

Define an `IKeyboardAdapter::poll(InputEvents&)` contract. The adapter clears
and fills a caller-owned event collection on each poll.

An input event is either:

* a printable character; or
* a named key: Tab, Enter, Backspace, Delete, Escape, Up, Down, Left, Right, or
  F1 through F12.

Each event carries the active Shift, Ctrl, Alt, Option, and Fn modifier flags.
Emit press-only semantic events in this change. Track the previous keyboard
snapshot so a held key is not emitted again merely because another key changes.
A release updates that snapshot but produces no event; pressing the key again
after release produces a new event. Preserve vendor-reported character order,
then emit named keys in enum order for deterministic behavior.

Keep vendor state extraction in the hardware adapter. Put state comparison and
event translation in hardware-independent code so it can be tested natively.

## 5. Test-Driven Implementation Sequence

Follow RED-GREEN-REFACTOR separately for each behavior and record the expected
initial failures in this plan's completion record.

1. Convert the existing build-information test to the `BuildInfo` contract,
   observe the expected failure, then implement the value.
2. Add logger tests for every threshold boundary and unchanged record delivery,
   then add the logger and fake sink.
3. Add keyboard translation tests, then implement the minimum snapshot and
   event translator behavior.
4. Add `SystemRuntime` integration tests with fake platform, display, logger,
   and keyboard adapters, then implement startup and update orchestration.
5. Add the thin Cardputer adapters and composition root, relying on production
   firmware compilation for vendor API compatibility.
6. Refactor only after focused and full native tests remain green.

Required native scenarios:

* build metadata is present and stable;
* log records below the threshold are suppressed;
* accepted log records preserve level, component, and message;
* printable characters and all named keys are translated correctly;
* modifiers are attached to emitted events;
* held keys are deduplicated;
* release followed by another press emits a new event;
* startup performs platform initialization before logging and display output;
* repeated startup is idempotent;
* pre-start update is safe and produces no events;
* running update refreshes hardware before polling input;
* boot metadata and display commands use the current `BuildInfo` values.

Direct extraction from M5Cardputer's keyboard structure is a thin-adapter TDD
exception because the native environment excludes vendor hardware libraries.
The hardware-independent translator receives full behavioral coverage, while
strict Cardputer-Adv compilation verifies the adapter boundary.

## 6. Documentation and Verification

Update `README.md` so it no longer describes the firmware as serial-only. State
that the firmware logs build metadata, renders a minimal boot screen, and polls
keyboard input without yet routing it to product behavior.

No `docs/ARCHITECTURE.md` change is expected because this plan implements its
existing lifecycle and keyboard-adapter model. Update that document in the same
change if implementation requires a different architectural decision.

Run and record:

```text
make format
make format-check
make lint
make test
make build
make check
```

When hardware is available, optionally upload the image and confirm the serial
records and boot screen. Lack of hardware does not waive native tests or
Cardputer-Adv compilation.

## 7. Acceptance Criteria

The change is complete only when:

* System Core lifecycle, logging, input translation, and display contracts are
  hardware-independent and covered by native tests;
* startup is ordered and idempotent, and pre-start updates are safe;
* the firmware logs all build metadata and renders the minimal boot screen;
* keyboard input is translated without duplicate held-key events or character
  logging;
* hardware-specific dependencies remain confined to adapters;
* no deferred Phase 1 or product behavior is introduced;
* README and the plan completion record reflect the delivered behavior;
* every applicable repository check passes.

## 8. Assumptions

* Plan 001 is merged before implementation begins.
* Existing pinned toolchain and library versions remain unchanged.
* Adapter instances and their dependencies live for the firmware process
  lifetime; interfaces do not own injected objects.
* The immediate-mode display operations are sufficient for the boot screen but
  do not commit the future application shell to that rendering model.
* Physical-device validation is optional when hardware is unavailable and must
  be recorded if performed.

## 9. Completion Record

Completed on 2026-08-31 on `feat/002-system-core-foundation` with
hardware-independent System Core contracts and native behavioral coverage.
`main.cpp` remains the composition root for statically lived Cardputer
platform, keyboard, display, and serial-log adapters.

The RED phase produced the expected initial failures:

1. the converted build-information suite failed because
   `firmwareBuildInfo()` did not exist;
2. the logger suite failed because `core/logging/logger.h` did not exist;
3. the keyboard suite failed because
   `core/input/keyboard_event_translator.h` did not exist;
4. the runtime integration suite failed because
   `core/lifecycle/system_runtime.h` did not exist.

The hardware adapter exception was verified through production compilation.
The first compile exposed that the pinned M5Cardputer library declares
`Keyboard_Class` in the global namespace; correcting that thin boundary made
the firmware compile successfully.

Delivered behavior:

* immutable, stable `BuildInfo` metadata;
* threshold-filtered structured logging and serial record formatting;
* semantic printable and named keyboard press events with modifiers,
  deterministic ordering, held-key deduplication, and release tracking;
* ordered, idempotent startup and safe pre-start updates;
* a black boot screen with the current product name and version in white;
* thin Cardputer-Adv lifecycle, keyboard, display, and serial adapters.

Verification completed successfully:

```text
make format        passed
make format-check  passed
make lint          passed; native and Cardputer-Adv Cppcheck found no defects
make test          passed; 14 native tests
make build         passed; Cardputer-Adv firmware compiled with strict warnings
make check         passed; lock, format, lint, test, and firmware build
```

Physical-device upload and visual validation were not performed because no
hardware was made available for this implementation session.
