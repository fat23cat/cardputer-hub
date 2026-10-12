# 051 — G0 STATUS sheet

## Current status

Planned on **2026-10-11**. Nothing is implemented yet.

The layout was agreed from an interactive mockup before this plan was written.

### Decisions taken before planning

| Decision | Reason |
| --- | --- |
| The sheet opens with the G0 button, not with a keyboard chord | One hand, and no conflict with any key a screen or Mini App uses. G0 is unused during normal operation. |
| Full-screen sheet (variant A), not a one-line band over the app | A band only repeats the Home status bar and cannot name the host. |
| No INTERNET row and no reachability probe | Requested. The sheet adds no network traffic. |
| Wi-Fi shows only its state word, without SSID or RSSI | Requested. |
| Battery shows only the percentage | Requested. No segment meter. |
| Opt+I, Ctrl+Space and Fn+digit were rejected | G0 was preferred. Fn+1–0 also belongs to LED GALLERY. |

## 1. Goal

Make the connection state readable from every screen. Today the Wi-Fi, BT,
Companion and battery state is visible only in the Home status bar. A Mini App
gives no hint why a Companion feature stopped or whether BT is still connected.

Pressing G0 slides a STATUS sheet over whatever is on screen. Pressing G0
again, or Escape, slides it away. The screen underneath is unchanged and was
never interrupted.

## 2. Scope

- G0 becomes the first global control. The hardware adapter reports it as an
  input event, and the shell maps it to the `ui.status.toggle` Action.
- A full-screen STATUS sheet with four rows: WI-FI, BLUETOOTH, COMPANION,
  BATTERY.
- The sheet works over Home, Settings, Bluetooth, Wi-Fi, Launcher and every
  Mini App. It does not open over the splash screen or a pairing prompt.
- The underlying screen and Mini App keep running behind the sheet. Timers,
  capability loss and service updates still happen; only their pixels are
  hidden.
- An overlay layer in the display adapter. It holds the sheet while the sheet
  is visible, so that closing the sheet does not need Mini Apps to repaint.
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
   │                      CompanionService state/advice, battery percent
   └─ StatusSheetView   → IDisplayAdapter overlay target
CardputerDisplayAdapter                   overlay layer: allocation, draw target,
                                          vertical reveal, composition with the page canvas
core/display/overlay_composition          host-testable row composition
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
  system screens;
- key feedback sounds as usual.

Escape that closes the sheet never reaches `IMiniApp::handleBack()` and never
dispatches `app.close`.

**Gating.** `ui.status.toggle` is rejected while the splash is shown, which
already happens because the shell receives no input then. It is also rejected
while `HostStatusSnapshot` carries a pairing prompt (`pairingPrompt` present),
so a passkey is never hidden. If a pairing prompt appears while the sheet is
visible, the sheet closes. When the display power state reaches Off, a visible
sheet closes immediately, without animation, so that waking shows the screen
the user was on.

### Sheet

The sheet is drawn with the standard header and the list-row grammar from
UI requirements §5, without a selection plate: it is a status board, as in
SERVICES HEALTH.

```text
y 6    STATUS                                        G0 CLOSE   (Ordinal)
y 20   ─────────────────────────────────────────────────────────  1 px Ink rule
y 32   01  WI-FI       ●  CONNECTED
y 56   02  BLUETOOTH   ●  MACBOOK PRO
y 80   03  COMPANION   ●  READY
y 104  04  BATTERY        72%
```

Ordinals are Ordinal at x 6. Labels are Ink at x 26. The 5×5 state dot sits at
x 106, y + 2, using the Home dot shapes and colors. Values are right-aligned to
x 234. The value column is 116 px, which is 16 glyphs at the 1.20× scale. Longer
host names are shortened by the shared text layout. Values with a Vermilion dot
are drawn in Vermilion; all other values are Ink. The label always carries the
meaning, as §2 of the UI requirements demands.

| Row | Source | Value and dot |
| --- | --- | --- |
| WI-FI | `NetworkService::status()` | The Wi-Fi Settings header words: `NOT CONFIGURED` (hollow), `OFF` (Pale), `CONNECTING` (Blue), `CONNECTED` (Leaf), `ERROR` (Vermilion). The dot is `homeWifiIndicator()`. |
| BLUETOOTH | `HostService::status()` | When Ready: the active host name, or `READY` if it has none (Leaf). Otherwise `OFF` (hollow), `CONNECTING` / `SECURING` / `PAIRING` (Blue), `ERROR` (Vermilion). The dot is `homeBluetoothIndicator()`. |
| COMPANION | `CompanionService::state()`, `mismatchAdvice()` | `READY` (Leaf); `CONNECTING` while Attaching or Handshaking (Blue); `WAITING BT` while Unavailable and BT is not Ready, otherwise `OFFLINE` (hollow); `ERROR` on ProtocolError (Vermilion); `UPDATE COMPANION` / `UPDATE FIRMWARE` / `REBUILD BOTH` when Incompatible (Vermilion), the same advice as Home. |
| BATTERY | the percent the shell already receives | `NN%`, or `--%` when unknown. No dot. Vermilion at 10% or below. |

The Companion runs over the selected BLE host, so the BLUETOOTH row names the
Mac the Companion belongs to. The sheet does not repeat the name.

The Wi-Fi and BT state words already exist in `WiFiSettings::headerStatus()`
and the anonymous `label()` in `host_settings.cpp`. They move to shared free
functions so the sheet and the settings screens cannot drift apart.

`buildStatusSheetModel()` is a pure function from the four inputs to four row
models (`ordinal`, `label`, `indicator`, `value`, `valueColor`).
`StatusSheetView` keeps the last drawn model. It draws the whole sheet on open,
and afterwards redraws only rows whose model changed. A settled sheet issues no
drawing calls.

### Motion

Opening slides the sheet down from the top edge. Closing slides it back up.
Both use the 220 ms cubic ease-out of page slides (§6). G0 or Escape pressed
during either motion reverses it from the currently presented position, as a
newer page slide does. The Home Living Orb pauses while the sheet covers it.

If G0 arrives during a page slide, the slide jumps to its end before the sheet
opens.

### Display overlay layer

The page canvas stays the target of all existing drawing. Mini Apps keep
drawing into it while the sheet is visible, so nothing has to be repainted
when the sheet closes.

`IDisplayAdapter` gains a default-no-op overlay contract:

```text
bool openOverlay()            allocate the 240×135 overlay surface; false → unavailable
void closeOverlay()           free it and present the full page canvas once
void setDrawTarget(target)    Page or Overlay, for the calls that follow in this frame
void setOverlayReveal(rows)   0..135: how many top rows of the LCD show the overlay
```

The overlay surface reuses the transient second framebuffer that page slides
already allocate. It exists only while the sheet is visible. The adapter never
holds a slide snapshot and an overlay at the same time: while the overlay is
open, `beginTransition()` is a no-op, and the page underneath changes in place.

During motion the shell redraws the sheet at `y = rows − 135` into the
overlay, and the adapter copies page rows `[rows, 135)` beneath it. The adapter
then presents one contiguous frame, keeping the single-push rule from the
existing slide code. Once fully revealed, page damage causes no LCD transfer,
and sheet row changes push only their own region. The row copy is the
host-tested `composeOverlayFrame()` in `core/display`, next to
`advanceSlideSnapshot()`.

If `openOverlay()` fails for lack of heap, the sheet does not open. The shell
logs a warning, and the underlying screen stays as it was.

## 4. Ownership & Boundaries

| Component | Owns | May call | Must not call |
| --- | --- | --- | --- |
| `CardputerKeyboardAdapter` | G0 edge detection, `NamedKey::G0`, `physicalPress` | M5Unified `BtnA` | ActionBus, shell, any Action id |
| `SystemRuntime` | wake-only consumption (unchanged) | keyboard and display power | — |
| `ApplicationShell` | global shortcut table, sheet visibility, input interception, gating, motion | ActionBus, const service status getters, `StatusSheetView`, display overlay contract | service mutators, Mini App internals, pairing control |
| `buildStatusSheetModel()` | value words, dots, colors | shared state-word functions | services, display |
| `StatusSheetView` | the last drawn model, row redraws | `IDisplayAdapter` | services, ActionBus |
| `CardputerDisplayAdapter` | overlay surface, draw target, reveal, composition | M5GFX, `composeOverlayFrame()` | sheet content, shell state |
| Mini Apps | their own UI | as today | any knowledge of G0 or the sheet |

## 5. State Model

```text
Hidden ──G0 (allowed, overlay allocated)──▶ Opening ──220 ms──▶ Visible
  ▲                                           │                    │
  │                                     G0 / Esc                G0 / Esc / pairing prompt
  │                                           ▼                    ▼
  └──────────────220 ms────────────────── Closing ◀─────────────────┘

Any state ──display Off──▶ Hidden   (immediately, no motion; overlay freed)
Hidden ──G0 rejected (splash, pairing prompt, no heap)──▶ Hidden
```

Opening and Closing reverse into each other from the presented position. The
overlay surface exists in Opening, Visible and Closing, and only there.

## 6. BDD Scenarios

### Scenario: Open over a Mini App

Given:
- POMODORO is active and counting
- the display is awake

When:
- the user presses G0

Then:
- the shell dispatches exactly one `ui.status.toggle`
- the sheet slides down over POMODORO and shows four rows
- POMODORO receives no input event for the press
- the Pomodoro timer keeps counting behind the sheet

### Scenario: Close with Escape

Given:
- the sheet is visible over SERVICES HEALTH

When:
- the user presses backtick

Then:
- the sheet slides up
- SERVICES HEALTH is still active, with its current pixels
- `handleBack()` is not called and no `app.close` is dispatched

### Scenario: Unavailable dependencies

Given:
- Wi-Fi is not configured
- BT is Off with no selected host
- the Companion is Unavailable
- the battery percent is unknown

When:
- the user presses G0

Then:
- the rows read `NOT CONFIGURED`, `OFF`, `WAITING BT` and `--%`
- the three dots are hollow Ordinal
- no row is Vermilion

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
- only the BLUETOOTH row is redrawn, showing `MACBOOK PRO` with a Leaf dot
- the sheet stays open

### Scenario: Capability lost underneath

Given:
- MAC CONTROL is active and the sheet is visible

When:
- the COMPANION capability disappears

Then:
- MAC CONTROL is deactivated and Launcher is drawn into the page canvas,
  without a slide
- the sheet stays visible, with COMPANION no longer `READY`
- closing the sheet reveals Launcher

### Scenario: Repeated input

Given:
- the sheet is opening and is half revealed

When:
- the user presses G0 again

Then:
- the sheet closes from its half-revealed position
- no second overlay is allocated

### Scenario: Other keys while visible

Given:
- the sheet is visible over NFC

When:
- the user presses Enter, Tab, Space and `,`

Then:
- NFC and the system screens receive none of them
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
- the sheet is hidden without motion and the overlay is freed
- the next key press wakes the display on the screen underneath

### Scenario: Not enough heap

Given:
- `openOverlay()` fails

When:
- the user presses G0

Then:
- a warning is logged
- the sheet stays hidden and the screen is unchanged

### Scenario: G0 during a page slide

Given:
- Launcher is sliding to a Mini App

When:
- the user presses G0

Then:
- the slide finishes at once
- the sheet opens over the Mini App

Not applicable: **in-flight failure** and **stale completion**. The sheet sends
no request and waits for no completion; it only reads status snapshots that the
services keep current.

## 7. Architecture Invariants

- `NamedKey::G0` reaches neither a Mini App nor the system screen routing.
- `ui.status.toggle` has exactly one handler, `ApplicationShell`.
- The hardware adapter knows no Action id.
- The sheet only reads snapshots. It never calls a service mutator and causes no
  network or Companion request.
- The sheet opens only from G0. No background change opens it, as UI
  requirements §9 already demands for popups.
- Escape that closes the sheet never reaches `handleBack()` and never dispatches
  `app.close`.
- The overlay surface exists only while the sheet is not Hidden. It never
  coexists with a slide snapshot.
- Mini Apps contain no sheet-specific code; `IMiniApp` is unchanged.
- Wi-Fi and BT state words come from one shared function each.

## 8. Tests mapped to scenarios

| Scenario | Test |
| --- | --- |
| Open over a Mini App | `test_application_shell`: `test_g0_opens_status_sheet_over_mini_app` |
| Close with Escape | `test_application_shell`: `test_escape_closes_sheet_without_closing_app` |
| Unavailable dependencies | `test_status_sheet`: `test_model_without_wifi_bt_companion_or_battery` |
| Companion from another build | `test_status_sheet`: `test_model_names_side_to_update` |
| Reconnect while visible | `test_status_sheet`: `test_view_redraws_only_changed_row` |
| Capability lost underneath | `test_application_shell`: `test_capability_loss_under_sheet_reveals_launcher` |
| Repeated input | `test_application_shell`: `test_g0_reverses_opening_sheet` |
| Other keys while visible | `test_application_shell`: `test_sheet_consumes_other_keys` |
| Pairing prompt | `test_application_shell`: `test_g0_rejected_during_pairing_prompt` |
| Pairing prompt appears while visible | `test_application_shell`: `test_pairing_prompt_closes_sheet` |
| Display off | `test_system_runtime`: `test_g0_press_while_off_only_wakes` |
| Idle while visible | `test_application_shell`: `test_display_off_hides_sheet` |
| Not enough heap | `test_application_shell`: `test_sheet_stays_hidden_without_overlay` |
| G0 during a page slide | `test_application_shell`: `test_g0_finishes_page_slide_first` |
| Overlay row composition | `test_slide_transition`: `test_compose_overlay_frame_rows` |
| Shared state words | `test_wifi_settings` and `test_host_settings` stay green after the move |
| Sheet layout | `test_status_sheet`: capture of the four-row frame through `ui_capture.h` |
| Adapter knows no Action id; Mini Apps unaware of G0 | `make architecture-check` plus review |

The fake display in the shell tests records the overlay contract calls, so the
shell tests can assert the draw target and reveal rows without hardware.

## 9. Physical Acceptance

On Cardputer-Adv, with BLE, Wi-Fi and the Companion connected:

1. G0 opens and closes the sheet over Home, Settings, Wi-Fi password entry,
   Launcher, and each Mini App. Escape also closes it.
2. The slide is smooth, without tearing or a flash of the screen underneath.
3. POMODORO keeps counting and LED GALLERY keeps animating the matrix while the
   sheet is visible. The screen underneath is correct after closing.
4. Escape that closes the sheet does not close the Mini App.
5. Turning BT off on the Mac updates the BLUETOOTH and COMPANION rows live.
6. A Companion from another build shows the update advice.
7. With SERVICES HEALTH open and a round running, the sheet opens. Free heap
   is logged before and after opening.
8. G0 wakes a dimmed or dark display without opening the sheet.
9. The sheet is readable at the 10% backlight fallback.
10. Holding G0 while pressing Reset still enters download mode.

## 10. Out of Scope

- Internet reachability, SSID, RSSI and a battery meter (declined).
- Configurable global shortcuts (Architecture §23 and Phase 4 checklist).
- Long press, double press or other G0 gestures.
- Charging or USB power state: BatteryService has only a voltage estimate.
- Any action from the sheet. It is read-only and has no selection.
- Changes to the Companion.

## Documentation to update with the implementation

- `docs/UI_REQUIREMENTS.md` §9: replace "a normal G0 press has no application
  action" and describe the sheet; add a §11 entry for the current firmware.
- `docs/ARCHITECTURE.md` §9 (G0 sentence), §23 (first global control) and the
  Phase 4 checklist line about G0.
- `docs/manuals/device-guide.md`: the Controls section, the G0 sentences, and
  a short STATUS section.
- `docs/plans/README.md`: this plan's disposition.
