# 036/2 — Home Actions and Connected Device Context

## Current status

**Software implemented — host checks and ESP-IDF firmware build passed; physical Cardputer-Adv visual acceptance pending.**

This addendum extends Plan 036 after physical validation on Cardputer-Adv.

The device-centric ambient Home from Plan 036 is still the base design, but hardware testing showed that a status bar plus the Living Orb alone feels too empty and does not provide enough interaction affordance.

This addendum keeps the existing visual language and ambient concept while adding:

- explicit `APPS` and `SETTINGS` actions at the bottom of Home;
- a real focus state for those actions;
- left/right Home navigation;
- a connected Bluetooth device row;
- removal of the Companion diamond from the top status bar.

This document supersedes the relevant Home-layout and Companion-indicator parts of Plan 036 where they conflict.

---

# 1. Updated Home concept

Home remains:

- device-centric;
- application-neutral;
- separate from Launcher;
- visually calm;
- based on the existing Bone / Ink / Ordinal flat UI language.

It is **not** becoming a dashboard and does not gain app-specific widgets.

The updated Home consists of four layers:

```text
global status bar
ambient Living Orb
connected-device context row
bottom system actions
```

Target layout:

```text
┌────────────────────────────────────────┐
│ ● WiFi   ● BT                     82% │
├────────────────────────────────────────┤
│                                        │
│             ·       ·                  │
│         ·               ·              │
│       ·      ·     ·      ·            │
│         ·               ·              │
│             ·       ·                  │
│                                        │
│ ● Work MacBook                         │
├───────────────────┬────────────────────┤
│ [      APPS      ]│      SETTINGS      │
└───────────────────┴────────────────────┘
```

The wireframe is conceptual; exact pixel geometry may be tuned on hardware.

---

# 2. Top status bar

The top status bar now contains only:

```text
WiFi
BT
Battery
```

Target:

```text
● WiFi   ● BT                     82%
```

Keep the existing semantic status-dot behavior from Plan 036.

The positions of:

- WiFi;
- BT;
- battery right edge

must remain stable.

---

# 3. Remove the Companion diamond from the status bar

Remove the dedicated:

```text
◆
```

Companion indicator from Home.

The top bar must no longer reserve or render a Companion slot.

Do not replace it with:

```text
COMPANION
CONNECTED
MAC
```

or another Companion-specific label.

Companion availability may continue to control Mini App availability elsewhere, but Home no longer exposes it as a separate status item.

---

# 4. Connected Bluetooth device row

Add a single compact context row above the bottom action bar.

When Bluetooth has a fully connected / `Ready` active host and a live Companion
session, show:

```text
● <device name>
```

Example:

```text
● Work MacBook
```

or:

```text
● Pavel's MacBook Pro
```

The status icon is a **round Leaf dot**, visually consistent with the WiFi and BT dots.

Do not use:

- a diamond;
- a laptop icon;
- a Bluetooth glyph;
- the word `Companion`;
- the word `Connected`.

The device name itself communicates what is connected.

---

# 5. Connected-device source of truth

Use the existing `HostService` connection state and active host name, together
with the live `COMPANION` capability.

The row is visible only when:

```text
HostConnectionStatus::Ready
```

and an active host name is available.
The `COMPANION` capability must also be available.

Expected state mapping:

```text
Off
→ hidden

Connecting
→ hidden

Securing
→ hidden

Pairing
→ hidden

Error
→ hidden

Ready with live COMPANION
→ ● <active host name>

Ready without live COMPANION
→ hidden
```

The BT status dot continues to reflect the Bluetooth host connection alone.
A Bluetooth HID connection may remain Ready after Companion closes; in that
case the device row is hidden while the BT dot stays Leaf.

---

# 6. Long device names

The connected-device row has a fixed available width.

If the active host name does not fit, truncate it visually with an ellipsis.

Example:

```text
● Pavel's MacBook Pro...
```

Do not modify the stored `HostProfile` name.

Do not horizontally scroll the text.

Use the existing font and normal Home text treatment; do not restore the removed large Micro 5 Home host-name asset.

---

# 7. Reserve the connected-device row

The vertical space for the connected-device row is always reserved.

Disconnected state:

```text
│                                        │
├───────────────────┬────────────────────┤
│ [      APPS      ]│      SETTINGS      │
└───────────────────┴────────────────────┘
```

Connected state:

```text
│ ● Work MacBook                         │
├───────────────────┬────────────────────┤
│ [      APPS      ]│      SETTINGS      │
└───────────────────┴────────────────────┘
```

The Living Orb and bottom action bar must not move when Bluetooth connects or disconnects.

Only the row contents change.

---

# 8. Bottom action bar

Add a persistent bottom Home action bar with two equal actions:

```text
APPS
SETTINGS
```

Conceptually:

```text
├───────────────────┬────────────────────┤
│       APPS        │      SETTINGS      │
└───────────────────┴────────────────────┘
```

These are real interactive Home actions, not decorative navigation hints.

Use the existing flat system UI style:

- Bone background;
- Ink text;
- one-pixel structural lines;
- no rounded card buttons;
- no shadows;
- no gradients;
- no large decorative icons.

---

# 9. Focus state

Exactly one bottom action is focused while Home is active.

Default:

```text
APPS
```

Conceptually:

```text
├───────────────────┬────────────────────┤
│ [      APPS      ]│      SETTINGS      │
└───────────────────┴────────────────────┘
```

After moving right:

```text
├───────────────────┬────────────────────┤
│       APPS        │ [    SETTINGS    ] │
└───────────────────┴────────────────────┘
```

The brackets above are only wireframe notation.

Production UI should reuse the existing Launcher/system **focus plate visual language** rather than drawing literal brackets.

Where practical, reuse the existing spring-based focus motion instead of introducing a separate Home focus-animation style.

---

# 10. Home focus navigation

Home controls become:

```text
Left
→ focus APPS

Right
→ focus SETTINGS

Enter
→ activate focused action

Tab
→ open Settings directly

Escape
→ no action on root Home
```

With only two actions, repeated Left/Right may either:

- clamp at the edge; or
- switch between the two entries.

Prefer the behavior that best matches existing shell navigation conventions.

Do not use Up/Down for the bottom action bar.

Reserve Up/Down for possible future Home content interaction.

---

# 11. Default focus and return behavior

Whenever Home becomes the active root page:

```text
focused action = APPS
```

This includes:

- initial entry after startup;
- returning from Launcher;
- returning from Settings;
- returning from other system pages;
- returning after a Mini App exits to Home through the normal navigation flow.

Do not persist `SETTINGS` focus between visits.

This preserves the existing muscle memory:

```text
Home + Enter
→ Apps
```

---

# 12. Existing direct shortcuts remain valid

The new focus system must preserve:

```text
Enter on default Home
→ Launcher
```

and:

```text
Tab
→ Settings
```

`Tab` does not need to move Home focus before navigation.

It remains a direct Settings shortcut.

---

# 13. Living Orb remains

Keep the Living Orb from Plan 036.

It remains the main ambient visual layer.

Do not redesign it into:

- a widget;
- a dashboard;
- a clock;
- weather;
- Mini App status;
- system metrics.

However, the Orb may be:

- slightly reduced in vertical extent;
- shifted upward;
- visually re-centered

to accommodate the connected-device row and bottom action bar.

Its motion model remains unchanged unless small geometry tuning is required for the new viewport.

---

# 14. Updated layout regions

Recommended conceptual layout:

```text
y = 0..20
status bar

y = 21
separator

y = 22..~95
ambient Living Orb

y = ~96..~111
connected-device row, always reserved

y = ~112
separator

y = ~113..134
APPS / SETTINGS action bar
```

Exact pixel values may be adjusted after physical testing.

Hard requirements:

- status bar stays fixed;
- action bar stays fixed;
- connected-device row stays reserved;
- Orb never overlaps either region;
- no element jumps when connection state changes.

---

# 15. Focus plate behavior

The Home action focus should feel like part of the same UI system as Launcher focus.

Recommended behavior:

- short spring motion when changing `APPS ↔ SETTINGS`;
- no continuous animation when settled;
- no focus animation while display is Off;
- changing focus counts as normal user input and therefore follows the existing display-power activity contract.

Do not introduce:

- pulsing buttons;
- glowing borders;
- bouncing text;
- independent perpetual button animations.

---

# 16. Interaction during display wake

Preserve the existing wake-only input contract.

If the display is Dimmed or Off:

```text
first key
→ wake only
```

The wake key must not:

- change Home focus;
- open Apps;
- open Settings.

The next normal key is handled by Home.

---

# 17. Incremental rendering

Keep the current dirty-region approach.

Expected redraw behavior:

```text
WiFi change
→ WiFi status region only

BT state change
→ BT status region only

battery change
→ battery region only

connected-device name visibility/name change
→ connected-device row only

APPS/SETTINGS focus change
→ bottom action bar only

ambient frame
→ ambient viewport only
```

Changing bottom focus must not redraw the Living Orb.

Changing the connected-device row must not redraw the Living Orb.

An ambient frame must not redraw the action bar.

---

# 18. Updated Home presentation state

Home presentation state now needs to represent:

```text
WiFi semantic state
BT semantic state
battery percentage
connected-device row content
bottom action focus
ambient phase / accumulator
```

Conceptually:

```cpp
enum class HomeAction : std::uint8_t {
    Apps,
    Settings,
};
```

The connected-device row may be represented by an optional display string
derived from `HostService`, gated by live `COMPANION` availability.

Do not store a duplicate connection policy inside Home.

---

# 19. Updated wireframes

## Bluetooth disconnected

```text
┌────────────────────────────────────────┐
│ ● WiFi   ○ BT                     82% │
├────────────────────────────────────────┤
│                                        │
│             ·       ·                  │
│         ·               ·              │
│       ·      ·     ·      ·            │
│         ·               ·              │
│             ·       ·                  │
│                                        │
│                                        │
├───────────────────┬────────────────────┤
│ [      APPS      ]│      SETTINGS      │
└───────────────────┴────────────────────┘
```

## Bluetooth connected

```text
┌────────────────────────────────────────┐
│ ● WiFi   ● BT                     82% │
├────────────────────────────────────────┤
│                                        │
│             ·       ·                  │
│         ·               ·              │
│       ·      ·     ·      ·            │
│         ·               ·              │
│             ·       ·                  │
│                                        │
│ ● Work MacBook                         │
├───────────────────┬────────────────────┤
│ [      APPS      ]│      SETTINGS      │
└───────────────────┴────────────────────┘
```

## Settings focused

```text
┌────────────────────────────────────────┐
│ ● WiFi   ● BT                     82% │
├────────────────────────────────────────┤
│                                        │
│             ·       ·                  │
│         ·               ·              │
│       ·      ·     ·      ·            │
│         ·               ·              │
│             ·       ·                  │
│                                        │
│ ● Work MacBook                         │
├───────────────────┬────────────────────┤
│       APPS        │ [    SETTINGS    ] │
└───────────────────┴────────────────────┘
```

---

# 20. BDD — default Home focus

```gherkin
Scenario: Home starts with Apps focused
  Given Home becomes the active root page
  Then the APPS action is focused
  And the SETTINGS action is not focused
```

---

# 21. BDD — switch Home focus

```gherkin
Scenario: User focuses Settings
  Given Home is visible
  And APPS is focused
  When the user presses Right
  Then SETTINGS becomes focused
  And APPS becomes unfocused
  And the ambient viewport is not redrawn
```

```gherkin
Scenario: User returns focus to Apps
  Given Home is visible
  And SETTINGS is focused
  When the user presses Left
  Then APPS becomes focused
  And SETTINGS becomes unfocused
```

---

# 22. BDD — activate focused action

```gherkin
Scenario: Enter opens Apps from default Home
  Given Home is visible
  And APPS is focused
  When the user presses Enter
  Then Launcher opens
```

```gherkin
Scenario: Enter opens Settings when Settings is focused
  Given Home is visible
  And SETTINGS is focused
  When the user presses Enter
  Then Settings opens
```

---

# 23. BDD — Tab shortcut

```gherkin
Scenario: Tab opens Settings regardless of Home focus
  Given Home is visible
  And APPS is focused
  When the user presses Tab
  Then Settings opens
```

---

# 24. BDD — focus resets on Home re-entry

```gherkin
Scenario: Returning Home restores Apps focus
  Given SETTINGS was focused on Home
  And the user opened Settings
  When the user returns to Home
  Then APPS is focused
```

---

# 25. BDD — connected device row

```gherkin
Scenario: Connected Bluetooth device name appears
  Given Home is visible
  And HostService reports Ready
  And the COMPANION capability is available
  And the active host name is "Work MacBook"
  Then Home shows a Leaf round status dot
  And Home shows "Work MacBook"
  And Home does not show "Companion"
  And Home does not show "Connected"
```

```gherkin
Scenario: Connecting state does not expose the device row
  Given Home is visible
  And HostService reports Connecting
  Then the connected-device row is empty
```

```gherkin
Scenario: Companion availability alone does not expose the row
  Given Home is visible
  And the COMPANION capability is available
  But HostService is not Ready
  Then the connected-device row is empty
```

```gherkin
Scenario: Closing Companion hides the row while Bluetooth remains ready
  Given Home shows "Work MacBook"
  And HostService reports Ready
  When the COMPANION capability becomes unavailable
  Then the device name and row Leaf dot disappear
  And the BT status dot remains Leaf
  And the connected-device row remains reserved
  And the Living Orb and bottom action bar do not move
```

---

# 26. BDD — disconnect does not move the layout

```gherkin
Scenario: Bluetooth disconnects while Home is visible
  Given Home shows "Work MacBook"
  When HostService leaves Ready
  Then the device name and Leaf dot disappear
  And the connected-device row remains reserved
  And the Living Orb does not move
  And the bottom action bar does not move
```

---

# 27. BDD — long host name

```gherkin
Scenario: Connected host name is wider than the available row
  Given HostService reports Ready
  And the COMPANION capability is available
  And the active host name does not fit in the device row
  Then Home renders a truncated display label with an ellipsis
  And the stored HostProfile name is unchanged
```

---

# 28. BDD — wake-only key

```gherkin
Scenario: Wake input does not change Home focus
  Given Home is visible
  And APPS is focused
  And the display is Off
  When the user presses Right
  Then the display wakes
  And APPS remains focused
  And SETTINGS does not open
```

---

# 29. Tests

Add or update host-side coverage for:

```text
APPS focused on initial Home
Left/Right changes focus
Enter activates focused action
Tab remains direct Settings shortcut
Home re-entry resets focus to APPS
wake-only key does not change focus

Ready host with live Companion shows round Leaf dot + host name
non-Ready host hides row
Companion capability alone does not show row
Ready host without Companion capability hides row while BT status stays Ready
Companion capability loss clears only device row
long host names truncate without modifying stored profile
disconnect clears only device row

focus update redraws only action bar
device row update redraws only device row
ambient frame does not redraw action bar
bottom focus does not redraw ambient viewport
```

Preserve all Plan 036 ambient timing and display-power tests.

---

# 30. Documentation updates

Update the relevant Plan 036/Home documentation to reflect this addendum.

At minimum review:

```text
README.md
docs/ARCHITECTURE.md
docs/UI_REQUIREMENTS.md
docs/manuals/device-guide.md
docs/plans/README.md
```

The final documentation must not claim that Home still contains a Companion diamond.

Document:

```text
Left / Right
→ select APPS / SETTINGS

Enter
→ activate selected action

Tab
→ Settings shortcut
```

---

# 31. Non-goals

This addendum does not add:

- Weather to Home;
- Pomodoro status to Home;
- VPS status to Home;
- notification cards;
- configurable Home widgets;
- a Home clock;
- app shortcuts beyond `APPS`;
- a third bottom action;
- Companion-specific status copy;
- Companion connection diagnostics;
- active Mini App information;
- LED Puzzle integration.

Home remains intentionally small and general-purpose.

---

# 32. Acceptance criteria

This addendum is complete when:

- Home has a persistent bottom `APPS / SETTINGS` action bar.
- `APPS` is focused by default.
- Left/Right moves focus between the two actions.
- Enter opens the focused action.
- Tab still opens Settings directly.
- Returning to Home restores `APPS` focus.
- The existing wake-only input behavior is preserved.
- The top bar contains WiFi, BT, and battery only.
- The Companion diamond is removed from Home.
- A round Leaf dot plus active Bluetooth host name appears only while `HostService` is `Ready` and `COMPANION` is live.
- Losing Companion availability hides the device row without changing the BT status dot.
- The device row is vertically reserved even while empty.
- Long device names truncate visually without modifying stored host names.
- The Living Orb remains the ambient center content.
- Orb geometry is adjusted only as needed to fit the new fixed regions.
- focus changes redraw only the bottom action region.
- connection-row changes redraw only the connected-device region.
- ambient frames do not redraw the action bar.
- the updated Home remains visually consistent with the existing Cardputer Hub system UI.
- physical Cardputer-Adv validation is performed after implementation.
