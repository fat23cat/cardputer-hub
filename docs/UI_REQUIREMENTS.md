# UI and Interaction Requirements

Status: **Approved requirements — planned, not yet implemented**

This document defines the shared visual, motion, sound, display-power, and
input-routing rules for future Cardputer Hub system UI and Mini Apps. It does
not describe behavior supported by the current boot-only firmware.

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
Connectivity owns USB, Wi-Fi, and Bluetooth transport behavior, and hardware
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

Motion must be event-driven. A settled screen must stop requesting animation
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

## 9. USB and BLE HID Precedence

When native USB HID is mounted and ready, Cardputer Hub must route host-control
HID exclusively over USB. Physical cable power alone is insufficient; the USB
HID transport must have completed enumeration.

While USB owns HID output:

* the selected BLE channel and its `HostProfile` remain selected;
* the same logical Action must never be emitted over both transports;
* transport handover must release or neutralize in-flight key state so a host
  cannot receive a stuck key;
* local UI navigation continues to work independently of host transport.

When USB HID disconnects, Cardputer Hub must automatically return HID output to
the selected BLE channel, reconnecting it when necessary. This fallback must
not require pairing again or silently select a different host. If the selected
BLE host is unavailable, the UI reports that state without sending the Action
elsewhere.

A wake-only input is consumed before transport selection and must not reach
either USB or BLE. Transport changes may update a small status indicator, but
must not navigate away from the current screen or display a blocking popup.

Delivery is split across the architectural phases: Phase 2 implements both HID
transports and their USB-first arbitration, Phase 3 supplies the active host,
Phase 6 exposes host selection in Device Manager, and Phase 7 maps logical
Actions to host-control HID reports. This section defines the end-to-end
interaction and does not move host selection into Connectivity.

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
* USB-over-BLE precedence, disconnect fallback, duplicate suppression, and
  in-flight key release;
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
* USB attachment and removal switch HID transport without duplicate or stuck
  keys.

## 11. Current-Firmware Boundary

This document is a requirement for future implementation. Until the relevant
phase delivers and validates each behavior, the current supported UI remains
the one described in `docs/manuals/`. Requirements here must be copied into the
user manual only when they become available in shipped firmware.
