# UI and Interaction Requirements

Status: **Approved requirements — Home and Bluetooth panel implemented; full UI pending**

This document defines the shared visual, motion, sound, display-power, and
input-routing rules for Cardputer Hub system UI and Mini Apps. It distinguishes the delivered Home and Bluetooth panel from the full UI still
planned. The screen implements the palette, flat layout, local controls, and
Micro5 pairing digits, and horizontal page transitions. Full Launcher transitions,
audio, idle dimming, wake-input
consumption, and the Home display-power treatment are not implemented yet.

The visual and acoustic direction is derived from
[Codex Microputer ADV](https://github.com/fat23cat/codex-microputer-adv), its
[design system](https://github.com/fat23cat/codex-microputer-adv/blob/main/DESIGN.md),
its
[reference screenshots](https://github.com/fat23cat/codex-microputer-adv/tree/main/screenshots),
and its
[Micro 5 font assets](https://github.com/fat23cat/codex-microputer-adv/tree/main/assets/fonts/audition).
Cardputer Hub reuses the parts that fit a general-purpose device platform. It
does not inherit Codex-specific task slots, protocol concepts, or application
flows. The screenshots are visual evidence for this reusable grammar, not
pixel-perfect golden masters for unrelated Hub screens.

Normative words such as **must**, **should**, and **may** describe requirements
for the eventual implementation.

## 1. Scope and Ownership

These rules apply consistently to:

* boot, Home, Launcher, settings, and other System UI;
* Mini App views, including Device Manager;
* full-screen system states such as Bluetooth pairing and critical errors;
* notifications and transient overlays;
* display power management and wake behavior;
* UI sounds and input feedback.

They do not change the architectural dependency direction. Mini Apps own their
presentation and interaction, Services own reusable state and behavior,
Connectivity owns Wi-Fi and Bluetooth transport behavior, and hardware
adapters own the physical display, backlight, speaker, and keyboard calls.
User intent must still be represented as logical Actions before it reaches
application behavior or a host transport.

The UI must render state supplied by System Core, Services, or Connectivity. It
must not implement pairing, bond persistence, host switching, or HID policy
inside drawing code. Host names, selected BLE channels, and bond references
remain dynamic `HostProfile` data and must never be hardcoded.

## 2. Display Baseline

The primary target is the Cardputer-Adv 240x135 landscape LCD. Layout must be
authored for this exact raster rather than treated as a scaled desktop UI.

The interface should feel continuous with the keyboard:

* prefer full-bleed surfaces;
* use flat color, square geometry, and one-pixel separators;
* use a six-pixel baseline gutter where a screen needs an inner margin;
* avoid rounded cards, drop shadows, ornamental gradients, and excessive
  containers;
* reserve animation and strong color for changes that carry meaning.

Every resting screen must remain readable at the fallback 10% backlight level.
Important state must never be communicated by color alone; a label, mark,
shape, or value must carry the same meaning.

## 3. Color System

Cardputer Hub must use the following reference palette as named design tokens:

| Token | Value | General role |
| --- | --- | --- |
| Bone | `#F4F2EC` | Primary light surface and light foreground |
| Ink | `#17150F` | Primary text, dark surface, and error surface |
| Blue | `#1B4FD0` | Active selection, connection, or work in progress |
| Vermilion | `#E2451E` | Action required and error accent |
| Leaf | `#4CB949` | Successful or newly completed state |
| Light neutral | `#CED0CB` | Acknowledged, viewed, or completed state |
| Pale | `#DEDBD1` | Idle, disabled, or unbound state |
| Ordinal | `#ABA89D` | Quiet row numbers and secondary reference marks |
| Home wave | `#D1CEC4` | Slightly stronger ambient dotted wave on Bone |

Token meaning must stay stable between screens. Feature code must request a
semantic token instead of introducing near-duplicate colors. Ink and Bone are
the default text/surface pair; the stronger colors are accents and state
surfaces, not decoration.

## 4. Typography

Cardputer Hub must use **Micro 5** from the reference repository for prominent
numeric values, ordinals where the size permits, short identifiers, and
display-scale headings or wordmarks. Pairing codes and channel numbers are
primary examples. Small labels, instructions, and body copy should use a
compact M5 bitmap font when Micro 5 would reduce legibility.

Micro 5 must be embedded as monochrome glyph masks rendered at their intended
LCD size. Runtime TTF parsing, antialiasing, and arbitrary font scaling are not
required. Glyphs sharing a role must use one optical size and baseline.

The font is distributed under the SIL Open Font License 1.1. Its copyright and
license notice must be stored beside the source font or generated glyph assets.
Generated masks remain traceable to the exact font revision and generation
tool.

Small system labels should be concise, normally uppercase, and may use one
pixel of tracking. Copy must not be shrunk merely to fit; shorten the text or
use a deliberate second line.

## 5. Layout and Components

Most functional screens should use three stable horizontal zones:

1. a compact header with the screen title at the left and one contextual
   status or dismissal hint at the right;
2. a content area that owns the remaining height;
3. a compact footer, separated by a one-pixel rule, that labels the physical
   keys available on the current screen.

Footer hints must describe actual controls rather than generic software
affordances. The primary back or close control belongs at the left, navigation
hints in the middle, and the primary confirm or apply control at the right.
Unavailable controls must be omitted rather than shown as if active. A screen
may omit the standard header and footer only for a deliberate full-screen
state such as boot, pairing, recording, direct manipulation, or a critical
takeover.

List-based screens should use one column of stable rows. The preferred row
grammar is:

```text
two-digit ordinal    tracked label                  right-aligned value
```

Focus is represented by one square-ended Ink selection plate that travels
between rows. The focused row is redrawn as the same content clipped and
inverted through that plate; it must not be a separately positioned duplicate
that can drift from the background row.

Information hierarchy must remain visible without extra containers:

* titles and primary labels use Ink on Bone;
* ordinals, inactive instructions, and reference metadata use the quieter
  Ordinal token while remaining legible at 10% brightness;
* the selected item uses a solid Ink plate with Bone content;
* values align to a stable right edge, and labels must not move when values
  change;
* one-pixel rules define structural regions; borders must not be repeated
  around every item unless the content is genuinely a matrix.

When a matrix is appropriate, cells must use a fixed shared grid, retain their
boundaries in every state, and use the same solid inversion language for
focus. Direct-manipulation screens should centre one dominant control or value,
keep directional hints spatially aligned with their physical input, and avoid
competing chrome.

Reusable components may include:

* the travelling selection plate for lists and menus;
* discrete segment meters with visible empty seats;
* discrete one-of-N segment selectors;
* a top-edge transient annunciator for exceptional state;
* a full-screen takeover for short, high-priority interactions;
* a quiet build stamp on the splash or About view.

Boot, pairing, and other sparse identity or setup screens may use restrained
registration-corner marks and isolated one-pixel texture. These marks are
framing, not controls, and must not appear as decoration throughout functional
screens. Full-screen states should use one dominant value or status label, one
short instruction, and a single semantic surface color whenever that is enough
to explain the state.

A page opens with its selection already in the correct resting position. Page
entry itself must not make the selection plate travel from an arbitrary origin.

Codex Microputer ADV's six full-height task slots, task-status takeovers, and
Codex protocol indicators are product-specific and are not Cardputer Hub
requirements. A future Hub screen may use a grid only when its own content and
physical controls justify one.

## 6. Motion and Transitions

Motion must be event-driven except for the explicitly approved Home ambient wave
described below. Settled functional screens must stop requesting animation
frames, and background polling must not make the interface twitch or repaint
continuously.

The common motion grammar is:

* direct key feedback reaches its full visual response on the first rendered
  frame after contact, then eases out;
* list focus uses a damped spring while rows remain stationary;
* horizontal page changes use one short cubic-eased slide, approximately
  220 ms on the target hardware;
* full-screen takeovers grow from the element or region that originated the
  event when that spatial relationship is meaningful;
* dismissal reverses the entrance instead of introducing an unrelated exit;
* repeated publication of unchanged state produces no animation;
* a newer event for the same subject may replace stale queued feedback, while
  independent user-visible events preserve arrival order.

Animation state must advance from injected monotonic elapsed time and remain
testable without physical hardware. Rendering, input polling, Connectivity,
and sound preparation must remain non-blocking. If the frame budget cannot
support an effect reliably, simplify the effect without changing its semantic
timing or input behavior.

Sound and motion triggered by a state change must start from the same accepted,
debounced semantic event. Raw callbacks must not independently trigger one and
leave the other behind.

## 7. Sound

Cardputer Hub must port the applicable synthesized cue implementation and
audible language from Codex Microputer ADV rather than approximate it with
plain `tone()` beeps or unrelated recordings. The same synthesis algorithms,
envelopes, interval and rhythm grammar, transposition behavior, and default
`CLOUD` startup composition should be retained for equivalent events.

The applicable cue roles are:

* boot;
* selection movement;
* menu open and apply;
* value step left and right;
* attention or confirmation required;
* success;
* error;
* return to idle;
* unmute.

Codex-specific task cues need not be compiled until Cardputer Hub has an
equivalent semantic event. The UI must not invent a sound merely because it
repainted.

Audio must be synthesized or prepared away from the render and input critical
paths and played asynchronously. It must not block Bluetooth, USB, keyboard
polling, or animation. The implementation must respect the Cardputer speaker's
useful register and preserve continuous attack and decay envelopes. If the
reference renderer cannot fit the available memory or CPU budget, the audible
result should be preserved with bounded precomputed data rather than replaced
with a generic beep.

Sound must have a persistent mute control and a persistent 0-100% volume in
10% steps. The initial default is 60%, matching the reference output level.
Wake-only input must not play an action or key-confirmation sound.

Any source code adapted from the reference repository must retain the notices
required by its
[Apache 2.0 license](https://github.com/fat23cat/codex-microputer-adv/blob/main/LICENSE).

## 8. Display Power and Idle Behavior

Display power is a three-state policy independent of application navigation:

| State | Local fallback behavior |
| --- | --- |
| Awake | Use the configured normal brightness while activity is recent |
| Dimmed | After 15 seconds without activity, smoothly reach 10% of configured normal brightness |
| Off | After remaining at the readable 10% level for a full 3 minutes, smoothly reduce the backlight to zero |

The three-minute dim hold begins after the display reaches its 10% target; it
must not be shortened by the fade time. Brightness changes must use a short,
monotonic, non-blocking ramp rather than an abrupt step. Waking uses the same
ramp in reverse.

The local fallback applies whenever no connected external system owns a
supported brightness and auto-dim policy. If a future host policy is active,
the UI may follow it; disconnecting that host must restore the local fallback
without changing the user's configured normal brightness.

Any intentional physical input while the display is Dimmed or Off must:

1. restore the display toward normal brightness;
2. reset the inactivity timer;
3. be consumed before Action routing or HID transport;
4. produce no command, navigation, text entry, or confirmation side effect.

The next input after wake is handled normally. Wake consumption belongs in the
shared input-routing path so every screen and Mini App behaves identically.

Background polling, idle animation, and unchanged Service state are not user
activity. A deliberate foreground notification may wake the screen only when
its product requirement says that it is important enough to interrupt idle.
Turning off the backlight must not disable Connectivity, Services, or input
polling.

### Splash-only low-brightness treatment

Only a motionless splash or attract screen may alter framebuffer colors during
automatic dimming. On that screen:

* genuinely light Bone surfaces remain light in the framebuffer;
* colored and dark elements smoothly collapse toward black as brightness
  decreases;
* the treatment is disabled during transitions and on functional screens.

Menus, pairing, notifications, and Mini Apps retain their literal semantic
colors and use backlight dimming only. This prevents a low-power transform from
changing the meaning of active UI state.

## 9. BLE Host Control and USB Diagnostics

BLE is the only implemented host-control transport. Settings has no Active
channel control, Auto mode, or persisted USB/Bluetooth output preference.
Device Manager selects a BLE Host Profile through the shared Action and
HostService infrastructure; pairing and connection state belong to
BluetoothService, not to the UI.

A selected BLE host's availability must be visible separately from selection.
If it is disconnected or not HID-ready, host-control Actions report
unavailability without sending to another host. Reconnection must not replay
interrupted commands. Releases belong to the original selected peer.

USB supports charging, firmware installation, and serial diagnostics only.
Cable attachment or removal must not change the selected BLE host, open
pairing, clear bonds, or interrupt the local UI through transport policy.
There is no USB HID output and no automatic transport switching.

Wake-only input is consumed before host-control Action dispatch. Background
connection changes may update a passive status indicator, but must not change
focus, navigate away, or open a blocking popup. Future transport selection UI
requires an explicit architecture revision when another transport is added.

Plan 017 brings forward HostService and host configuration from Phase 3 and Home with a Bluetooth panel from Phases 4/6. It supports pairing, selection,
rename, and persisted BLE On/Off. The saved selection is marked SELECTED, separately from OFF/CONNECTING/SECURING/READY status. Background
connection updates preserve focus. Settings keys remain local. In lists, the
physical Up/Down-marked keys (`;` / `.`) navigate without Fn; in name/code entry
the same keys retain their text-input semantics. Existing named arrow events
remain supported. Focus movement redraws only changed rows; status, footer, and
error updates redraw their own regions. Scrolling must not clear the whole LCD.
Returning from a modal invalidates the list cache and paints the list again.

Home is the default root view. The approved dashboard has an eight-pixel gutter,
a compact top line for time, one Wi-Fi icon and status (no network name), and
battery percentage (without an icon) above a one-pixel rule at y=24. Time is `--:--` until a
clock source is implemented; Wi-Fi is `OFFLINE` because normal firmware does
not yet compose Wi-Fi connectivity. BatteryService supplies the hardware's
estimated percentage, or `--%` when unavailable. No demo telemetry is rendered.

Below it, SELECTED HOST is a quiet label at y=38. The selected name uses Micro 5
at optical size 36, with uppercase ink at y=55..70; long labels are truncated
with an ellipsis within 224 pixels. This display conversion does not rename the
stored profile. No selection is labelled NO HOST SELECTED. One Bluetooth icon
and an explicit OFF/CONNECTING/SECURING/PAIRING/READY/ERROR label sit below the
name. The icon uses Ordinal for Off, Blue for connection/pairing, Leaf for Ready,
and Vermilion for Error; the label stays Ink. There is no title or footer on Home.

A low-contrast Home wave dotted texture moves only in y=99..134, with a 28-second cycle
and at most two frames per second. It advances from injected monotonic elapsed
time, pauses whenever Home is hidden, never moves text or icons, and redraws
only that lower region. This is an explicit exception to the event-only motion
rule. It must eventually pause while the display is off and must never count as
user activity. Battery updates repaint only their header region; host/status
updates repaint only the host region. Pairing's Micro 5 digits are unchanged.

Tab on the main keyboard (also G0 or Fn+Tab) opens a general SETTINGS list with a single implemented Bluetooth entry,
using the existing ordinal and Ink focus plate, without a bottom bar or
Esc Home label. Enter opens
the existing Bluetooth panel without modifying its controls or layout. Its Esc
Home behavior is retained; Tab, G0 or Fn+Tab from the BLE list returns to Settings. The
menu input is consumed without dismissing host submenus, rename/delete prompts,
or pairing. Enter/B do not open anything on Home. Repeated Tab/G0/Fn+Tab
in Settings does not add history entries or repaint a settled view. Background
connection updates never navigate away from the current screen. G0 uses the
debounced press edge from M5Unified BtnA and emits the local SystemMenu input;
holding it does not repeat. Its hardware boot/download function is unchanged. Plain Tab is a built-in
system-screen control, not a global shortcut for future text-entry Mini Apps.

The Cardputer adapter composes drawing into a persistent RGB565 canvas and copies
only completed dirty regions to the LCD. Settled functional views cause no display transfer; Home transfers only its
changed regions, including the bounded ambient wave.
Page changes use a 220 ms cubic ease-out slide: forward navigation enters from
right, Back from left. Home, Settings, Bluetooth, host actions, rename/delete,
and pairing participate; focus moves, typed characters, and status updates do
not restart transitions. The Home wave pauses during a slide. Input remains
live; a newer navigation transition starts from the currently presented pixels.
No event is queued for later host replay. Animation positions update at most
once per 16 ms and stop at completion. Spring focus motion, sound, and
display-power policy remain pending. If canvas allocation
fails, the adapter retains direct drawing as a usable fallback. Full Device
Manager integration and Phase 7 Action-to-HID mappings remain planned.

BLE Off is the explicit persistent disconnect control on Cardputer. While BLE
is On, the selected host may reconnect automatically, including after macOS's
Disconnect command. Other saved hosts must not take over. Physical acceptance
of filtering and the final UI is tracked in plan 017.

## 10. Verification Requirements

UI implementation is not complete until the relevant behavior is verified.
Host-side tests must cover at least:

* palette-token and layout-state selection;
* deterministic transition endpoints and interrupted transitions;
* stationary screens ceasing to request frames;
* 15-second dim timing, the full three-minute dim hold, and final off timing;
* wake-only consumption from both Dimmed and Off;
* splash-only color collapse preserving light pixels and darkening non-light
  pixels;
* selected BLE host readiness, unavailable-target feedback, neutral release,
  and no stale output after disconnect/reconnect;
* USB diagnostics remaining independent of BLE host selection;
* sound cue selection, mute, volume, and non-blocking dispatch.

Representative stable frames should be capturable without altering live state
so splash, Launcher, list, modal, pairing, notification, dimmed, and error
screens can be reviewed as images. Automated tests should validate behavior and
geometry; screenshots provide visual review and must not replace those tests.

Physical Cardputer-Adv validation must confirm:

* text and focus remain readable at 10% brightness;
* dim, off, and wake ramps do not flash or expose a stale frame;
* the first dimmed/off input is consumed and the second operates normally;
* motion remains smooth while Connectivity is active;
* cue timing, volume, and speaker output match the intended reference;
* BLE reconnects and selected-host changes produce no duplicate commands or
  stale presses, and release neutralizes controls on the original peer;
* USB cable cycles preserve BLE selection and bonds while local input and
  display remain responsive; serial diagnostics return after reconnection.

## 11. Current-Firmware Boundary

This document includes delivered behavior and future requirements. Home,
Settings/Bluetooth, per-host menus, partial frame presentation, page slides,
the ambient wave, and the two-second segmented startup splash are implemented.
The splash uses Bone, Ink, Blue, Pale, and Ordinal tokens, keeps the firmware
version visible throughout, and advances without blocking background work.
Launcher integration, spring focus motion,
sound, dim/off/wake policy, and live clock/Wi-Fi composition remain open in the
[phase checklist](ARCHITECTURE.md#47-initial-development-order). The manuals
describe current operation; planned behavior must not be presented there as
already supported.

### First Bluetooth enable

When no host is selected, the Bluetooth row directs focus to a saved host (or
Add device for an empty list) with a visible Enter instruction. Focus alone does
not select a host or start pairing. Home displays OFF until the user confirms;
missing selection and invalid input are not Bluetooth ERROR states. Actual
storage, Bluetooth, and missing-bond failures retain the ERROR treatment.

Saved host intent is labelled SELECTED, never ACTIVE; selection remains visible
while Off or after a failed connection attempt. READY denotes the actual secured
HID connection. Home labels the corresponding name SELECTED HOST.

Settings and the Bluetooth list have no bottom separator or Esc Home footer.
Escape/backtick still returns Home; arrow and Enter controls remain unchanged. Enter on a saved host opens a
Connect / Rename / Delete menu with Esc Back; opening it does not connect or
change selection. The current host name appears below the actions. Rename and
Delete use Esc Cancel; Delete shows the exact host name and needs confirmation.
Back from either returns to the host menu, then the Bluetooth list, then Home.
The global X / Forget all hosts UI and Action are removed. Host action list
navigation retains incremental painting and the shared palette/font geometry.
