# 051 — G0 STATUS sheet

## Current status

Planned on **2026-10-11**. Revised on **2026-10-12** before any implementation.
Software implemented on **2026-10-12**; local gates pass; physical acceptance
(section 9) is pending.

Delivered as planned: `NamedKey::G0` from the keyboard adapter, the shell's
global shortcut table with `ui.status.toggle`, `buildStatusSheetModel()` and
`StatusSheetView` in `apps/shell/status_sheet`, the display overlay mode with
the 240×24 band, the shared `wifiStatusWord()`, `hostConnectionWord()` and
`bluetoothSwitchWord()`, and the architecture-check rules for G0 and the
Action. `app_main` logs the heap when the sheet opens and closes, for
acceptance item 8. The two state-word moves kept their existing tests green.

The layout was agreed from an interactive mockup before this plan was written.
The revised five-row sheet is drawn pixel for pixel with the device font in
[`051-mockups/status-sheet.html`](051-mockups/status-sheet.html), in four
states.

### Revision of 2026-10-12

The first version predated three changes and no longer matched the firmware:

- Plan 050 removed the 220 ms page slide and its transient second framebuffer.
  The first version reused that buffer for the overlay and copied the slide's
  motion and tests (`advanceSlideSnapshot()`, `test_slide_transition`), none of
  which exist now.
- The page canvas moved to an 8-bit palette (32.4 KiB). With Wi-Fi, BLE, the
  Companion and microSD the device has about 80 KiB free and a 37 KiB largest
  block. An HTTPS request needs 48 KiB free. A second full-screen surface, even
  at 8 bits, would stop SERVICES HEALTH from using HTTPS while the sheet is
  open and could fail outright next to WEB ACCESS.
- Plan 050 added WEB ACCESS, which Home shows as a WEB group, and holds
  Bluetooth off while the access point runs (`HostStatusSnapshot::radioHeld`,
  shown as `PAUSED` in the Bluetooth list).

This revision therefore:

- draws the sheet straight to the LCD through one small row buffer instead of
  a full-screen overlay surface (§3, Display overlay);
- presents the sheet at once, like every page change since 050, instead of
  sliding it;
- adds a WEB row and the Bluetooth `PAUSED` word;
- drops the page-slide scenario and the overlay composition test;
- names the newer Mini Apps (AI, LED CANVAS, WEB ACCESS) in the scenarios and
  in physical acceptance.

### Decisions taken before planning

| Decision | Reason |
| --- | --- |
| The sheet opens with the G0 button, not with a keyboard chord | One hand, and no conflict with any key a screen or Mini App uses. G0 is unused during normal operation. |
| Full-screen sheet (variant A), not a one-line band over the app | A band only repeats the Home status bar and cannot name the host. |
| No INTERNET row and no reachability probe | Requested. The sheet adds no network traffic. |
| Wi-Fi shows only its state word, without SSID or RSSI | Requested. |
| Battery shows only the percentage | Requested. No segment meter. |
| Opt+I, Ctrl+Space and Fn+digit were rejected | G0 was preferred. Fn+1–0 also belongs to LED GALLERY. |

### Decisions taken in the revision

| Decision | Reason |
| --- | --- |
| No full-screen overlay surface; a 240×24 row buffer only | Any second 240×135 buffer costs 32.4 KiB or more of a 37 KiB largest block. |
| The sheet appears and disappears at once | Confirmed on 2026-10-12. Page changes have had no motion since 050, and a slide would redraw the whole sheet every frame through the row buffer. |
| A WEB row is always shown | WEB ACCESS runs in the background and changes Bluetooth. Fixed rows keep the board stable. |
| The BLUETOOTH row says `PAUSED` while WEB ACCESS holds the radio | Same word as the Bluetooth list; `OFF` would suggest the saved setting changed. |
| The state dot sits just before the value, not in a fixed column | Chosen from the mockup on 2026-10-12: a fixed column left a wide gap before short values such as `READY` and `OFF`. |

## 1. Goal

Make the connection state readable from every screen. Today the Wi-Fi, BT,
WEB, Companion and battery state is visible only in the Home status bar. A Mini
App gives no hint why a Companion feature stopped, whether BT is still
connected, or that WEB ACCESS is holding Bluetooth off.

Pressing G0 shows a STATUS sheet over whatever is on screen. Pressing G0
again, or Escape, removes it. The screen underneath is unchanged and was never
interrupted.

## 2. Scope

- G0 becomes the first global control. The hardware adapter reports it as an
  input event, and the shell maps it to the `ui.status.toggle` Action.
- A full-screen STATUS sheet with five rows: WI-FI, BLUETOOTH, COMPANION, WEB,
  BATTERY.
- The sheet works over Home, Settings, Bluetooth, Wi-Fi, Launcher and every
  Mini App. It does not open over the splash screen or a pairing prompt.
- The underlying screen and Mini App keep running behind the sheet. Timers,
  capability loss and service updates still happen; only their pixels are
  hidden.
- An overlay mode in the display adapter: while it is on, page drawing goes on
  into the page canvas without reaching the LCD, and the sheet is drawn to the
  LCD through a row buffer. Closing presents the page canvas once, so Mini Apps
  never repaint for the sheet.
- Documentation: the UI requirements, architecture and device guide currently
  state that a normal G0 press does nothing. They change with this plan.

## 3. Architecture

```text
CardputerKeyboardAdapter (hardware)      G0 edge via M5Unified BtnA → NamedKey::G0, physicalPress
   ↓
SystemRuntime (core)                      unchanged: wake-only presses are consumed here
   ↓
ApplicationShell (apps/shell)             global shortcut table: G0 → ui.status.toggle
   ├─ sheet visible? → G0 / Escape close, every other event is consumed
   ├─ otherwise      → existing Mini App or system routing
   ├─ StatusSheetModel  ← NetworkService::status(), HostService::status(),
   │                      CompanionService state/advice, WebAccessService::status(),
   │                      battery percent
   └─ StatusSheetView   → IDisplayAdapter overlay regions
CardputerDisplayAdapter                   overlay mode: page presentation held back,
                                          240×24 row buffer, full page push on close
```

**Input.** `M5.update()` already runs in `CardputerPlatform::update()` before
the keyboard poll, and M5Unified already debounces GPIO0 as `BtnA` on
Cardputer-Adv. `CardputerKeyboardAdapter::poll()` reads `M5.BtnA.wasPressed()`.
A press appends `InputEvent{NamedKey, NamedKey::G0}` and sets `physicalPress`.
The display power controller therefore treats G0 like any key: a press while
the display is dimmed or off only wakes it and is consumed. Holding G0 has no
further effect. G0 with Reset still enters ROM download mode, which firmware
cannot see.

`NamedKey::G0` names the physical button. The meaning lives in the shell's
global shortcut table, a constant array of `{key, action id}` pairs with one
entry. Architecture §23 asks for shortcuts as data; this is the first entry,
not the configurable system (see Out of Scope).

**Routing.** `ApplicationShell::update()` checks every event against the
global table before the existing Mini App or system routing. While the sheet
is visible:

- G0 and plain Escape (backtick or Fn+backtick) close it;
- every other event is consumed and reaches neither the Mini App nor the
  system screens, including keys a Mini App binds without a modifier (AI's
  `R`, LED GALLERY's effect keys, POMODORO's Space);
- the active Mini App and the system screens still receive their update with
  an empty event list, so timers and service-driven redraws go on;
- key feedback sounds as usual; G0 uses the ordinary key cue.

Escape that closes the sheet never reaches `IMiniApp::handleBack()` and never
dispatches `app.close`.

**Gating.** `ui.status.toggle` is rejected while the splash is shown, which
already happens because the shell receives no input then. It is also rejected
while `HostStatusSnapshot` carries a pairing prompt (`pairingPrompt` present),
so a passkey is never hidden, and when the display adapter cannot enter
overlay mode (no page canvas). If a pairing prompt appears while the sheet is
visible, the sheet closes. When the display power state reaches Off, a visible
sheet closes, so that waking shows the screen the user was on.

### Sheet

The sheet is drawn with the standard header (`drawScreenHeader()`) and the
list-row grammar from UI requirements §5, without a selection plate: it is a
status board, as in SERVICES HEALTH. `G0 CLOSE` is the header's one dismissal
hint, because G0 is not an obvious way back.

```text
y 6    STATUS                                        G0 CLOSE   (Ordinal)
y 20   ─────────────────────────────────────────────────────────  1 px Ink rule
y 28   01  WI-FI                                   ● CONNECTED
y 48   02  BLUETOOTH                             ● MACBOOK PRO
y 68   03  COMPANION                                   ● READY
y 88   04  WEB                                     ● 2 CLIENTS
y 108  05  BATTERY                                         72%
```

Rows are 20 px apart. Ordinals are Ordinal at x 6. Labels are Ink at x 26. The
Values are right-aligned to x 234. The 5×5 state dot, in the Home dot shapes
and colors, sits at y + 2 with its right edge 4 px before the value's first
glyph, so it moves with the value's width and reads as part of it. A value has
at most 16 glyphs (112 px, from x 122), so the dot never starts left of x 113
and never meets a label, which ends by x 89. Longer host names are shortened by the shared text
layout. Values with a Vermilion dot are drawn in Vermilion; all other values
are Ink. The label always carries the meaning, as §2 of the UI requirements
demands.

| Row | Source | Value and dot |
| --- | --- | --- |
| WI-FI | `NetworkService::status()` | The Wi-Fi Settings header words: `NOT CONFIGURED` (hollow), `OFF` (Pale), `CONNECTING` (Blue), `CONNECTED` (Leaf), `ERROR` (Vermilion). The dot is `homeWifiIndicator()`. The station row is independent of the WEB ACCESS access point. |
| BLUETOOTH | `HostService::status()` | When Ready: the active host name, or `READY` if it has none (Leaf). `PAUSED` (hollow) while `radioHeld` and the saved setting is On. Otherwise `OFF` (hollow), `CONNECTING` / `SECURING` / `PAIRING` (Blue), `ERROR` (Vermilion). The dot is `homeBluetoothIndicator()`. |
| COMPANION | `CompanionService::state()`, `mismatchAdvice()` | `READY` (Leaf); `CONNECTING` while Attaching or Handshaking (Blue); `WAITING BT` while Unavailable and BT is not Ready (including `PAUSED`), otherwise `OFFLINE` (hollow); `ERROR` on ProtocolError (Vermilion); `UPDATE COMPANION` / `UPDATE FIRMWARE` / `REBUILD BOTH` when Incompatible (Vermilion), the same advice as Home. |
| WEB | `WebAccessService::status()` | `OFF` (hollow); while Running `ON` with no client, `1 CLIENT`, or `N CLIENTS` (Leaf); `ERROR` (Vermilion). The dot is `homeWebIndicator()`, hollow while Off. |
| BATTERY | the percent the shell already receives | `NN%`, or `--%` when unknown. No dot. Vermilion at 10% or below. |

The Companion runs over the selected BLE host, so the BLUETOOTH row names the
Mac the Companion belongs to. The sheet does not repeat the name.

The Wi-Fi and BT state words already exist in `WiFiSettings::headerStatus()`
and the anonymous `label()` in `host_settings.cpp`, and `PAUSED` in the
Bluetooth list row. They move to shared free functions so the sheet and the
settings screens cannot drift apart.

`buildStatusSheetModel()` is a pure function from the five inputs to five row
models (`ordinal`, `label`, `indicator`, `value`, `valueColor`).
`StatusSheetView` keeps the last drawn model. It draws the whole sheet on open,
and afterwards redraws only rows whose model changed. A settled sheet issues no
drawing calls.

The Home Living Orb and the Home focus plate pause while the sheet is visible,
as they do while Home is hidden.

### Display overlay mode

The page canvas stays the target of all existing drawing. Mini Apps keep
drawing into it while the sheet is visible, so nothing has to be repainted
when the sheet closes. The sheet itself needs no full-screen memory: it is
drawn to the LCD region by region through one row buffer.

`IDisplayAdapter` gains a default-no-op overlay contract:

```text
bool beginOverlay()                  hold back page presentation; false → no page canvas
void beginOverlayRegion(rectangle)   following calls draw into the row buffer for this
                                     rectangle (at most 240×24)
void endOverlayRegion()              push the region to the LCD once
void endOverlay()                    free the row buffer and present the full page canvas once
```

While overlay mode is on:

- drawing outside a region goes into the page canvas as today; its damage
  accumulates and causes no LCD transfer;
- drawing inside a region goes into a 240×24 row buffer with the page
  canvas's 8-bit palette (5.6 KiB). The buffer is allocated by
  `beginOverlay()` and freed by `endOverlay()`. If it cannot be allocated,
  region drawing goes straight to the LCD: the sheet still opens, and a row
  change may flicker once;
- `beginTransition()` stays a no-op, as on the Cardputer today.

`StatusSheetView` draws the header as one region and each row as one region,
and on open clears the remaining band beneath the last row the same way. Every
region is pushed as one transfer, so the LCD never shows a cleared row
followed by its text.

`beginOverlay()` returns false when the adapter is on its direct-draw fallback
(no page canvas): page drawing would then reach the LCD over the sheet. The
shell logs a warning, and the sheet does not open.

## 4. Ownership & Boundaries

| Component | Owns | May call | Must not call |
| --- | --- | --- | --- |
| `CardputerKeyboardAdapter` | G0 edge detection, `NamedKey::G0`, `physicalPress` | M5Unified `BtnA` | ActionBus, shell, any Action id |
| `SystemRuntime` | wake-only consumption (unchanged) | keyboard and display power | — |
| `ApplicationShell` | global shortcut table, sheet visibility, input interception, gating | ActionBus, const service status getters, `StatusSheetView`, display overlay contract | service mutators, Mini App internals, pairing control |
| `buildStatusSheetModel()` | value words, dots, colors | shared state-word functions | services, display |
| `StatusSheetView` | the last drawn model, row redraws, region boundaries | `IDisplayAdapter` | services, ActionBus |
| `CardputerDisplayAdapter` | overlay mode, row buffer, held-back page damage, the full push on close | M5GFX | sheet content, shell state |
| Mini Apps | their own UI | as today | any knowledge of G0 or the sheet |

## 5. State Model

```text
Hidden ──G0 (allowed, beginOverlay() true)──▶ Visible
  ▲                                              │
  └────── G0 / Esc / pairing prompt / display Off ┘   endOverlay(): full page push

Hidden ──G0 rejected (splash, pairing prompt, no page canvas)──▶ Hidden
```

Overlay mode is on exactly while the sheet is Visible. A display that turns Off
closes the sheet; the page push then reaches a dark LCD, and waking shows the
current page.

## 6. BDD Scenarios

### Scenario: Open over a Mini App

Given:
- POMODORO is active and counting
- the display is awake

When:
- the user presses G0

Then:
- the shell dispatches exactly one `ui.status.toggle`
- the display enters overlay mode and the sheet shows five rows
- POMODORO receives no input event for the press
- the Pomodoro timer keeps counting behind the sheet

### Scenario: Close with Escape

Given:
- the sheet is visible over SERVICES HEALTH

When:
- the user presses backtick

Then:
- the display leaves overlay mode and presents the page canvas once
- SERVICES HEALTH is still active, with its current pixels
- `handleBack()` is not called and no `app.close` is dispatched

### Scenario: Unavailable dependencies

Given:
- Wi-Fi is not configured
- BT is Off with no selected host
- the Companion is Unavailable
- WEB ACCESS is Off
- the battery percent is unknown

When:
- the user presses G0

Then:
- the rows read `NOT CONFIGURED`, `OFF`, `WAITING BT`, `OFF` and `--%`
- the four dots are hollow Ordinal
- no row is Vermilion

### Scenario: WEB ACCESS holds Bluetooth

Given:
- Bluetooth is saved On with a selected host
- WEB ACCESS is Running with two clients, so the radio is held

When:
- the sheet opens

Then:
- the BLUETOOTH row reads `PAUSED` with a hollow dot
- the COMPANION row reads `WAITING BT`
- the WEB row reads `2 CLIENTS` with a Leaf dot

### Scenario: Companion from another build

Given:
- the Companion is Incompatible with advice UpdateFirmware

When:
- the sheet opens

Then:
- the COMPANION row reads `UPDATE FIRMWARE` in Vermilion with a Vermilion dot

### Scenario: Reconnect while visible

Given:
- the sheet is visible with BLUETOOTH `CONNECTING`

When:
- HostService reaches Ready with host name `MACBOOK PRO`

Then:
- only the BLUETOOTH row region is redrawn, showing `MACBOOK PRO` with a Leaf
  dot
- the sheet stays open

### Scenario: Capability lost underneath

Given:
- MAC CONTROL is active and the sheet is visible

When:
- the COMPANION capability disappears

Then:
- MAC CONTROL is deactivated and Launcher is drawn into the page canvas
- no page pixel reaches the LCD while the sheet is visible
- the sheet stays visible, with COMPANION no longer `READY`
- closing the sheet reveals Launcher

### Scenario: Repeated input

Given:
- the sheet is visible

When:
- the user presses G0 twice in one input batch

Then:
- the sheet closes and opens again
- the display is in overlay mode exactly once at the end, with one row buffer

### Scenario: Other keys while visible

Given:
- the sheet is visible over AI

When:
- the user presses Enter, Tab, Space, `,` and `R`

Then:
- AI and the system screens receive none of them
- no `ai-agent-status.refresh` or `ai-usage.refresh` is dispatched
- the sheet is unchanged

### Scenario: Pairing prompt

Given:
- the Bluetooth screen shows a pairing passkey

When:
- the user presses G0

Then:
- `ui.status.toggle` is rejected and the passkey stays visible

### Scenario: Pairing prompt appears while visible

Given:
- pairing was started and is Discoverable
- the sheet is visible

When:
- HostService publishes a pairing prompt

Then:
- the sheet closes and the prompt is visible

### Scenario: Display off

Given:
- the display is off

When:
- the user presses G0

Then:
- the display wakes
- the sheet does not open

### Scenario: Idle while visible

Given:
- the sheet is visible

When:
- the idle timer turns the display off

Then:
- the sheet is hidden and overlay mode ends
- the next key press wakes the display on the screen underneath

### Scenario: No page canvas

Given:
- the display adapter is on its direct-draw fallback, so `beginOverlay()`
  returns false

When:
- the user presses G0

Then:
- a warning is logged
- the sheet stays hidden and the screen is unchanged

### Scenario: No row buffer

Given:
- `beginOverlay()` succeeds but the row buffer cannot be allocated

When:
- the user presses G0

Then:
- the sheet opens, drawn straight to the LCD
- closing it presents the page canvas as usual

Not applicable: **in-flight failure** and **stale completion**. The sheet sends
no request and waits for no completion; it only reads status snapshots that the
services keep current. **G0 during a page slide** no longer applies: page
changes have had no motion since plan 050.

## 7. Architecture Invariants

- `NamedKey::G0` reaches neither a Mini App nor the system screen routing.
- `ui.status.toggle` has exactly one handler, `ApplicationShell`.
- The hardware adapter knows no Action id.
- The sheet only reads snapshots. It never calls a service mutator and causes no
  network, web or Companion request.
- The sheet opens only from G0. No background change opens it, as UI
  requirements §9 already demands for popups.
- Escape that closes the sheet never reaches `handleBack()` and never dispatches
  `app.close`.
- Overlay mode is on only while the sheet is Visible. No page pixel reaches the
  LCD during overlay mode, and leaving it presents the full page canvas once.
- The overlay allocates at most the 240×24 row buffer; no second full-screen
  buffer is created.
- Mini Apps contain no sheet-specific code; `IMiniApp` is unchanged.
- Wi-Fi and BT state words come from one shared function each.

## 8. Tests mapped to scenarios

| Scenario | Test |
| --- | --- |
| Open over a Mini App | `test_application_shell`: `test_g0_opens_status_sheet_over_mini_app` |
| Close with Escape | `test_application_shell`: `test_escape_closes_sheet_without_closing_app` |
| Unavailable dependencies | `test_status_sheet`: `test_model_without_wifi_bt_companion_web_or_battery` |
| WEB ACCESS holds Bluetooth | `test_status_sheet`: `test_model_shows_paused_bluetooth_and_web_clients` |
| Companion from another build | `test_status_sheet`: `test_model_names_side_to_update` |
| Reconnect while visible | `test_status_sheet`: `test_view_redraws_only_changed_row` |
| Capability lost underneath | `test_application_shell`: `test_capability_loss_under_sheet_reveals_launcher` |
| Repeated input | `test_application_shell`: `test_double_g0_leaves_one_overlay` |
| Other keys while visible | `test_application_shell`: `test_sheet_consumes_other_keys` |
| Pairing prompt | `test_application_shell`: `test_g0_rejected_during_pairing_prompt` |
| Pairing prompt appears while visible | `test_application_shell`: `test_pairing_prompt_closes_sheet` |
| Display off | `test_system_runtime`: `test_g0_press_while_off_only_wakes` |
| Idle while visible | `test_application_shell`: `test_display_off_hides_sheet` |
| No page canvas | `test_application_shell`: `test_sheet_stays_hidden_without_overlay` |
| No row buffer | physical acceptance 8 (adapter-only fallback) |
| Shared state words | `test_wifi_settings` and `test_host_settings` stay green after the move |
| Sheet layout | `test_status_sheet`: capture of the five-row frame through `ui_capture.h` |
| Adapter knows no Action id; Mini Apps unaware of G0 | `make architecture-check` plus review |

The fake display in the shell and sheet tests records the overlay contract
calls: whether overlay mode is on, each region rectangle, and page drawing made
during overlay mode. The tests assert that every sheet draw falls inside a
region and that page drawing continues without being presented.

## 9. Physical Acceptance

On Cardputer-Adv, with BLE, Wi-Fi and the Companion connected:

1. G0 opens and closes the sheet over Home, Settings, Wi-Fi password entry,
   Launcher, and each Mini App, including AI, LED CANVAS and WEB ACCESS.
   Escape also closes it.
2. Opening and closing show no flash of a cleared row or of the screen
   underneath.
3. POMODORO keeps counting, LED GALLERY keeps animating the matrix, and LED
   CANVAS keeps drawing strokes from the phone while the sheet is visible. The
   screen underneath is correct after closing.
4. Escape that closes the sheet does not close the Mini App.
5. Turning BT off on the Mac updates the BLUETOOTH and COMPANION rows live.
6. Starting WEB ACCESS while the sheet is closed, then opening it, shows
   BLUETOOTH `PAUSED` and the WEB client count; joining a phone updates WEB.
7. A Companion from another build shows the update advice.
8. With SERVICES HEALTH open and a round running, the sheet opens. Free heap
   and the largest block are logged before and after opening; the difference
   is at most the row buffer, and HTTPS checks still run.
9. G0 wakes a dimmed or dark display without opening the sheet.
10. The sheet is readable at the 10% backlight fallback.
11. Holding G0 while pressing Reset still enters download mode.

## 10. Out of Scope

- Internet reachability, SSID, RSSI and a battery meter (declined).
- Motion for opening and closing the sheet (declined).
- Configurable global shortcuts (Architecture §23 and Phase 4 checklist).
- Long press, double press or other G0 gestures.
- Charging or USB power state: BatteryService has only a voltage estimate.
- Any action from the sheet. It is read-only and has no selection; WEB ACCESS
  and Bluetooth are still switched in their own screens.
- Changes to the Companion or the web UI.

## Documentation to update with the implementation

- `docs/UI_REQUIREMENTS.md` §9: replace "a normal G0 press has no application
  action", describe the sheet, and add the overlay mode next to the canvas
  description; §11: an entry for the current firmware.
- `docs/ARCHITECTURE.md` §5 (display contract: overlay mode and the row
  buffer), §9 (G0 sentence), §23 (first global control) and the Phase 4
  checklist line about G0.
- `docs/manuals/device-guide.md`: the Controls section, the G0 sentences, and
  a short STATUS section.
- `docs/plans/README.md`: this plan's disposition.
