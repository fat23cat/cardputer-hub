# 022 — UI Update and Render Scheduling

Status: **Complete**

Physical Cardputer-Adv validation completed on 2026-09-14 with commit
`46b760a` installed through CRUB's dedicated `hub` partition. The smoke test
confirmed responsive keyboard input, smooth Home wave and page transitions,
unchanged Bluetooth/Host Settings behavior, and correct battery/status updates.

## 1. Goal

Reduce unnecessary work in the main firmware loop and remove frequent dynamic string allocation from UI rendering.

The current runtime loop executes approximately once per FreeRTOS tick and calls `ApplicationShell::update()` on every iteration.

UI code currently performs state serialization and temporary `std::string` construction even when nothing visible has changed.

This plan introduces:

* explicit UI update scheduling;
* bounded UI refresh frequency;
* structured view-state comparison instead of serialized string comparison;
* separation between high-frequency service polling and lower-frequency UI work.

The change must preserve existing UI behavior.

---

## 2. Current Problem

The main runtime loop currently performs:

```text
runtime.update()
hosts.update()
battery.update()
applicationShell.update()
delay(1 tick)
```

`ApplicationShell::update()` therefore runs approximately every millisecond.

The Home renderer repeatedly constructs values such as:

```cpp
std::string name;
std::string state;
std::string frame;
std::string battery;
```

`HostSettings::render()` additionally builds a serialized string representing most of the UI state:

```cpp
std::string frame = ...;

for (const auto& host : settings.hosts)
    frame += ...;
```

The resulting string is compared against `previousFrame_` to decide whether rendering is required.

This causes avoidable:

* heap allocations;
* string copies;
* formatting;
* comparisons;
* UI-layer work;

inside one of the hottest firmware paths.

This is particularly undesirable for a long-running embedded application.

---

## 3. Scope

This plan should change only runtime/UI scheduling and render change detection.

Included:

* UI update cadence;
* Home view-state tracking;
* Bluetooth/Host Settings view-state tracking;
* removal of string-based frame serialization;
* tests for dirty-state rendering and timing behavior.

Not included:

* redesign of `ApplicationShell`;
* Launcher implementation;
* AppRegistry integration;
* changes to Bluetooth behavior;
* changes to HostService public API;
* display power management;
* animation redesign.

---

## 4. Target Runtime Model

High-frequency infrastructure may continue to update every firmware loop:

```text
Firmware loop
    ↓
Input polling
BluetoothService / HostService
other time-sensitive Services
    ↓
UI scheduler
    ↓
ApplicationShell
```

UI processing should run at a bounded cadence.

Recommended initial cadence:

```text
20–50 ms
```

For example:

```text
UI update rate: 50 Hz
interval: 20 ms
```

This is sufficient for keyboard-driven interaction and current screen transitions while reducing UI work by roughly an order of magnitude or more.

The exact value should be defined as a named constant and covered by tests rather than being scattered through the runtime.

---

## 5. Input Handling

Input must remain responsive.

Input events sampled between UI updates must not be lost.

Choose one of these approaches:

### Preferred

Accumulate semantic `InputEvents` until the next UI update.

```text
runtime.update()
    ↓
pendingInput += newInput

when ui interval reached:
    shell.update(pendingInput, elapsed)
    pendingInput.clear()
```

### Alternative

Trigger a UI update immediately whenever non-empty input is received, while keeping animation-only updates rate-limited.

For the current firmware, the second approach may be simpler:

```text
update UI if:
- input exists, OR
- UI timer expired
```

This gives immediate keyboard response while avoiding idle 1 kHz UI polling.

---

## 6. Home View State

Replace `homeFrame_` string serialization with a typed state.

Example:

```cpp
struct HomeViewState {
    std::uint32_t activeHostId = 0;
    std::string hostName;
    HomeConnectionStatus status = HomeConnectionStatus::Off;
    std::optional<std::uint8_t> batteryPercent;

    bool operator==(const HomeViewState&) const = default;
};
```

If defaulted C++20 comparison is unavailable because the project remains C++17, implement explicit equality.

Alternatively split static and frequently changing state:

```cpp
struct HomeConnectionFrame {
    std::uint32_t activeHostId;
    std::string hostName;
    HomeConnectionStatus status;
};

std::optional<HomeConnectionFrame> previousConnectionFrame_;
std::optional<std::uint8_t> previousBattery_;
```

The wave animation should continue to have its own timer.

Do not reconstruct unrelated frame state simply because the animation advances.

---

## 7. Host Settings View State

Remove `previousFrame_` and the large serialized state string from `HostSettings`.

Use typed state snapshots instead.

Example:

```cpp
struct HostSettingsRenderState {
    std::size_t focus;
    bool renaming;
    bool deleting;
    std::optional<std::uint32_t> detailHost;
    std::size_t detailFocus;

    HostUiStatus status;
    HostResult lastResult;

    bool pairing;
    BluetoothPairingState pairingState;
    std::optional<PairingPromptViewState> pairingPrompt;

    std::optional<std::uint32_t> activeHost;
    std::vector<HostListEntryViewState> hosts;

    std::string entry;
};
```

However, avoid creating an unnecessarily generic UI framework.

For the current screen, separate state structures for:

```text
Host list
Host detail
Rename dialog
Delete dialog
Pairing dialog
```

may be simpler and cheaper.

Existing incremental row drawing in `renderList()` should be preserved.

---

## 8. Allocation Policy

This plan does not require eliminating `std::string` from UI code entirely.

Strings are appropriate for:

* host names;
* editable text;
* human-readable labels.

The goal is specifically to avoid repeatedly creating synthetic serialized strings solely for change detection.

Avoid patterns such as:

```cpp
frame =
    std::to_string(a) + ":" +
    std::to_string(b) + ":" +
    ...
```

inside regularly executed render paths.

---

## 9. Main Loop Changes

Move UI scheduling responsibility into a small dedicated runtime helper or the composition root.

Avoid growing `app_main()` indefinitely.

Possible abstraction:

```cpp
class UiScheduler {
  public:
    bool shouldUpdate(std::chrono::milliseconds elapsed, bool hasInput);

  private:
    std::chrono::milliseconds remaining_{0};
};
```

A simpler elapsed accumulator in `main.cpp` is acceptable initially if it remains small.

Do not move Bluetooth timing into the UI scheduler.

---

## 10. Testing

Add native tests covering:

### Scheduler

* no-input UI updates are rate-limited;
* input triggers immediate UI processing;
* elapsed time is accumulated correctly;
* large delayed frames do not cause repeated catch-up rendering.

### Home rendering

* unchanged state produces no unnecessary redraw;
* host change redraws host section;
* Bluetooth status change redraws connection state;
* battery change redraws battery only;
* wave updates independently.

### Host Settings

* unchanged state produces no redraw;
* changing focus redraws only required rows;
* renaming input updates the rename view;
* host status changes update the status area;
* pairing prompt changes redraw pairing content.

---

## 11. Acceptance Criteria

The plan is complete when:

* `ApplicationShell::update()` is no longer called every 1 ms while idle;
* keyboard input remains subjectively immediate;
* Home animation remains smooth;
* no UI state uses large concatenated strings for dirty checking;
* `HostSettings::previousFrame_` is removed;
* existing native UI tests pass;
* new dirty-render tests pass;
* firmware builds successfully with ESP-IDF;
* behavior on the physical Cardputer remains unchanged.

---

## 12. Follow-Up

Do not combine this work with Launcher or Mini App architecture.

A later UI architecture plan may introduce:

```text
Screen
View
MiniApp lifecycle
Launcher
```

only when the first real Mini App requires it.
