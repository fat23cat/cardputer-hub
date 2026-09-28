# 038/2 — Codex Plus Reset Credits and Expanded Usage Details

Status: **Software implemented; physical acceptance pending**

Suggested branch:

```text
feat/038-2-codex-plus-reset-details
```

Suggested PR title:

```text
[038/2] Add Codex Plus reset-credit details
```

Target repository:

```text
fat23cat/cardputer-hub
```

Parent plan:

```text
038 — AI Usage Mini App, automatic provider discovery, and Puzzle quota gauge
```

---

# 0. Scope

Plan 038/2 is a narrow extension of the already implemented and physically verified AI Usage feature.

It extends **Codex Plus only**.

It does not redesign:

```text
Codex Business
Cursor Enterprise
provider auto-detection
host switching
Puzzle overview
Companion discovery
AI_USAGE app availability
```

The extension adds:

```text
1. remaining Codex reset-credit count on the normal Codex Plus dashboard;
2. an expanded Codex Plus detail mode;
3. a detailed reset-credit page.
```

The normal dashboard remains the primary view.

The expanded view is optional and opens only by explicit user input.

---

# 1. Current behavior to preserve

The implemented Plan 038 already provides:

```text
HOME MAC
    ↓
CODEX · PLUS
    ↓
5-hour limit
weekly limit
```

and:

```text
WORK MAC
    ↓
CODEX · BUSINESS
CURSOR · ENTERPRISE
```

Host switching already clears old session data correctly.

Puzzle already adapts automatically to the current provider/metric set.

All of this must remain unchanged.

Hard invariants:

```text
no Home/Work configuration
no manual account type
no provider switches
no stale provider leakage between Macs
no extra header on the main AI Usage dashboard
```

---

# 2. Goal

For a Codex Plus account, expose the reset-credit information already available from Codex without cluttering the normal monitor.

The user should be able to glance at the normal dashboard and immediately see:

```text
current 5-hour remaining
current weekly remaining
time until both resets
how many reset credits remain
```

Then, when desired, open a detailed mode and inspect:

```text
used vs remaining for both limits
reset timing for both limits
available reset-credit count
individual reset-credit entries
expiry information
```

---

# 3. Confirmed Codex data

The existing Codex app-server request remains:

```text
account/rateLimits/read
```

For Codex Plus the response already exposes:

```text
rateLimits.primary / secondary
rateLimitResetCredits
```

The existing Plan 038 normalization already identifies rolling windows by:

```text
windowDurationMins
```

and must continue doing so.

Relevant reset-credit information known from the response includes:

```text
availableCount
per-credit status
per-credit title
per-credit expiresAt
```

Other reset-credit fields may exist, but Plan 038/2 must not depend on fields that are not needed by the approved UI.

No additional OpenAI endpoint is required.

---

# 4. Non-goal: using a reset credit

Plan 038/2 is **read-only**.

It must not add:

```text
apply reset
consume reset credit
confirm reset
change Codex limits
open a purchase flow
```

Cardputer only displays reset-credit availability and details.

Any future action that consumes a reset credit requires a separate plan.

---

# 5. Main Codex Plus screen

The approved normal screen becomes:

```text
CODEX · PLUS                         R×2

5 HOUR                       63% LEFT
████████████████████████░░░░░░░░░░░░░░
RESET 2H 11M

WEEK                         81% LEFT
███████████████████████████████░░░░░░░
RESET 5D 18H
```

The only new element is:

```text
R×2
```

Meaning:

```text
2 reset credits available
```

Do not add:

```text
RESET CREDITS: 2
AVAILABLE RESETS: 2
```

as a new row on the main dashboard.

The reset count must consume only the unused space on the existing provider/plan row.

---

# 6. Main-screen reset badge semantics

For a known reset-credit count:

```text
R×0
R×1
R×2
R×3
...
```

The value is:

```text
rateLimitResetCredits.availableCount
```

Rules:

```text
known zero
→ show R×0

known positive count
→ show R×N

reset-credit data not present / unsupported / invalid
→ omit the badge entirely
```

Do not render:

```text
R×--
```

on the normal screen.

Absence of the badge means the reset-credit count is unavailable, not zero.

---

# 7. Main-screen geometry

Keep the existing Codex Plus layout.

Recommended provider-row geometry:

```text
left label:
x = 8
CODEX · PLUS

reset count:
right aligned to x = 232
R×N
```

Do not move the limit sections downward merely to fit the reset count.

The badge uses the compact bitmap font rather than becoming a large Micro 5 value.

Suggested semantic treatment:

```text
CODEX · PLUS → Ink
R×N          → Blue or Ink
```

Do not use Vermilion merely because the count is low.

Reset-credit count is informational, not an alert threshold.

---

# 8. Expanded mode

Codex Plus gains an internal expanded mode.

Normal flow:

```text
AI USAGE main dashboard
    ↓ Enter
Codex Plus expanded details
```

The expanded mode contains two pages:

```text
LIMITS
RESETS
```

Default page on entry:

```text
LIMITS
```

The normal main dashboard remains unchanged for all non-Plus provider combinations.

---

# 9. Expanded Page 1 — LIMITS

Approved conceptual layout:

```text
CODEX · PLUS

5 HOUR
USED                 37%
LEFT                 63%
RESET              2H 11M

WEEK
USED                 19%
LEFT                 81%
RESET              5D 18H

LIMITS   |   RESETS
```

This page is informational.

It expands the existing remaining-only presentation into explicit:

```text
USED
LEFT
RESET
```

for both rolling windows.

---

# 10. LIMITS page formatting

For each metric:

```text
USED = 100 - remainingPercent
LEFT = remainingPercent
```

If Plan 038 internally stores `usedPercent`, use the direct normalized value rather than re-deriving it.

Values must remain consistent with the main screen.

Example:

```text
main:
63% LEFT

expanded:
USED 37%
LEFT 63%
```

Never allow rounding to produce:

```text
USED 38%
LEFT 63%
```

unless the provider itself supplies independently rounded values with that result.

Prefer one normalized source of truth.

---

# 11. LIMITS page reset formatting

Use the same reset formatting rules as the main screen.

Examples:

```text
RESET 2H 11M
RESET 5D 18H
RESET --
```

Unknown reset must not display:

```text
RESET 0H 0M
```

The existing fixed behavior from Plan 038 remains authoritative.

---

# 12. Expanded Page 2 — RESETS

Approved conceptual layout:

```text
RESET CREDITS

AVAILABLE             2

#1  FULL RESET
EXP IN 21D

#2  FULL RESET
EXP IN 39D

LIMITS   |   RESETS
```

This page shows:

```text
available reset-credit count
individual available reset credits
their title/type
their expiry
```

It does not show raw API identifiers.

---

# 13. Which reset credits appear

The detailed page is about credits that can still be used.

Therefore the visible list should prefer:

```text
status = available / usable
```

Do not clutter the page with:

```text
expired credits
already-used credits
invalid entries
```

unless the real Codex API makes an otherwise-important distinction that cannot be represented by the available count.

If the provider response contains historical/non-usable entries, normalize them out of the Cardputer-visible list.

Preserve:

```text
availableCount
```

independently from the number of entries transmitted to Cardputer.

---

# 14. Reset-credit ordering

Visible reset credits are sorted by:

```text
earliest known expiry first
```

Credits without a known expiry go after credits with a known expiry.

Stable ordering is required so the list does not jump between refreshes.

If two credits have identical/unknown expiry, preserve provider order as the tiebreaker.

---

# 15. Reset-credit title

Use the provider-supplied title when it is:

```text
present
valid UTF-8
representable within the bounded protocol/UI after truncation
```

Normalize title on the Mac for the display:

```text
uppercase presentation
prefer FULL RESET when it identifies a longer full-reset title
otherwise shorten to 16 UTF-8 bytes at a code-point boundary if needed
```

Example:

```text
Full reset
→ FULL RESET
```

Do not transmit or display:

```text
raw credit ID
account ID
workspace ID
opaque provider metadata
```

If title is missing or cannot be represented safely:

```text
RESET CREDIT
```

is the fallback label.

---

# 16. Reset-credit expiry presentation

The compact reset list uses relative expiry.

Examples:

```text
EXP IN 21D
EXP IN 6D
EXP IN 18H
EXP IN 42M
EXP NOW
EXP --
```

Recommended formatting:

```text
>= 48 hours
→ whole days, floor or consistently rounded according to one helper

24..47 hours
→ 1D

1..23 hours
→ whole hours

1..59 minutes
→ whole minutes

1..59 seconds
→ 1M

zero remaining seconds with known expiry
→ EXP NOW

unknown
→ EXP --
```

Do not require a Cardputer timezone or wall-clock implementation solely for this view.

The Companion should normalize enough information for a relative countdown.

---

# 17. Reset-credit scrolling

The approved layout shows two credits at once.

Visible capacity:

```text
2 reset-credit entries per page
```

If more than two usable credits exist:

```text
Up / Down
→ scroll reset-credit list
```

Keep `AVAILABLE N` fixed.

Optional compact range indicator when scrolling is required:

```text
1-2/4
3-4/4
```

Place it right-aligned on the `AVAILABLE` line or title line only if it fits without harming readability.

Do not show a range indicator when:

```text
availableCount <= 2
```

---

# 18. Navigation

Current shell behavior remains authoritative:

```text
Escape
→ closes the Mini App through ApplicationShell
```

Plan 038/2 must not try to intercept shell-owned Escape as an internal Back command.

Approved local navigation:

## Main dashboard

```text
Enter
→ open expanded mode on LIMITS
```

Existing main-screen Up/Down behavior used for Puzzle metric focus remains unchanged.

## Expanded mode

```text
Left / Right
→ switch LIMITS ↔ RESETS

Enter
→ return to normal AI Usage main dashboard

Up / Down
→ on RESETS only, scroll when more than two credits exist
```

No persistent modal stack is needed.

Entering expanded mode always starts on:

```text
LIMITS
```

Leaving and reopening expanded mode may reset to `LIMITS`.

---

# 19. Expanded footer

Both expanded pages show:

```text
LIMITS   |   RESETS
```

The active page must be visually clear.

Recommended treatment:

```text
active page   → Ink or Blue
inactive page → Ordinal
```

A thin focus marker or underline may be used.

Do not introduce rounded tabs.

Do not add:

```text
ESC BACK
ENTER BACK
```

to the footer.

Input behavior may be documented in the device guide instead.

---

# 20. Expanded LIMITS geometry

Target:

```text
240 × 135
```

Delivered structure:

```text
provider row at y = 6

5-hour column at x = 8..112
week column at x = 128..232

each column:
window name, large USED percentage, remaining bar,
LEFT percentage, reset timing

footer separator
y ≈ 112

footer
y = 118..132
```

The physical arrow-marked `,` / `/` keys switch the pages without Fn.
The physical `;` / `.` keys scroll RESETS without Fn. Logical arrows work too.

Expanded mode prioritizes exact textual details.

---

# 21. Expanded RESETS geometry

Recommended structure:

```text
title
y = 5

available count
y = 22

credit #1
y = 42..68

credit #2
y = 74..100

footer separator
y ≈ 112

footer
y = 118..132
```

Each credit uses two compact rows:

```text
#1  FULL RESET
EXP IN 21D
```

Do not draw each credit inside a card.

Whitespace and typography provide separation.

---

# 22. Empty reset-credit state

Known zero:

```text
RESET CREDITS

AVAILABLE             0

NO RESETS AVAILABLE

LIMITS   |   RESETS
```

The main screen simultaneously shows:

```text
R×0
```

Do not treat zero credits as an error.

---

# 23. Reset-credit data unavailable

If the Companion/session supports expanded Plus details but reset-credit data is temporarily unavailable:

```text
RESET CREDITS

AVAILABLE            --

DETAILS UNAVAILABLE

LIMITS   |   RESETS
```

The main screen omits `R×N`.

Do not display stale reset-credit information from another host/session.

If previous reset data from the same session becomes stale because refresh fails, follow the same provider-freshness policy as Plan 038.

Do not silently present stale expiry data as current.

---

# 24. Non-Plus behavior

This extension applies only to:

```text
CODEX · PLUS
```

Do not add the reset badge to:

```text
CODEX · BUSINESS
CURSOR · ENTERPRISE
```

Do not add empty reset pages for them.

For a Work dashboard with:

```text
CODEX BUSINESS
CURSOR ENTERPRISE
```

Plan 038 behavior remains exactly as before.

---

# 25. Mixed/future provider safety

The implementation should key the feature from semantic provider data:

```text
provider == Codex
plan == Plus
```

Do not infer Plus based on:

```text
there are exactly two metrics
provider index == 0
home Mac
Cursor absent
```

This preserves zero-configuration behavior and avoids coupling UI to one machine.

---

# 26. Puzzle behavior

Plan 038/2 does not redesign Puzzle behavior.

Keep:

```text
main Codex Plus:
top half    → 5-hour
bottom half → weekly
```

Entering the expanded view does not change the background overview by itself.

Existing main-screen metric-focus behavior remains available.

The new reset-credit list does not receive a Puzzle visualization in this plan.

Do not represent reset-credit count as Puzzle pixels.

Do not add reset-credit warning animation.

Those can be considered separately later if they prove useful.

---

# 27. Companion model extension

Extend the normalized Codex Plus provider snapshot with optional reset-credit details.

Conceptually:

```swift
struct AiResetCredit {
    var title: String
    var expiresAt: UInt32?
    var expiresRemainingSeconds: UInt32?
}

struct AiResetCredits {
    var availableCount: UInt8
    var credits: [AiResetCredit]
}
```

Then:

```swift
AiUsageProviderSnapshot
    ...
    resetCredits: AiResetCredits?
```

Exact types may vary.

Hard requirements:

```text
resetCredits exists only when data is known
availableCount may be zero
credits array is bounded
```

---

# 28. Bounded reset-credit count

The wire protocol must remain bounded.

Recommended maximum transmitted detailed entries:

```text
4
```

This can be smaller than:

```text
availableCount
```

Example:

```text
availableCount = 7
detailed entries transmitted = first 4 by expiry
```

The Cardputer may show:

```text
AVAILABLE 7
```

while only allowing detail inspection for the bounded transmitted subset.

`detailedCount` must never exceed `availableCount`; both v4 codecs reject a
payload that violates this invariant.

If physical testing suggests that four is unnecessarily restrictive and payload budget comfortably allows more, the implementation may use another explicit small bound.

Do not transmit an unbounded provider array.

---

# 29. Protocol compatibility

Plan 038 currently uses Companion protocol v3 for `AI_USAGE`.

Do not silently change the existing v3 AI Usage payload.

Plan 038/2 introduces:

```text
Companion Protocol v4
```

Supported versions become:

```text
v4
v3
v2
v1
```

Reason:

> existing v3 peers must continue decoding the original AI Usage payload exactly as implemented.

---

# 30. Protocol compatibility matrix

```text
new firmware + new Companion
→ v4
→ AI Usage + Codex Plus reset credits

new firmware + v3 Companion
→ v3
→ existing AI Usage still works
→ reset badge/details unavailable

new firmware + v2 Companion
→ existing pre-AI Companion behavior

old v3 firmware + new Companion
→ negotiates v3
→ existing AI Usage still works
→ Companion emits original v3 payload

old v2 firmware + new Companion
→ negotiates v2
→ existing behavior remains
```

No GATT UUID changes.

No framing changes.

No operation-ID changes are required.

---

# 31. AI_USAGE payload versioning

Keep the same:

```text
AI_USAGE operation
```

Protocol v4 adds a new AI Usage payload schema that includes optional reset-credit data.

Conceptually:

```text
v3 selected
→ encode AI Usage payload schema 1

v4 selected
→ encode AI Usage payload schema 2
```

New firmware must decode:

```text
schema 1
schema 2
```

Old firmware never receives schema 2 because it cannot negotiate v4.

---

# 32. Reset-credit wire fields

Each detailed reset-credit entry needs only what the approved UI uses.

Recommended wire information:

```text
title length
bounded title bytes
expiry remaining seconds
expiry-known flag
```

At provider level:

```text
reset-credit-data-known flag
availableCount
detailedCount
```

Do not transmit:

```text
raw credit ID
description
account identifier
grant source
opaque JSON
```

unless later UI requirements explicitly need them.

---

# 33. Bounded title size

Reset-credit titles are provider data and must be bounded.

Recommended maximum:

```text
24 UTF-8 bytes
```

or another small explicit limit proven to fit the protocol payload.

Requirements:

```text
valid UTF-8
safe truncation on code-point boundary
no buffer overflow
deterministic fallback title
```

Firmware rendering still performs visual truncation based on pixel width.

---

# 34. Codex normalization

Extend only Codex Plus normalization.

Existing Plus detection remains:

```text
planType = plus
rolling windows identified by windowDurationMins
```

Then optionally inspect:

```text
rateLimitResetCredits
```

Normalize:

```text
availableCount
usable reset-credit entries
title
expiresAt / remaining expiry
```

Business normalization remains unchanged.

Cursor normalization remains unchanged.

---

# 35. Reset-credit validation

Treat malformed reset-credit metadata as local optional-data failure.

Example:

```text
Plus rate limits valid
reset-credit structure malformed
```

must produce:

```text
valid Codex Plus provider
valid 5H/WEEK metrics
resetCredits = unavailable
```

Do not drop the entire Codex provider merely because reset-credit details cannot be parsed.

This is a required resilience invariant.

---

# 36. Companion refresh behavior

No new polling loop is required.

Reuse the existing Plan 038 Codex refresh cadence.

Each normal Codex refresh updates:

```text
rolling limits
reset-credit metadata
```

atomically as one provider snapshot where possible.

If limits succeed but reset-credit optional data is malformed:

```text
retain valid limits
mark reset-credit details unavailable
```

Do not start a second high-frequency reset-credit poll.

---

# 37. Firmware model extension

Extend the normalized firmware snapshot with optional reset-credit data.

Conceptually:

```cpp
struct AiResetCredit {
    ...
};

struct AiResetCredits {
    bool available = false;
    std::uint8_t availableCount = 0;
    ...
};

struct AiUsageProviderSnapshot {
    ...
    AiResetCredits resetCredits;
};
```

The exact representation may use fixed arrays rather than dynamic allocation.

Prefer bounded fixed storage appropriate for firmware.

---

# 38. Session isolation

Reset-credit state obeys the same hard host/session boundary as Plan 038.

On Companion session change:

```text
old provider snapshot cleared
old reset-credit count cleared
old reset-credit list cleared
expanded view state cleared
reset-list scroll offset cleared
```

Required regression:

```text
Home Mac with R×2
→ switch to Work Mac

R×2 must disappear immediately.
```

And:

```text
Work Mac
→ switch back to Home Mac with different count

new count must come only from the new session.
```

---

# 39. App state

Extend `AiUsageApp` with a small local presentation state.

Conceptually:

```text
view:
    Main
    ExpandedLimits
    ExpandedResets

resetScrollOffset
```

This is presentation state only.

Do not move protocol or provider logic into `AiUsageApp`.

On activation:

```text
view = Main
scroll = 0
```

On deactivation:

```text
view = Main
scroll = 0
```

On session/provider shape change that invalidates Codex Plus detail mode:

```text
return to Main
```

---

# 40. Enter behavior

`Enter` semantics:

```text
Main + Codex Plus
→ ExpandedLimits

ExpandedLimits
→ Main

ExpandedResets
→ Main
```

If the normal screen does not contain a Codex Plus provider eligible for expanded details:

```text
Enter
→ no new Plan 038/2 action
```

Do not unexpectedly open reset details for Business/Cursor.

---

# 41. Left / Right behavior

Inside expanded mode only:

```text
Left / Right
→ switch LIMITS ↔ RESETS
```

Use the existing named-key and physical left/right input conventions already used by other Mini Apps.

Switching pages:

```text
LIMITS → RESETS
```

resets or clamps the reset-credit scroll offset.

No page-transition animation is required unless existing Mini App transition infrastructure makes it essentially free and consistent.

A direct redraw is acceptable.

---

# 42. Up / Down behavior

Main view:

```text
preserve existing Plan 038 metric/Puzzle focus behavior
```

Expanded LIMITS:

```text
no action
```

Expanded RESETS:

```text
scroll reset-credit entries when detailedCount > 2
```

Do not let reset-list scrolling alter Puzzle metric focus.

---

# 43. Footer active state

Examples:

LIMITS active:

```text
LIMITS   |   RESETS
^^^^^^
```

RESETS active:

```text
LIMITS   |   RESETS
           ^^^^^^
```

The actual implementation may use:

```text
Blue active text
Ordinal inactive text
```

instead of an underline.

Keep footer geometry stable between pages.

---

# 44. Main-screen render invalidation

Adding reset count must not cause continuous redraw.

Redraw provider-row reset badge only when:

```text
reset-credit availability becomes known/unknown
availableCount changes
provider/plan changes
session changes
```

A normal provider refresh with the same count must not repaint solely because it was refreshed.

Preserve the Plan 038 settled-screen invariant.

---

# 45. Expanded render invalidation

Expanded LIMITS redraws when presentation values change:

```text
5H used/left
5H reset display
week used/left
week reset display
page/view state
```

Expanded RESETS redraws when:

```text
availableCount changes
credit list changes
expiry display changes
scroll offset changes
page/view state
```

Countdown text may update when its displayed unit changes.

Do not repaint continuously at the firmware loop rate.

---

# 46. Main-screen tests

Cover:

```text
Codex Plus with 2 credits
→ R×2

Codex Plus with 0 credits
→ R×0

Codex Plus with unknown reset-credit data
→ no badge

Codex Business
→ no reset badge

Cursor
→ no reset badge

same count after provider refresh
→ no unnecessary provider-row redraw
```

Also verify the badge is right-aligned to the approved edge.

---

# 47. Expanded LIMITS tests

Cover:

```text
Enter from Plus main opens LIMITS

5H:
USED
LEFT
RESET

WEEK:
USED
LEFT
RESET

values match main dashboard
unknown reset renders RESET --

no progress bars required
footer visible
LIMITS marked active
```

---

# 48. Expanded RESETS tests

Cover:

```text
Left/Right switches to RESETS
header says RESET CREDITS
AVAILABLE count shown

0 available
1 available
2 available
>2 available

title fallback
known expiry
unknown expiry

credits sorted by expiry
only usable credits shown
```

---

# 49. Reset scrolling tests

With more than two detailed credits:

```text
initial:
credits 1-2

Down:
credits 2-3 or next bounded scroll position according to chosen list behavior

further Down:
clamped at end

Up:
moves toward beginning

page change away/back:
scroll reset/clamped according to implementation contract
```

Use one deterministic scrolling model and test it.

---

# 50. Navigation tests

Cover:

```text
Main + Enter
→ ExpandedLimits

ExpandedLimits + Right
→ ExpandedResets

ExpandedResets + Left
→ ExpandedLimits

Expanded page + Enter
→ Main

Escape
→ still owned by ApplicationShell
→ closes AI USAGE

Business/Cursor-only dashboard + Enter
→ no Codex Plus detail mode
```

---

# 51. Host-switch regression tests

Required:

```text
Home:
Codex Plus
R×2
expanded reset details visible

switch to Work:
Codex Business + Cursor

expected:
expanded mode exits
R×2 disappears
old reset list unavailable
no Home reset data remains
```

Then reverse:

```text
Work → Home

new Plus snapshot has R×1

expected:
R×1
not R×2
```

---

# 52. Protocol tests

Add coverage for:

```text
v4 negotiation
v3 fallback
v2/v1 fallback unchanged

old v3 AI Usage fixture remains byte-identical

v4 AI Usage schema with:
    reset data absent
    zero credits
    one credit
    max detailed credits

invalid detailed count
title too long / truncation
invalid UTF-8 title
truncated reset-credit payload
unexpected trailing payload
unknown expiry
known expiry
```

Keep matching Swift/C++ fixtures where useful.

---

# 53. Companion normalization tests

Use a realistic Codex Plus response fixture containing:

```text
5-hour rolling limit
weekly rolling limit
rateLimitResetCredits.availableCount
multiple reset credits
title
status
expiresAt
```

Verify:

```text
rolling limits still normalize exactly as before
available count preserved
only usable credits become details
expiry ordering stable
malformed optional reset section does not drop Plus metrics
```

Do not require a real OpenAI account in automated tests.

---

# 54. Companion session tests

For negotiated protocol:

```text
v3
→ original AI Usage payload
→ no reset-credit extension

v4
→ extended AI Usage payload
```

This backward compatibility is required.

---

# 55. Firmware service tests

Cover:

```text
v3 snapshot
→ normal AI Usage works
→ reset details absent

v4 Plus snapshot
→ reset details accepted

session switch
→ reset details cleared immediately

same semantic snapshot
→ no unnecessary revision

count changes
→ revision increments

credit expiry presentation changes
→ appropriate revision/render update
```

---

# 56. Physical acceptance — main screen

On Home Mac with Codex Plus and known reset credits:

```text
CODEX · PLUS                         R×N
```

must be visible without disturbing:

```text
5 HOUR
WEEK
bars
reset countdowns
```

Check at:

```text
10% display brightness
normal configured brightness
```

Count must remain readable.

---

# 57. Physical acceptance — expanded LIMITS

Press Enter.

Expected:

```text
CODEX · PLUS

5 HOUR
USED ...
LEFT ...
RESET ...

WEEK
USED ...
LEFT ...
RESET ...

LIMITS | RESETS
```

Verify:

```text
no clipping
right-aligned values
footer readable
no accidental app exit
```

---

# 58. Physical acceptance — expanded RESETS

Press Right.

Expected:

```text
RESET CREDITS

AVAILABLE N

#1 ...
EXP ...

#2 ...
EXP ...

LIMITS | RESETS
```

If more than two entries are available:

```text
Up / Down
```

must scroll reliably.

---

# 59. Physical acceptance — host switching

While expanded reset details are visible:

```text
switch to another saved Mac
```

Expected:

```text
old details disappear immediately
app returns to a valid main-state presentation
no reset-credit data leaks to the new session
```

Switch back.

Expected:

```text
new current Plus reset-credit count appears
```

---

# 60. Documentation

Update:

```text
docs/ARCHITECTURE.md
docs/UI_REQUIREMENTS.md
docs/manuals/device-guide.md
docs/plans/README.md
protocol/companion/README.md
companion/macos/README.md
```

Document:

```text
protocol v4
AI Usage schema extension
Codex Plus R×N meaning
expanded LIMITS / RESETS pages
navigation
reset-credit data is read-only
```

Do not document reset-credit consumption because it is not implemented.

---

# 61. Non-goals

Plan 038/2 does not add:

```text
reset-credit consumption
reset confirmation
reset notifications
Puzzle reset-credit visualization
Puzzle reset-credit warnings

Codex Business detail mode
Cursor detail mode

usage history
1h / 24h deltas
pace
forecasting
session spend

new AI providers
new settings
manual account configuration
```

Keep the scope strictly Codex Plus reset-credit visibility and expanded details.

---

# 62. Recommended implementation order

```text
01. Add Plan 038/2 document

02. Introduce Companion protocol v4 negotiation
03. Preserve v3/v2/v1 compatibility
04. Define AI Usage payload schema 2
05. Add reset-credit bounded wire model
06. Add Swift codec
07. Add C++ codec
08. Add protocol fixtures/tests

09. Extend Codex Plus normalization
10. Parse availableCount
11. Parse usable reset-credit entries
12. Normalize title
13. Normalize expiry
14. Sort by expiry
15. Make reset section optional/failure-isolated
16. Add Companion normalization tests

17. Extend firmware AiUsageSnapshot
18. Decode reset-credit data
19. Preserve v3 snapshot compatibility
20. Clear reset data on session change
21. Add service tests

22. Add R×N to Codex Plus main provider row
23. Add known-zero behavior
24. Add unknown-data badge omission
25. Add main UI tests

26. Add AiUsageApp internal view state
27. Add Enter Main → ExpandedLimits
28. Add Expanded LIMITS screen
29. Add Left/Right page switching
30. Add Expanded RESETS screen
31. Add reset list scrolling
32. Add Enter Expanded → Main
33. Preserve shell-owned Escape
34. Preserve existing main Puzzle focus behavior
35. Add navigation/UI tests

36. Update documentation

37. Run companion-check
38. Run host-check
39. Run firmware-check
40. Run firmware-size
41. Run CI

42. Flash physical Cardputer
43. Validate main R×N
44. Validate LIMITS page
45. Validate RESETS page
46. Validate >2 credit scrolling if test data can be injected
47. Validate Home → Work switch while expanded
48. Validate Work → Home switch
```

---

# 63. Validation commands

At minimum:

```bash
make companion-check
make host-check
make firmware-check
make firmware-size
```

Run the full CI-equivalent suite as well.

Do not mark physical acceptance complete from host tests alone.

---

# 64. Completion criteria

Plan 038/2 is complete when:

```text
[ ] Companion protocol v4 is implemented
[ ] v3/v2/v1 compatibility remains intact
[ ] original v3 AI Usage payload remains compatible

[ ] Codex Plus reset-credit count is normalized
[ ] usable reset-credit entries are normalized
[ ] reset-credit title is bounded
[ ] reset-credit expiry is normalized
[ ] malformed reset details do not break Plus limit monitoring

[ ] firmware receives optional reset-credit details
[ ] reset-credit state clears on session change

[ ] main Codex Plus screen shows R×N
[ ] known zero shows R×0
[ ] unknown count omits the badge
[ ] Business/Cursor screens are unchanged

[ ] Enter opens expanded LIMITS for Codex Plus
[ ] LIMITS shows USED / LEFT / RESET for 5H
[ ] LIMITS shows USED / LEFT / RESET for WEEK

[ ] RESETS shows AVAILABLE count
[ ] RESETS shows two detailed entries at once
[ ] RESETS supports bounded scrolling for more entries
[ ] missing title has a safe fallback
[ ] missing expiry shows EXP --

[ ] Left/Right switches expanded pages
[ ] Enter returns expanded mode to Main
[ ] Escape remains shell-owned and closes the app

[ ] existing main Puzzle behavior remains unchanged
[ ] no reset-credit Puzzle visualization is added

[ ] Home → Work clears reset-credit UI
[ ] Work → Home loads only the new session's reset data
[ ] no stale reset-credit data leaks between hosts

[ ] protocol tests pass
[ ] Companion tests pass
[ ] AI Usage service tests pass
[ ] AI Usage UI tests pass
[ ] host-check passes
[ ] companion-check passes
[ ] firmware-check passes
[ ] firmware-size passes
[ ] CI passes

[ ] physical main-screen acceptance passes
[ ] physical LIMITS-page acceptance passes
[ ] physical RESETS-page acceptance passes
[ ] physical host-switch acceptance passes
```

---

# 65. End state

Normal Codex Plus dashboard:

```text
CODEX · PLUS                         R×2

5 HOUR                       63% LEFT
████████████████████████░░░░░░░░░░░░░░
RESET 2H 11M

WEEK                         81% LEFT
███████████████████████████████░░░░░░░
RESET 5D 18H
```

Press Enter:

```text
CODEX · PLUS

5 HOUR
USED                 37%
LEFT                 63%
RESET              2H 11M

WEEK
USED                 19%
LEFT                 81%
RESET              5D 18H

LIMITS   |   RESETS
```

Press Right:

```text
RESET CREDITS

AVAILABLE             2

#1  FULL RESET
EXP IN 21D

#2  FULL RESET
EXP IN 39D

LIMITS   |   RESETS
```

The normal dashboard stays compact.

The extra data exists only when the user asks for it.

Codex Plus remains automatically detected.

No new configuration is introduced.

Business and Cursor behavior remain unchanged.
