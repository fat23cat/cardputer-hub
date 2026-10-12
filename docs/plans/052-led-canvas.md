# 052 — LED CANVAS: draw on the 8×8 matrix from the web UI

## Current status

Implemented on **2026-10-12**. Final `make host-check` and `make firmware-check`
passed after the color synchronization correction (the two components of `make check`).
Host evidence: architecture and format checks, static analysis (no high/medium
findings), 968 native tests passed, 125 Python tests OK with one existing skip,
including the Node checks for the real page model and request lifecycle.
The C++/JavaScript color parity check passed for all seven modes. Color stamps
keep browser/device phases consistent after Clear, reload, dropped intermediate
samples and other browsers; stationary pointers retain their own hue.
The production image compiled and its effective ESP-IDF configuration passed.
Desktop and 390 px mobile browser layouts, drawing and APP CLOSED/reopen were
also checked against mock local HTTP endpoints. Native LCD captures were rendered
and inspected.

Delivered: Mini App and registration, fixed input/playout Service, strict web
endpoint, pure seven-mode engine, shared network join QR presentation, mobile
and desktop LED page, bounded batched sender and local preview, tests, and
architecture/UI/manual updates. No optional pre-touch demo is included.

Review corrections: Clear suppresses stroke sends while waiting, discards retries
created by a failing in-flight request, and resets the browser sample clock.
Client packet ordering now survives backlog. Regression tests reproduce both
review findings and check fresh input after Clear.

Physical acceptance in section 9 remains pending: contact reporting on phones,
halo visibility at 1%/3%, device latency, two phones and Pomodoro handoff.
No device was flashed during this implementation.

The behavior was agreed from interactive mockups before this plan was written.

### Mockups

- The "Hub LED Canvas" design canvas:
  <https://claude.ai/artifact/4fzFXBmarrwStVR1nsQyKS> (private to its owner
  until shared). Its artboards: phone and desktop LED pages, the color-mode
  sheet, the phone page while the Mini App is closed, and three Cardputer
  screens drawn at 3×.
- The same artboards as source in [`052-mockups/`](052-mockups/). They need
  the canvas runtime to render, so read them as markup and JavaScript. The
  `<script>` block of `Main.dc.html` is the reference implementation of the LED
  model in section 3; `Desktop.dc.html` adds hover drawing.

### Decisions taken before planning

| Decision | Reason |
| --- | --- |
| A separate LED CANVAS Mini App owns the matrix while it is open | Matrix ownership is then the existing claim arbitration: one ForegroundApplication claim, released on close, Pomodoro or any lower claim returns. No new ownership rule. |
| The page sends touch points, never frames | Colors, fade and halo are computed on the Cardputer, so the matrix stays smooth when Wi-Fi is slow. The page draws its own preview with the same model for instant feedback. |
| Batched HTTP POSTs, no WebSocket | `Esp32HttpServer` answers one request at a time and has no WebSocket support; a batch every ~60 ms is enough. |
| The LED under the finger is at full brightness, neighbors glow dimmer | Requested, to imitate light spill. |
| Brush AUTO / FINE / WIDE; AUTO uses the browser's contact size | Fingers differ. iOS Safari may report no or a fixed contact size, so AUTO trusts the size only after it varied, else uses WIDE. |
| Seven color modes plus multi-touch | Agreed on the mode sheet: Rainbow, Embers, Velocity, Mix, Ripple, Kaleidoscope, Solid. |
| Brush is a per-browser preference, not shared state | It only changes the radius the page sends. Mode, fade, color and hold are shared with the Cardputer. |
| Enter on the Cardputer starts WEB ACCESS when it is off | The app is useless without the server; the start goes through the existing `web.set-enabled` Action. |

## 1. Goal

A phone or computer signed in to the WEB ACCESS page opens an **LED** tab and
draws on the Unit Puzzle 8×8 matrix with a finger or the mouse. The matrix
lights up and fades smoothly, following the movement, with several color modes.
The Cardputer shows a LED CANVAS screen that explains what to do and what is
happening.

## 2. Scope

- **LED CANVAS Mini App** in APPS, after LED GALLERY. Always available, like
  LED GALLERY, with or without a Unit Puzzle.
- **LED web app** (`IWebApp` id `led`, title `LED`): a start tile, a header
  tab and the `#/led` page in `web/index.html`, phone and desktop layouts as
  in the mockups.
- **Drawing:** touch, pen and mouse through Pointer Events. Multi-touch, up to
  five pointers per browser. Desktop: drawing on hover (default on, a
  checkbox turns it off) or with the button held.
- **Modes:** Rainbow, Embers, Velocity, Mix, Ripple, Kaleidoscope, Solid (seven
  colors). **Fade:** Short, Medium, Long. **Hold** stops fading. **Clear**
  empties the matrix.
- **Brush:** AUTO, FINE, WIDE, with a halo in every mode.
- **Several browsers at once:** up to four (the WEB ACCESS session limit);
  their strokes mix on the matrix.
- **Cardputer keys:** Enter starts WEB ACCESS when it is off or in error,
  Space toggles Hold, `del` (Backspace) clears, Q shows the WEB ACCESS join QR
  code, Escape closes the app and leaves WEB ACCESS running.
- Shared settings live in RAM for the boot; nothing is persisted.

## 3. Architecture

```text
Browser (#/led page: pointer capture, local preview, batch sender)
   │  POST /api/led/strokes, PUT /api/led/settings, POST /api/led/clear, GET /api/led
   ▼
WebAccessService (unchanged: sign-in, sessions, routing)
   │  IWebApp::handle()
   ▼
LedCanvasWebEndpoint                         services/led_canvas
   │  validate, decode
   ▼
LedCanvasService                             services/led_canvas
   ├─ active flag (set by the Mini App)
   ├─ shared settings + revision
   ├─ clients (≤4) and pointer slots (≤5 each)
   ├─ playout queue (fixed size) and clear generation
   └─ Actions: led-canvas.set-hold, led-canvas.clear
   ▲  takeDue(), status()
   │
LedCanvasApp (Mini App)                      apps/led_canvas
   ├─ LCD screen
   ├─ LedCanvasEngine (heap, while active)   apps/led_canvas
   └─ IndicatorClaim (ForegroundApplication) ──► IndicatorService ──► ILEDAdapter
LedCanvasApp ──ActionBus web.set-enabled / led-canvas.*──► services
LedCanvasApp ──status()──► WebAccessService
```

Everything runs on the main loop: `webAccess.update()` delivers requests to the
endpoint; the active Mini App's `update(elapsed)` drains due points into the
engine and publishes a frame. No locks are needed.

### HTTP API

Same rules as plan 050: session cookie, `X-Hub-Request: 1` on every non-GET,
errors as `{"error":"<code>"}`, bodies ≤ 5 KiB.

| Method and path | Result |
| --- | --- |
| `GET /api/led` | `{"open":bool,"revision":N,"fingers":K,"settings":{"mode","fade","ink","hold"}}` |
| `POST /api/led/strokes` | body below; 200 `{"open":true,"revision":N,"fingers":K}`; 409 `{"error":"app-closed"}`; 400 |
| `PUT /api/led/settings` | full `settings` object; 200 with the `GET` object; 400. Allowed while closed. |
| `POST /api/led/clear` | 204; 409 `app-closed` |

The start tile's `summaryJson()` is `{"open":bool,"fingers":K}`.

Stroke batch:

```json
{"client":"9f2c41d0","seq":42,"p":[[slot,x,y,r,dt,flags,hue,mixInk], ...]}
```

| Field | Meaning |
| --- | --- |
| `client` | 8 hex digits, random per page load; maps to one of four client slots (least recently used is replaced) |
| `seq` | increases per batch; a batch with `seq` ≤ the last seen for that client is answered 200 and ignored (a retry) |
| `slot` | pointer slot 0–4, assigned by the page from `pointerId` |
| `x`, `y` | 0–2047, position in 1/256 LED (0 = left/top edge of LED 0) |
| `r` | 5–29, brush radius in 1/16 LED |
| `dt` | 0–255 ms since the previous point of this batch (the first: since the previous batch's last point, capped) |
| `flags` | bit 0: stroke starts; bit 1: stroke ends |
| `hue` | ending hue phase 0–9999; one turn is 10000; prevents browser/device cycle drift |
| `mixInk` | stroke ink 0–2, cycling red/green/blue within each browser |

The page supplies the two color-state fields with every point. Legacy six-field
points remain accepted and use the engine counters. RGB, the halo, interpolation
and fading are still calculated on the Cardputer; no frames are sent.

At most 64 points per batch. The page keeps one stroke request in flight,
sends every ~60 ms or at 48 points, merges points while a request is pending,
and drops intermediate points (never a start or end) beyond 64.

### Playout

The service stamps a batch with its arrival time and schedules each point at
`max(arrival + 100 ms, client last scheduled time) + Σdt`. This prevents a new
batch from overtaking pending Move/End points of the same client while other
clients remain independent. Clear, app close and server reset discard the
queued timeline. `takeDue(elapsed)` advances the service clock and
returns the points now due, in order, so the matrix replays the stroke with its
original rhythm despite network jitter. The queue holds 256 points; when it is
full the oldest are dropped. A pointer without points for 500 ms is ended. A
start for a slot that is still drawing ends the old stroke first.

### LED model

`LedCanvasEngine` owns fixed buffers: 64 RGB values, 64 heat values (Embers),
at most 10 ripple rings, the hue, and per-pointer state (position, LED,
smoothed speed, stroke number, radius). Output is full-range logical RGB; the
global brightness limit stays in `IndicatorService`. The page's preview uses
the same model and these constants (reference: `052-mockups/Main.dc.html`).

| Item | Value |
| --- | --- |
| Frame rate | publish ≈30 FPS while anything is lit or a pointer is down; stop when dark |
| Stroke sampling | between two points, one brush step per 0.5 LED |
| Brush weight | LED under the point: 1. Distance `d` from the point to an LED center ≤ `r`: 1. Else `0.45 × (1 − (d − r) / 1.3)`, ≥ 0 |
| Applying a weight | 1: replace the LED's color. Below 1: per-channel maximum with `color × weight` |
| Brush radius | FINE 0.40, WIDE 1.05, AUTO `contact / 2 / pitch + 0.1` clamped to 0.3–1.8; the radius is smoothed 0.7 old / 0.3 new |
| AUTO trust | touch only, after at least two distinct rounded contact sizes; before that WIDE; mouse always FINE |
| Fade | every frame, RGB × `exp(−dt / fade)`, values below 2/255 become 0; fade = 0.35 / 1.0 / 3.0 s |
| Hold | no fading, no heat decay; rings keep moving |
| Speed | smoothed `0.6 old + 0.4 new` LED/s; decays `× exp(−5 dt)` while the pointer is still for > 60 ms |
| A pointer held still | repaints at its position every frame |

| Mode | Rule |
| --- | --- |
| Rainbow | hue +0.04 per newly entered LED; color HSV(hue, 1, 1) |
| Embers | heat = max(heat, weight); heat − dt / (fade × 1.4); color from the ramp black → (110,8,0) → (226,60,20) → (255,150,30) → (255,236,190) |
| Velocity | color from the ramp (20,50,200) → (0,170,220) → (230,60,30) → (255,190,40) → (255,255,235) at speed / 16 |
| Mix | only on entering an LED: add stroke ink (red (255,30,20), green (30,255,40), blue (30,70,255) by each browser's stroke number) × weight, halo × 0.5, saturating |
| Ripple | white brush; on entering an LED, hue +0.11 and a ring of HSV(hue) from the point: radius 6 LED/s, width 0.9 LED, life 1.3 s, strength × (1 − age / life), per-channel maximum |
| Kaleidoscope | hue +0.025 per entered LED; each weighted LED and its three mirrors get hue + 0, 0.25, 0.5, 0.75 |
| Solid | the selected ink: Vermilion (255,70,30), Amber (255,176,0), Leaf (70,220,60), Cyan (0,200,255), Blue (40,90,255), Violet (180,70,255), White (255,255,255) |

Rainbow and Kaleidoscope retain the last entered hue for each stationary
pointer. The ending phase stamps let the engine reconstruct the sampled path
without inheriting another browser's hue. Clear resets the hue and stroke count
on both sides. A two-language parity test compares logical RGB within one
channel unit across all seven modes.

Mode ids: `rainbow`, `embers`, `velocity`, `mix`, `ripple`, `kaleidoscope`,
`solid`. Fade ids: `short`, `medium`, `long`. `ink` is 0–6.

### Cardputer screen

Standard header `LED CANVAS`; right status as below. Layouts follow the 3×
artboards (divide coordinates by 3).

| State | Content | Footer |
| --- | --- | --- |
| WEB ACCESS Off | header status `WEB OFF`; 2× `WEB ACCESS OFF`; quiet `THE PHONE DRAWS OVER WI-FI` | right `ENTER START` |
| WEB ACCESS Error | header status `WEB ERROR`; 2× `WEB ACCESS ERROR`; the same quiet line | right `ENTER RETRY` |
| Waiting | header status = client count as WEB ACCESS shows it; rows `NETWORK`, `OPEN 192.168.4.1`, `TAB LED`; Blue mark + `WAITING FOR A TOUCH` | left `Q JOIN CODE` |
| Live (a pointer is down or a point arrived in the last 3 s) | Leaf mark + `LIVE  N FINGERS` (`1 FINGER`); 2× mode name; quiet `FADE MEDIUM`, plus `  HOLD` while held | left `SPACE HOLD` (`SPACE RESUME` while held), right `DEL CLEAR` |

Q shows the same full-screen join code as WEB ACCESS; extract its drawing into
a shared presentation helper rather than duplicating it. The LCD never mirrors
the matrix.

### Web page

Follow `052-mockups/Main.dc.html` (phone) and `Desktop.dc.html`: dark panel of
64 LEDs with glow, caption, MODE chips (desktop: a grid with descriptions),
color swatches in Solid, BRUSH with a note (`One LED with a soft halo`, `Five
LEDs with a soft halo`, `Your finger covers 1.4 LEDs`, `Size not reported here,
using Wide`), FADE with HOLD, CLEAR PANEL. When the app is closed, show
`PhoneClosed.dc.html`: APP CLOSED mark, dark panel, `Open LED CANVAS on the
Cardputer`, and poll `GET /api/led` every 2 s until it opens. While open and
idle, poll every 2 s; stroke responses carry `revision` and `open`, and a
changed revision refetches settings. The mockups' self-drawing preview before
the first touch is optional. The brush choice is kept in `localStorage`.

## 4. Ownership & Boundaries

| Component | Owns | May call | Must not call |
| --- | --- | --- | --- |
| `app_main` | object lifetime, wiring, `addApp(ledCanvasWeb)`, Action registration | every layer | stroke logic |
| `LedCanvasWebEndpoint` | the `/api/led` HTTP mapping and JSON validation | `LedCanvasService` | `IndicatorService`, `LedCanvasEngine`, `IHttpServer` |
| `LedCanvasService` | active flag, settings and revision, clients, pointer slots, playout queue, clear generation, `led-canvas.*` Actions | nothing below Services | `IndicatorService`, display, `WebAccessService`, Mini Apps |
| `LedCanvasEngine` | LED buffers and the model in section 3 | — (pure) | any Service, hardware |
| `LedCanvasApp` | LCD state, engine lifetime, one ForegroundApplication claim | `LedCanvasService::setActive/takeDue/status`, `WebAccessService::status()`, ActionBus, `IndicatorService` through its claim | `WebAccessService` mutators, `IHttpServer`, `ILEDAdapter` |
| `WebAccessService` | unchanged | `IWebApp` | anything LED |

## 5. State Model

```text
LedCanvasService
Closed ──setActive(true)──► Open ──setActive(false)──► Closed
           (clears queue,        stroke batches accepted;
            pointers, engine)    Closed answers 409 app-closed

LedCanvasApp screen (while active)
WebOff ◄──► WebError        (WebAccessService state)
   │ Running
   ▼
Waiting ──point arrives──► Live ──3 s without points and no pointer down──► Waiting
```

Leaving `Running` in WEB ACCESS returns the screen to WebOff or WebError and
ends every pointer; the matrix fades out normally.

## 6. BDD Scenarios

### Scenario: Draw a stroke (happy path)

Given:
- WEB ACCESS is Running, LED CANVAS is active, a browser is signed in

When:
- the page posts a batch with a stroke start at (3.5, 3.5), r 0.75, mode Rainbow

Then:
- the endpoint answers 200 with `open: true`
- 100 ms later the engine paints LED (3,3) at full brightness and its
  neighbors with the halo weight
- the claim publishes a frame, and the screen shows `LIVE  1 FINGER`

### Scenario: App closed (unavailable dependency)

Given:
- WEB ACCESS is Running, LED CANVAS is not active

When:
- the page posts a stroke batch

Then:
- the endpoint answers 409 `app-closed`, nothing is queued
- `GET /api/led` reports `open: false`, and the page shows APP CLOSED

### Scenario: WEB ACCESS off when the app opens (unavailable dependency)

Given:
- WEB ACCESS is Off

When:
- the user opens LED CANVAS and presses Enter

Then:
- the app dispatches exactly one `web.set-enabled` with `enabled = true`
- the screen changes to Waiting when the service reports Running

### Scenario: Server stops while drawing (in-flight failure)

Given:
- LED CANVAS is Live with a pointer down

When:
- WEB ACCESS stops

Then:
- every pointer ends, the queue is cleared, the matrix fades out
- the screen shows WebOff; the app stays open and keeps its claim

### Scenario: Reconnect

Given:
- a page drew, lost Wi-Fi and rejoined with the same session

When:
- it posts a batch with a new `seq`

Then:
- the batch is accepted; the earlier pointer of that client had ended after 500 ms

### Scenario: Retried batch (repeated input)

Given:
- a batch with `seq` 42 was accepted

When:
- the same batch arrives again

Then:
- the endpoint answers 200, and no point is queued twice

### Scenario: Batch after the app closed (stale completion)

Given:
- points are queued for playout

When:
- the user presses Escape

Then:
- the claim is released, the queue and pointers are cleared
- a lower claim (for example Pomodoro) is visible again
- WEB ACCESS keeps running; the next batch gets 409

### Scenario: Two browsers

Given:
- two signed-in browsers with different `client` ids, LED CANVAS active, mode Mix
- one browser is on its red stroke and the other on its green stroke

When:
- both draw at once, slot 0 each

Then:
- the strokes are separate pointers, and `fingers` is 2
- where a red and a green stroke cross, the LED is yellow

### Scenario: Hold and clear from the Cardputer

Given:
- LED CANVAS is Live

When:
- the user presses Space, then `del`

Then:
- `led-canvas.set-hold` sets `hold = true` and increases the revision; the page shows HOLD
- `led-canvas.clear` empties the engine; the footer shows `SPACE RESUME`

### Scenario: Settings from the page

Given:
- LED CANVAS is active

When:
- the page sends `PUT /api/led/settings` with mode `embers`

Then:
- the revision increases, the engine switches mode without clearing the matrix,
  and the screen shows `EMBERS`

### Scenario: Queue full

Given:
- 256 points are waiting

When:
- another batch arrives

Then:
- the oldest points are dropped, the batch is accepted, and memory does not grow

## 7. Architecture Invariants

- Only `IndicatorService` writes LED hardware; LED CANVAS uses one
  ForegroundApplication claim, held only while the app is active.
- The page sends touch points and settings; the Cardputer never receives or
  sends frames.
- `LedCanvasService` accepts points only while the app has called
  `setActive(true)`.
- `LedCanvasWebEndpoint` never calls `LedCanvasEngine` or `IndicatorService`.
- `LedCanvasApp` starts WEB ACCESS only through `web.set-enabled`; Escape
  never stops it.
- `WebAccessService` is not changed for this plan except by registering the app.
- Service memory is fixed: four clients, five pointer slots each, 256 queued
  points, ten rings; nothing grows with traffic.
- The engine is deterministic and host-testable; the page's preview uses the
  constants in section 3.
- No personal device or host is special-cased.

## 8. Tests mapped to scenarios

| Scenario | Test |
| --- | --- |
| Draw a stroke | `test_led_canvas_service`: `test_batch_is_played_after_delay`; `test_led_canvas_engine`: `test_brush_center_full_and_halo_dimmer` |
| App closed | `test_led_canvas_web`: `test_strokes_rejected_while_closed` |
| WEB ACCESS off | `test_led_canvas_app`: `test_enter_starts_web_access_once` |
| Server stops while drawing | `test_led_canvas_app`: `test_web_stop_ends_pointers_and_shows_off` |
| Reconnect | `test_led_canvas_service`: `test_silent_pointer_ends_after_500ms` |
| Retried batch | `test_led_canvas_service`: `test_repeated_seq_is_ignored` |
| Batch after close | `test_led_canvas_app`: `test_escape_releases_claim_and_clears_queue` |
| Two browsers | `test_led_canvas_service`: `test_clients_get_separate_pointers`; engine: `test_mix_red_and_green_make_yellow` |
| Hold and clear | `test_led_canvas_service`: `test_hold_action_bumps_revision`; app: `test_space_and_del_dispatch_actions` |
| Settings from the page | `test_led_canvas_web`: `test_settings_put_validates_and_bumps_revision` |
| Queue full | `test_led_canvas_service`: `test_full_queue_drops_oldest` |
| Backlogged batches preserve client order | `test_led_canvas_service`: `test_same_client_batches_cannot_overtake_pending_end`; `test_later_arrival_and_clear_reset_the_playout_timeline` |
| Clear during a failed stroke request | `test_web/led_canvas.cjs`: Clear/retry race, no stroke requests while Clear waits, fresh sample clock afterwards |
| Color synchronization | engine: clear restart, supplied phase, separate stationary pointers and Mix ink; web: bounds/legacy decoding; `test_python.test_led_canvas_ui`: real C++/JS RGB parity |
| Engine modes | `test_led_canvas_engine`: one test per mode rule, fade, hold, sampling step |
| Batch decoding limits | `test_led_canvas_web`: out-of-range fields, > 64 points, missing marker header |
| Dependency direction | `scripts/check_architecture.py` and `test_python/test_web_architecture.py` |
| Page asset in step | `test_python/test_web_ui_asset.py` after `scripts/generate_web_ui.py` |

## 9. Physical Acceptance

- iPhone Safari: draw with a thin and a thick finger; record whether AUTO
  becomes trusted or stays WIDE, and the note it shows.
- An Android phone, if available: AUTO follows the finger size.
- Desktop browser: hover drawing and button drawing.
- Each mode on the Unit Puzzle at the default 3% and at 1% brightness; the halo
  must stay visible. If low brightness rounds it away, tune the halo weights.
- Latency and smoothness of a fast stroke; no visible stutter from the 100 ms
  playout delay.
- Two phones drawing at once.
- Pomodoro running: open LED CANVAS, draw, press Escape; Pomodoro's frame
  returns.
- Escape leaves WEB ACCESS running; the phone page shows APP CLOSED and wakes
  when the app is reopened.

## 10. Out of Scope

- WebSocket or any streaming transport; frames sent back to the page.
- Saving, replaying or sharing drawings; animation recording.
- Changing modes from the Cardputer keyboard (a possible follow-up: `,` / `/`).
- Pressure or force input.
- Persisting settings across reboots.
- Mirroring the matrix on the LCD.
- Changes to `IndicatorService` brightness or gamma.
