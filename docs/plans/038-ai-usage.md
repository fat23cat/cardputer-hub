# 038 — AI Usage Mini App, automatic provider discovery, and Puzzle quota gauge

Status: **Software implemented; physical acceptance pending**

Suggested branch:

```text
feat/038-ai-usage
```

Suggested PR title:

```text
[038] Add AI usage monitor and Puzzle quota gauge
```

Target repository:

```text
fat23cat/cardputer-hub
```

---

# 0. Current state

Implementation update (2026-09-27): protocol v3, background provider collection,
firmware service, Mini App, Puzzle gauge, shared fixtures and host-side tests
are implemented. Independent review fixes cover real Business numeric strings,
Cursor's derived session cookie, bounded Codex discovery and process recovery,
and stable firmware UI revisions. Provider transports and credentials now have
fake-backed checks. Live Codex/Cursor account discovery, CI and physical
Cardputer-Adv acceptance remain to be verified on the target machines and device.
The baseline description below records the state when this plan was written.

The repository already has the pieces this feature should build on:

```text
Cardputer Hub
    ↓
MiniAppRuntime
    ↓
Services
    ↓
CompanionService
    ⇅
Companion Protocol v2
    ⇅
Cardputer Companion.app
```

Current relevant production behavior:

- `MAC CONTROL` uses Companion request/response operations.
- `MAC STATUS` uses the capability-gated `SYSTEM_METRICS` operation through `MacStatusService`.
- Companion protocol currently negotiates v1/v2.
- `CompanionService` owns request IDs, timeouts, capability publication, response routing, and session state.
- `IndicatorService` already arbitrates Puzzle output using claims and priorities.
- `PomodoroLedController` owns a `BackgroundApplication` claim while Pomodoro is active.
- `LED GALLERY` owns a `ForegroundApplication` claim while open.
- shared LED brightness is already enforced by `IndicatorService`; AI Usage must not bypass it.
- normal Mini Apps use the full 240×135 display and may omit internal chrome when the content is self-explanatory.

Plan 038 adds:

```text
AI USAGE
```

plus automatic discovery of local Codex and Cursor usage on the connected Mac.

No user configuration is allowed for deciding:

```text
home vs work Mac
ChatGPT Plus vs Business
Codex rolling limits vs Business credits
Cursor installed vs absent
Cursor Enterprise vs unavailable
```

The connected computer must describe itself through the data the Companion can actually obtain.

---

# 1. Goal

Create a zero-configuration AI usage monitor for Cardputer Hub.

The feature should automatically adapt to the currently connected Mac.

Expected examples:

```text
HOME MAC
    ↓
Codex detected
plan = Plus
limits = 5 hour + weekly
Cursor absent
    ↓
Cardputer shows only CODEX · PLUS
```

```text
WORK MAC
    ↓
Codex detected
plan = Business
limit = credits
Cursor detected
plan = Enterprise
limit = personal spend
    ↓
Cardputer shows CODEX · BUSINESS + CURSOR · ENTERPRISE
```

The user must never configure:

```text
this is my home Mac
this is my work Mac
enable Cursor
disable Cursor
Codex account type
Cursor account type
credit limit
billing cycle
```

The Companion owns discovery.

The Cardputer renders the normalized snapshot it receives.

---

# 2. Confirmed provider sources

The implementation is based on provider mechanisms already verified manually.

## Codex / ChatGPT

The local Codex app-server exposes:

```text
account/rateLimits/read
```

Observed Plus response includes:

```text
planType = plus

primary / secondary:
    usedPercent
    windowDurationMins
    resetsAt
```

Observed windows:

```text
300 minutes   → 5 hour
10080 minutes → weekly
```

Observed Business response includes:

```text
planType = business

individualLimit:
    limit
    used
    remainingPercent
    resetsAt
```

The Business values represent workspace credits.

Do not assume:

```text
primary == 5 hour
secondary == weekly
```

Identify rolling windows by `windowDurationMins`.

Do not infer USD from Business credits in the MVP.

## Cursor

The logged-in Cursor Agent account provides a session credential in macOS Keychain.

The verified usage endpoint is:

```text
GET https://cursor.com/api/usage-summary
```

Observed Enterprise response includes:

```text
billingCycleStart
billingCycleEnd
membershipType
limitType

individualUsage.overall:
    enabled
    used
    limit
    remaining

teamUsage.onDemand:
    enabled
    used
    limit
    remaining
```

The numeric spend values are cents.

Example:

```text
9458   → $94.58
255000 → $2,550.00
```

MVP Cardputer presentation uses:

```text
individualUsage.overall
```

The team on-demand pool is not shown on the main Cardputer screen.

The Cursor endpoint is not treated as a stable public integration contract. Isolate it behind a provider adapter so it can be replaced without changing Companion protocol or firmware UI.

---

# 3. End-state architecture

```text
                      macOS Companion
                           │
          ┌────────────────┴────────────────┐
          │                                 │
          ▼                                 ▼
  CodexUsageProvider                 CursorUsageProvider
  codex app-server                   Keychain + HTTPS
          │                                 │
          └────────────────┬────────────────┘
                           ▼
                    AiUsageCollector
                           │
                    normalized cache
                           │
                    CompanionSession
                           │
                      AI_USAGE v3
                           │
                           ▼
                    CompanionService
                           │
                           ▼
                     AiUsageService
                     │           │
                     │           └── AiUsageIndicatorController
                     ▼                          │
                  AI USAGE                      ▼
                  Mini App                 Puzzle Unit 8×8
```

Ownership rule:

> Provider-specific authentication, subprocesses, HTTP, JSON and account-shape detection live on macOS. Cardputer firmware receives only a bounded normalized usage model.

---

# 4. Zero-configuration discovery

Provider discovery runs automatically.

Conceptually:

```text
Companion starts
    ↓
discover Codex
discover Cursor
    ↓
refresh providers
    ↓
publish normalized cached snapshot
```

No settings screen is added.

## Codex discovery

Try to locate a usable `codex` executable automatically.

Discovery may use:

```text
current process PATH
known user-local locations
Homebrew locations
login-shell PATH fallback
```

Do not require the user to enter an executable path.

Suggested candidate locations include, but are not limited to:

```text
~/.local/bin/codex
/opt/homebrew/bin/codex
/usr/local/bin/codex
```

Do not hardcode one location as authoritative.

When found, start and maintain:

```text
codex app-server --listen stdio://
```

Perform the normal JSONL initialization handshake and query:

```text
account/rateLimits/read
```

The subprocess is owned by the Codex provider and may be restarted with bounded backoff if it exits.

## Cursor discovery

Cursor is considered absent when no usable local Cursor Agent credential is available.

On macOS, read the existing Cursor Agent session from Keychain using the Security framework.

Do not shell out to:

```text
security ...
```

from the production Companion implementation.

The token:

- stays in Companion memory;
- is never transmitted to Cardputer;
- is never written to logs;
- is never persisted by Cardputer Hub;
- is sent only to Cursor for the usage request.

If Cursor is not installed or not authenticated:

```text
Cursor provider is absent
```

This is not an error screen.

---

# 5. Account-type detection

Account type is data-driven.

## Codex Plus / rolling limits

If the response contains usable rolling windows:

```text
windowDurationMins = 300
windowDurationMins = 10080
```

normalize as:

```text
provider = CODEX
plan = PLUS
metrics:
    FIVE_HOUR
    WEEK
```

The provider label should come from `planType` when available rather than from the Mac identity.

## Codex Business / credits

If the response contains:

```text
individualLimit
```

normalize as:

```text
provider = CODEX
plan = BUSINESS
metric:
    CREDITS
```

Use:

```text
limit
used
remainingPercent
resetsAt
```

Do not force the response into 5-hour/week semantics.

## Cursor

A successful `usage-summary` response determines:

```text
provider = CURSOR
plan = membershipType
metric = MONEY
```

For the verified corporate account:

```text
membershipType = enterprise
```

The main metric uses:

```text
individualUsage.overall
```

---

# 6. Provider absence vs provider failure

These are different states.

## Absent

Examples:

```text
Cursor Agent not installed
Cursor credential does not exist
Codex executable not found
```

Behavior:

```text
provider is omitted
```

Do not render:

```text
CURSOR NOT INSTALLED
CURSOR OFF
NO CURSOR
```

when another provider is working.

## Detected but temporarily failing

Examples:

```text
Codex subprocess crashed after previously working
Cursor request times out after previously working
temporary network failure
```

Behavior:

```text
retain last valid provider snapshot
mark provider stale
```

If a provider has never produced a valid sample and cannot currently load, it may remain omitted from the normal snapshot.

Diagnostics may expose the distinction inside Companion, but there is no user configuration associated with it.

---

# 7. Refresh model on macOS

The Cardputer request path must not wait for provider network/subprocess work.

`AiUsageCollector` owns a background cache.

Recommended behavior:

```text
Companion launch
→ immediate provider discovery / refresh

Mac wake
→ immediate refresh

provider process recovery
→ immediate refresh

normal refresh
→ every 60 seconds
```

No catch-up bursts.

At most one refresh per provider may be in flight.

Provider failures use bounded retry/backoff.

A Cardputer `AI_USAGE` request returns the current cached normalized snapshot immediately.

If discovery is still running and no valid provider exists yet:

```text
snapshot state = Discovering
provider count = 0
```

---

# 8. Normalized Companion model

Provider-specific JSON must not cross the Companion boundary.

Conceptually:

```swift
enum AiProviderId {
    codex
    cursor
}

enum AiPlanKind {
    unknown
    plus
    business
    enterprise
}

enum AiMetricKind {
    fiveHour
    week
    credits
    money
}

enum AiMetricUnit {
    percent
    credits
    cents
}

enum AiProviderFreshness {
    fresh
    stale
}

struct AiUsageMetric {
    kind
    unit

    used
    limit
    remaining
    remainingPercent

    resetAt
    resetRemainingSeconds
}

struct AiUsageProviderSnapshot {
    provider
    plan
    freshness
    metrics[]
}

struct AiUsageSnapshot {
    generation
    state
    providers[]
}
```

Exact Swift/C++ types may differ.

MVP bounds:

```text
max providers = 2
max metrics per provider = 2
```

Current expected combinations are:

```text
CODEX PLUS
    5 HOUR
    WEEK

CODEX BUSINESS
    CREDITS

CURSOR ENTERPRISE
    MONEY
```

---

# 9. Host switching is a hard isolation boundary

The Cardputer may move between saved Macs.

A snapshot belongs only to the Companion session that produced it.

When the active Companion session disappears or changes:

```text
old AI snapshot
→ clear immediately

old Puzzle AI claim
→ clear immediately

old provider selection
→ clear immediately
```

Then:

```text
new Companion handshake
→ new AI usage snapshot
→ rebuild UI and Puzzle
```

Required invariant:

```text
Work Mac → Home Mac
```

must never temporarily produce:

```text
CODEX · PLUS
CURSOR · ENTERPRISE   ← stale provider from the previous Mac
```

Session identity is stronger than freshness.

No AI usage data survives a Companion session change.

---

# 10. Companion protocol v3

Current production protocol supports v1/v2.

Plan 038 introduces:

```text
Companion Protocol v3
```

Supported versions after implementation:

```text
v3
v2
v1
```

New Companion HELLO:

```text
3
2
1
```

Select the highest mutually supported version.

Compatibility matrix:

```text
new firmware + new Companion
→ v3
→ AI_USAGE available

new firmware + v2 Companion
→ v2
→ MAC STATUS continues
→ AI USAGE unavailable

new firmware + v1 Companion
→ v1
→ existing v1 functionality continues

old firmware + new Companion
→ old firmware selects its supported version
→ existing functionality continues
```

Do not change GATT UUIDs or chunk framing.

---

# 11. New capability

Protocol v3 adds:

```text
CompanionCapability::AiUsage
```

mapped to:

```text
AI_USAGE
```

The capability means:

> this Companion build supports the AI usage request contract.

It does **not** mean:

> every supported AI provider is installed and authenticated.

This distinction avoids dynamic capability negotiation after the handshake.

Therefore:

```text
v3 compatible Companion
→ advertises AI_USAGE

AI_USAGE request
→ may return Discovering / zero providers
```

The Launcher can keep the Mini App eligible while provider discovery finishes.

---

# 12. New operation

Protocol v3 adds:

```text
AI_USAGE
```

as a request/response operation.

Request payload:

```text
empty
```

Response states:

```text
OK
→ normalized snapshot payload

NOT_AVAILABLE
→ collector unavailable at the Companion level

MALFORMED
→ invalid request / internal encoding failure
```

Provider absence belongs inside a valid snapshot and does not make the entire operation `NOT_AVAILABLE`.

---

# 13. AI usage payload

Use a fixed, bounded binary schema.

Do not send JSON over BLE.

Recommended schema concept:

```text
payload schema version
snapshot state
provider count

provider[0..N]:
    provider id
    plan id
    freshness
    metric count

    metric[0..M]:
        metric kind
        unit
        used
        limit
        remaining
        remaining percent
        reset epoch
        reset remaining seconds
```

All multi-byte numeric fields use the project protocol's existing byte-order convention.

Requirements:

```text
payload <= companionMaxPayloadSize
max providers enforced
max metrics enforced
unknown enum rejected
percent clamped/validated to 0..100
numeric overflow rejected
truncated payload rejected
extra unexpected bytes rejected according to schema contract
```

Cursor cents fit comfortably in `uint32`.

Codex displayed credits may be rounded to whole credits for the Cardputer UI.

The Companion may keep higher precision internally.

---

# 14. Reset representation

The UI needs both stable cycle identity and convenient formatting.

Each normalized metric should preserve:

```text
resetAt
resetRemainingSeconds
```

`resetAt` is useful for:

```text
detecting that a cycle changed
detecting a completed reset
```

`resetRemainingSeconds` is useful because the Cardputer does not need a wall-clock/timezone dependency merely to display a countdown.

Presentation rules:

```text
< 24 hours
→ RESET 4H 58M

>= 24 hours
→ RESET 6D 23H
```

For MVP, relative reset text is preferred over requiring local calendar/date formatting.

The UI examples may use `OCT 1` conceptually, but implementation should remain timezone-independent unless a general time service already exists.

---

# 15. Firmware service

Add:

```text
src/services/ai_usage/
    ai_usage_service.h
    ai_usage_service.cpp
```

Responsibilities:

```text
observe AI_USAGE capability/session
request cached snapshot
decode normalized payload
own current AiUsageSnapshot
track freshness/generation
clear state on session change
poll while AI_USAGE capability is live
```

Recommended Cardputer poll interval:

```text
30 seconds
```

Rationale:

- provider data changes slowly;
- Companion already refreshes provider data in the background;
- the BLE request only retrieves cached state;
- 30 seconds is responsive enough for a physical quota dashboard;
- reset countdown can advance locally between responses.

At most one `AI_USAGE` request may be in flight.

Do not queue missed polls.

---

# 16. Firmware snapshot model

Conceptually:

```cpp
enum class AiUsageState {
    Empty,
    Discovering,
    Ready,
};

enum class AiProvider {
    Codex,
    Cursor,
};

enum class AiPlan {
    Unknown,
    Plus,
    Business,
    Enterprise,
};

enum class AiMetricKind {
    FiveHour,
    Week,
    Credits,
    Money,
};

enum class AiMetricUnit {
    Percent,
    Credits,
    Cents,
};

struct AiUsageMetric {
    ...
};

struct AiUsageProviderSnapshot {
    ...
};

struct AiUsageSnapshot {
    generation;
    state;
    providers;
};
```

Mini App code must not parse Companion envelopes.

Puzzle code must not parse Companion envelopes.

Both consume `AiUsageService` state.

---

# 17. App registration

Register:

```text
id: ai-usage
displayName: AI USAGE
iconId: ai-usage
entryRoute: ai-usage
requiredCapabilities:
    AI_USAGE
```

The Launcher already communicates Companion capability availability.

Do not add a duplicate live/connected badge inside the app.

---

# 18. UI direction

Use **Variant A**.

Hard requirements:

```text
240 × 135
no internal app header
no title bar
no footer
no connection-state chrome
no host name
no LIVE badge
no permanent UPDATED label
no rounded cards
no ornamental containers
```

The entire display is data.

Provider labels are content, not a global page header.

Use the existing Cardputer Hub visual system:

```text
Bone
Ink
Pale
Blue
Leaf
Ordinal
Vermilion
Micro 5 where appropriate
compact bitmap font for labels/body text
1 px separators
```

---

# 19. Main semantic rule: show remaining capacity

Progress bars represent:

```text
remaining
```

not:

```text
used
```

Therefore:

```text
long bar  → plenty left
short bar → quota nearly exhausted
```

Text must explicitly say:

```text
LEFT
```

so color is never the sole carrier of meaning.

---

# 20. One-provider layout — Codex Plus

Conceptual screen:

```text
┌────────────────────────────────────────┐
│ CODEX · PLUS                           │
│                                        │
│ 5 HOUR                       100% LEFT │
│ ██████████████████████████████████████ │
│ RESET 4H 58M                           │
│                                        │
│ WEEK                         100% LEFT │
│ ██████████████████████████████████████ │
│ RESET 6D 23H                           │
└────────────────────────────────────────┘
```

Recommended pixel geometry:

```text
surface                    240 × 135

left margin                8
right edge                 232

provider label             x=8,   y≈6

metric A label/value       y≈28
metric A bar               x=8,   y≈43, w=224, h=7
metric A reset             x=8,   y≈55

metric B label/value       y≈83
metric B bar               x=8,   y≈98, w=224, h=7
metric B reset             x=8,   y≈110
```

Exact baselines may be tuned on hardware, but the hierarchy must remain.

---

# 21. Two-provider layout — Work Mac

Conceptual screen:

```text
┌────────────────────────────────────────┐
│ CODEX · BUSINESS                       │
│ 19,765 / 20,000 CR              1% LEFT│
│ █                                      │
│ RESET 3D 8H                            │
├────────────────────────────────────────┤
│ CURSOR · ENTERPRISE                    │
│ $94.58 / $2,550                96% LEFT│
│ ████████████████████████████████████   │
│ RESET 3D 8H                            │
└────────────────────────────────────────┘
```

Recommended geometry:

```text
provider 1 region            y=0..66
separator                    y=67
provider 2 region            y=68..134
```

Within each provider region:

```text
provider / plan              x=8, y=region+6
main value                   x=8, y=region+24
remaining percent            right-aligned to x=232
bar                          x=8, y=region+40, w=224, h=7
reset                        x=8, y=region+53
```

Use one-pixel separator in the shared palette.

No cards.

---

# 22. Single-provider / single-metric layout

If only one provider exists and it has one metric, do not render an empty second block.

Use the full screen with more breathing room:

```text
CODEX · BUSINESS

19,765 / 20,000 CR
1% LEFT !

█

RESET 3D 8H
```

or:

```text
CURSOR · ENTERPRISE

$94.58 / $2,550
96% LEFT

████████████████████████████████████

RESET 3D 8H
```

The same formatting helpers should be reused.

---

# 23. Progress-bar color semantics

Suggested remaining-capacity mapping:

```text
51..100% left
→ Leaf

20..50% left
→ Blue

5..19% left
→ Vermilion

0..4% left
→ Vermilion + textual "!"
```

Do not add a new yellow palette token.

Critical state must remain understandable without color.

Example:

```text
1% LEFT !
```

---

# 24. Loading state

During initial provider discovery, before any usable snapshot:

```text
┌────────────────────────────────────────┐
│                                        │
│                                        │
│              CHECKING AI               │
│                                        │
│                 · · ·                  │
│                                        │
│                                        │
└────────────────────────────────────────┘
```

This is a temporary state.

Do not show fake provider placeholders.

---

# 25. Zero-provider state

If discovery completes and no provider is usable:

```text
NO AI ACCOUNTS
```

with a compact secondary explanation such as:

```text
CODEX / CURSOR NOT AVAILABLE
```

Do not add configuration controls.

The user fixes authentication/install state on the Mac.

The Mini App is read-only.

---

# 26. Stale provider state

If a provider previously returned valid data but refresh later fails:

- retain the last values;
- visibly mark that provider `STALE`;
- do not present stale data as silently current.

Example:

```text
CODEX · PLUS                     STALE
```

Optionally replace the reset row with:

```text
LAST UPDATE 3M AGO
```

once the reset estimate itself can no longer be considered trustworthy.

Do not replace the entire app with a global error if another provider remains fresh.

---

# 27. Dirty-region rendering

Follow existing project rendering behavior.

`AiUsageApp` should capture a typed render frame containing only presentation-relevant values.

Redraw only when:

```text
snapshot generation changes
provider freshness changes
formatted reset countdown changes
temporary selection marker changes
```

A settled screen must stop repainting continuously.

The progress bars should redraw only their own bounded regions where practical.

---

# 28. In-app Puzzle focus interaction

AI USAGE remains primarily a read-only dashboard.

The only local interaction is optional Puzzle inspection.

Use existing physical Up/Down navigation keys to cycle through visible metrics.

Examples:

```text
Plus:
    5 HOUR
    WEEK

Work:
    CODEX BUSINESS
    CURSOR ENTERPRISE
```

Selection exists only to choose the metric temporarily expanded on Puzzle.

LCD indication should remain minimal.

Recommended treatment:

```text
small 2 px Blue tick at the left edge of the selected metric block
```

Do not introduce a large focus card or highlighted background.

After approximately:

```text
3 seconds
```

without selection input:

```text
selection marker disappears
Puzzle returns to overview mode
```

The dashboard itself does not page or change content.

Escape remains shell-owned and closes the Mini App normally.

---

# 29. Puzzle Unit purpose

Puzzle output is not decoration.

It acts as:

```text
always-visible AI quota gauge
threshold warning surface
high-resolution selected-metric gauge
reset feedback surface
```

It remains useful even when:

```text
Home is open
another normal Mini App is open
LCD is dimmed/off
```

subject to normal `IndicatorService` priority arbitration.

---

# 30. Puzzle arbitration

Add an AI usage indicator owner:

```text
ai-usage
```

Background quota overview should use:

```text
IndicatorPriority::Idle
```

This ensures existing meaningful features win naturally.

Examples:

```text
Pomodoro running
→ BackgroundApplication
→ overrides AI usage overview

LED Gallery open
→ ForegroundApplication
→ overrides AI usage overview
```

When AI USAGE is actively inspecting a metric, its temporary full-matrix focus may use:

```text
ForegroundApplication
```

and is released after the focus timeout.

Critical quota feedback may use a temporary higher-priority warning claim, but it must be short-lived and bounded.

All output must go through `IndicatorService`.

Never write directly to Puzzle hardware.

Never bypass the configured 1–10% LED brightness ceiling.

---

# 31. Puzzle overview — one metric

If exactly one relevant metric is visible:

```text
use full 8×8
```

All 64 pixels represent remaining capacity.

Conceptually:

```text
96% left

████████
████████
████████
████████
████████
████████
████████
█████□□□
```

Use a deterministic fill order.

Do not use random animation in the resting gauge.

---

# 32. Puzzle overview — two metrics

If two metrics are visible:

```text
top 4 rows    → metric A
bottom 4 rows → metric B
```

Examples:

```text
HOME
top    Codex 5 hour
bottom Codex week
```

```text
WORK
top    Codex Business credits
bottom Cursor Enterprise spend
```

Order is stable.

Do not insert a full empty divider row.

A divider row wastes 12.5% of the entire matrix.

---

# 33. Puzzle split identity markers

The two halves need visible identity beyond their position.

Reserve exactly one neutral marker pixel per half:

```text
top metric marker
→ top-left pixel

bottom metric marker
→ bottom-right pixel
```

Marker color:

```text
Ordinal / quiet neutral
```

The remaining:

```text
31 pixels per half
```

represent quota.

Markers are not counted as quota fill.

Conceptual identity:

```text
top half
M███████
████████
████□□□□
□□□□□□□□

bottom half
████████
████████
██████□□
□□□□□□□M
```

`M` is a dim neutral marker, not a colored quota pixel.

This creates separation without sacrificing an entire row.

---

# 34. Puzzle fill semantics

Quota pixels represent:

```text
remainingPercent
```

For a split half:

```text
lit quota pixels = round(31 × remainingPercent / 100)
```

For full 8×8:

```text
lit pixels = round(64 × remainingPercent / 100)
```

Clamp:

```text
0..100
```

A positive non-zero percentage may use at least one quota pixel so `1% LEFT` is visually distinguishable from zero.

Zero remains zero quota pixels.

Markers remain visible in split mode.

---

# 35. Puzzle colors

Use the same semantic remaining-capacity mapping as LCD:

```text
51..100% → Leaf
20..50%  → Blue
5..19%   → Vermilion
0..4%    → Vermilion
```

The fill amount is the primary information.

Color is secondary.

Therefore two metrics remain distinguishable even if both happen to have the same color.

---

# 36. Puzzle critical behavior

Normal state:

```text
static gauge
```

No decorative breathing or spinner.

For:

```text
0..4% remaining
```

allow a subtle low-duty-cycle pulse.

Do not create a high-frequency attention animation.

Threshold feedback:

```text
cross below 20%
→ one brief pulse

cross below 5%
→ two brief pulses
```

Threshold feedback should occur only on an actual crossing, not on every refresh.

Do not wake the LCD solely for these Puzzle pulses.

---

# 37. Puzzle reset feedback

Detect a new quota cycle using normalized reset identity/state.

When a metric resets and remaining capacity increases into the new cycle:

```text
play one brief fill animation
0 → current remaining
```

Then return to the static gauge.

Do not loop the reset animation.

Do not infer reset merely from a single failed sample.

---

# 38. Puzzle selected-metric mode

While AI USAGE is open and the user selects a metric:

```text
selected metric
→ full 8×8 gauge
```

This provides higher resolution than the 31-pixel split gauge.

Example:

```text
5 hour = 37% left

████████
████████
████████
████████
████████
████□□□□
□□□□□□□□
□□□□□□□□
```

After the focus timeout:

```text
release foreground claim
→ normal background overview returns
```

This behavior is temporary and does not change persistent settings.

---

# 39. Puzzle behavior on host switch

Host switching is immediate cleanup.

When Companion session changes:

```text
AI background claim
→ clear/release

AI focus claim
→ clear/release

threshold state
→ clear

metric selection
→ clear
```

Do not keep the old Work gauge visible while Home discovery runs.

During discovery, the Puzzle may be blank unless another higher-priority owner is active.

---

# 40. AiUsageIndicatorController

Add a reusable service/controller rather than embedding Puzzle policy in `AiUsageApp`.

Suggested location:

```text
src/services/ai_usage/
    ai_usage_indicator_controller.h
    ai_usage_indicator_controller.cpp
```

Responsibilities:

```text
consume AiUsageSnapshot
choose overview metrics
build 8×8 quota frames
manage Idle background claim
manage temporary foreground focus claim
manage threshold-crossing feedback
manage reset feedback
clear on session change
```

Must not own:

```text
Companion protocol parsing
provider HTTP
provider authentication
LCD rendering
Mini App navigation
```

---

# 41. Companion Swift boundaries

Add a provider abstraction.

Conceptually:

```swift
protocol AiUsageProviding {
    var id: AiProviderId { get }

    func discover() async -> Bool
    func refresh() async -> AiUsageProviderSnapshot?
}
```

Possible structure:

```text
companion/macos/Sources/CompanionCore/
    AiUsage.swift
    AiUsageCollector.swift
    CodexUsageProvider.swift
    CursorUsageProvider.swift
```

Platform-specific credential/process/network code may remain in the App target when needed, but keep the normalized model and collector testable.

The exact file split may vary.

---

# 42. Codex provider implementation

Responsibilities:

```text
find executable
start app-server
perform initialize handshake
issue account/rateLimits/read
parse only the expected response
normalize supported account shapes
restart crashed process with backoff
```

Do not parse `/status` terminal output.

Do not scrape ChatGPT web UI.

Do not persist ChatGPT authentication outside Codex's own existing storage.

Do not send account IDs to Cardputer.

Ignore unrelated app-server notifications.

Use request IDs and JSON decoding rather than line-position assumptions.

---

# 43. Cursor provider implementation

Responsibilities:

```text
locate existing Cursor Agent authentication
read credential through macOS Keychain API
derive request authentication in memory
request /api/usage-summary
decode supported response fields
normalize individualUsage.overall
```

Do not expose:

```text
raw token
JWT claims
cookie
email
team identifiers
```

to firmware.

Do not log secrets.

Do not persist the Cursor credential in Cardputer Hub storage.

The provider must be isolated because the verified endpoint may change.

A future migration to an official Cursor usage API should replace this adapter without changing the firmware model.

---

# 44. Companion menu-bar diagnostics

Do not add configuration.

It is acceptable to expose read-only diagnostics in the existing Companion menu UI, for example:

```text
AI Usage
Codex: Plus · OK
Cursor: Enterprise · OK
Last refresh: 12s ago
```

This is diagnostic only.

No provider enable/disable switches.

No account-type selectors.

No limit fields.

No token display.

This section may be omitted from the first implementation if it unnecessarily expands scope.

---

# 45. Security requirements

Hard requirements:

```text
OpenAI auth never leaves the Mac
Cursor auth never leaves the Mac

no credentials in BLE payloads
no credentials in logs
no credentials in diagnostics UI
no credentials in NVS
no credentials on microSD
```

The Cardputer receives only normalized usage numbers and labels represented by enums.

On Companion stop:

```text
terminate owned Codex subprocess
clear in-memory provider state
clear Cursor token references
```

---

# 46. Error handling

Provider failures are independent.

Example:

```text
Codex fresh
Cursor stale
```

must still render Codex normally.

Do not turn the whole app into an error.

A malformed provider response:

```text
reject provider sample
retain previous valid provider snapshot as stale
```

A malformed BLE AI usage payload:

```text
reject entire new snapshot
retain existing firmware snapshot until it becomes stale
```

A Companion session change:

```text
clear immediately
```

This is different from an ordinary refresh failure.

---

# 47. Main-loop composition

Normal firmware becomes conceptually:

```text
CompanionService
    ↓
AiUsageService
    ↓
AiUsageIndicatorController

AiUsageService
    ↓
AiUsageApp
```

Recommended update order:

```text
companion.update(elapsed)
hostControl.update()
macStatus.update(elapsed)
aiUsage.update(elapsed)

...
pomodoro.update(elapsed)
pomodoroLed.update(elapsed)
aiUsageIndicator.update(elapsed)

indicator.update()
```

Exact placement may vary, but:

- `CompanionService` must update before AI usage completion consumption;
- all indicator claims must be updated before `IndicatorService::update()` resolves output.

---

# 48. Completion ownership

Add operation-filtered completion use for:

```text
AI_USAGE
```

Ownership invariant:

```text
HostControlService
→ APP_ACTIVATE

MacStatusService
→ SYSTEM_METRICS

AiUsageService
→ AI_USAGE
```

No service may drain another operation's completion.

Existing filtered completion support should be reused.

---

# 49. BDD — Home Mac auto-detection

```gherkin
Scenario: Home Mac exposes only Codex Plus
  Given Companion discovers Codex
  And account/rateLimits/read reports plan Plus
  And 300-minute and 10080-minute windows
  And Cursor is not available
  When AI usage snapshot is normalized
  Then exactly one provider is published
  And the provider is CODEX · PLUS
  And it contains 5 HOUR and WEEK metrics
  And no Cursor placeholder is published
```

---

# 50. BDD — Work Mac auto-detection

```gherkin
Scenario: Work Mac exposes Codex Business and Cursor Enterprise
  Given Codex reports individualLimit
  And planType is Business
  And Cursor usage-summary reports membershipType Enterprise
  When AI usage snapshot is normalized
  Then CODEX · BUSINESS contains a credit metric
  And CURSOR · ENTERPRISE contains a money metric
  And no manual account configuration is required
```

---

# 51. BDD — Host switch isolation

```gherkin
Scenario: Switching from Work Mac to Home Mac clears old provider data
  Given Work Mac currently publishes Codex Business and Cursor Enterprise
  And the Puzzle shows their split overview
  When the Work Companion session disappears
  Then the AI usage firmware snapshot is cleared immediately
  And the AI Puzzle claims are cleared immediately
  When Home Companion becomes Ready
  And publishes Codex Plus only
  Then the LCD shows only Codex Plus
  And the Puzzle shows 5-hour and weekly limits
  And Cursor Enterprise never appears in the Home session
```

This is a required regression test.

---

# 52. BDD — Cursor absent

```gherkin
Scenario: Cursor is not installed on the connected Mac
  Given Codex usage is available
  And Cursor credentials are absent
  Then Cursor provider is omitted
  And AI USAGE remains usable
  And the UI does not show a Cursor error or empty card
```

---

# 53. BDD — Provider stale

```gherkin
Scenario: Cursor temporarily fails after a valid sample
  Given Cursor previously returned a valid Enterprise snapshot
  When subsequent refreshes fail
  Then the last Cursor values remain available
  And Cursor is marked stale
  And fresh Codex data continues to render normally
```

---

# 54. BDD — UI has no header

```gherkin
Scenario: AI USAGE opens with one provider
  Given a valid Codex Plus snapshot
  When AI USAGE renders
  Then the 240×135 surface is used for provider data
  And there is no AI USAGE title row
  And there is no LIVE indicator
  And there is no Companion indicator
  And there is no host-name row
```

---

# 55. BDD — remaining bar semantics

```gherkin
Scenario: Quota decreases
  Given a metric has 80 percent remaining
  When it later has 20 percent remaining
  Then the progress bar becomes shorter
  And the numeric label says 20% LEFT
```

---

# 56. BDD — Puzzle split overview

```gherkin
Scenario: Two metrics are shown on Puzzle
  Given the overview contains two metrics
  Then the first metric uses the top four rows
  And the second metric uses the bottom four rows
  And no full divider row is reserved
  And the top-left marker identifies the top zone
  And the bottom-right marker identifies the bottom zone
  And each zone has 31 quota pixels
```

---

# 57. BDD — Puzzle priority

```gherkin
Scenario: Pomodoro starts while AI quota overview is active
  Given AI usage owns an Idle indicator claim
  When Pomodoro acquires BackgroundApplication
  Then Pomodoro output wins
  And AI usage does not release or corrupt Pomodoro state
  When Pomodoro becomes idle
  Then the AI quota overview becomes visible again
```

```gherkin
Scenario: LED Gallery opens
  Given AI usage background gauge exists
  When LED Gallery acquires ForegroundApplication
  Then LED Gallery owns the Puzzle output
  When LED Gallery closes
  Then the AI quota overview may return
```

---

# 58. BDD — Puzzle focus

```gherkin
Scenario: User inspects a metric in AI USAGE
  Given AI USAGE is open
  And two overview metrics exist
  When the user moves selection to one metric
  Then the selected metric uses the full 8×8 Puzzle
  And the LCD shows only a minimal selection tick
  When the focus timeout expires
  Then the foreground AI claim is released
  And the split overview returns
```

---

# 59. BDD — threshold notification

```gherkin
Scenario: Remaining capacity crosses below 5 percent
  Given a metric previously had at least 5 percent remaining
  When a fresh snapshot reports less than 5 percent remaining
  Then one bounded critical Puzzle feedback sequence occurs
  And it does not repeat on every identical refresh
  And it does not wake the LCD
```

---

# 60. BDD — reset

```gherkin
Scenario: A quota cycle resets
  Given a metric belongs to the previous reset cycle
  When a fresh snapshot identifies a new reset cycle
  And remaining capacity increases
  Then one bounded fill animation is shown on Puzzle
  And the normal static gauge resumes afterward
```

---

# 61. Protocol tests

Add coverage for:

```text
v3 negotiation
v2 fallback
v1 fallback

v3 capability list includes AI_USAGE
v2 capability list does not include AI_USAGE

AI_USAGE request only allowed in v3

AI usage payload round-trip

zero providers
one provider / one metric
one provider / two metrics
two providers

invalid provider count
invalid metric count
unknown provider enum
unknown plan enum
unknown metric enum
invalid percent
numeric overflow/truncation
payload too short
payload too long
```

Keep matching Swift/firmware fixtures where useful.

---

# 62. macOS provider tests

## Codex

Use fake process/JSONL transport.

Cover:

```text
executable absent
successful initialize
Plus rolling limits
windows arriving primary/secondary in either semantic order
Business individualLimit
missing optional fields
malformed JSON
request ID mismatch
process exit
restart/backoff
no secret logging
```

## Cursor

Use fake Keychain and HTTP boundaries.

Cover:

```text
credential absent
Enterprise success
cent conversion
individual overall disabled
malformed response
401 / expired credential
timeout
stale-cache behavior
no secret logging
```

No test should require real OpenAI/Cursor accounts.

---

# 63. AiUsageService tests

Cover:

```text
AI_USAGE unavailable
immediate request when capability appears
30-second polling
one request in flight
valid snapshot accepted
generation changes
provider ordering stable
session change clears immediately
ordinary timeout does not clear immediately
stale transition
recovery
filtered completion ownership
```

Use injected elapsed time.

No real sleeps.

---

# 64. AI USAGE UI tests

Cover:

```text
AppRegistry registration
requires AI_USAGE

no internal title/header
no host name
no LIVE indicator
no connection dot

Plus one-provider/two-metric layout
Business one-provider/one-metric layout
two-provider Work layout

remaining-bar geometry
right-aligned percentage
credit formatting
USD formatting from cents
reset formatting
critical "!" marker

loading state
zero-provider state
per-provider stale state

selection tick
selection timeout
unchanged frame does not repaint continuously

Escape uses shared Mini App exit
wake-only input causes no selection change
```

---

# 65. Puzzle tests

Cover pure frame generation separately from hardware.

Required cases:

```text
0%
1%
4%
5%
19%
20%
50%
51%
100%

single metric full 8×8

two-metric split
top marker reserved
bottom marker reserved
31-pixel quota capacity per half

stable ordering
same color still visually separated

threshold crossing once
no repeated threshold animation
reset animation once
focus mode full 8×8
focus timeout
session clear removes claim

Pomodoro priority wins
LED Gallery priority wins
configured LED brightness remains enforced
```

Do not require physical Puzzle hardware for unit tests.

---

# 66. Companion menu diagnostics tests

Only if the optional diagnostics UI is implemented.

Cover:

```text
provider plan visible
fresh/stale visible
last refresh visible

no token
no account ID
no email
no team ID
no editable configuration
```

---

# 67. Documentation

Update:

```text
README.md
docs/ARCHITECTURE.md
docs/UI_REQUIREMENTS.md
docs/manuals/device-guide.md
docs/plans/README.md
protocol/companion/README.md
companion/macos/README.md
```

Document:

```text
Companion protocol v3
AI_USAGE capability
AI_USAGE operation
automatic Codex/Cursor discovery
zero-configuration account detection
host-switch clearing
AI USAGE UI
Puzzle quota overview
Puzzle priority behavior
security boundary
```

Do not document the Cursor internal endpoint as a stable public Cursor API.

Document it as an implementation adapter subject to change.

---

# 68. Non-goals

Plan 038 does not add:

```text
manual Home/Work profiles
manual provider enable/disable
manual plan selection
manual limit entry
manual $800 mapping for Codex Business credits

ChatGPT web scraping
Cursor dashboard HTML scraping
Cursor team-admin API requirement

Claude
Gemini
GitHub Copilot
other AI providers

historical usage charts
daily usage history
cost forecasting
"days until limit" prediction
recommendations on which model to use

changing or purchasing plans
resetting Codex quota
changing Cursor team limits

sending provider credentials to Cardputer
storing provider credentials on Cardputer

decorative Puzzle animations unrelated to quota state
```

The provider model should remain extensible, but those integrations are separate future work.

---

# 69. Recommended implementation order

```text
01. Add Plan 038 document and architecture notes

02. Introduce Companion protocol v3 negotiation
03. Preserve v1/v2 behavior
04. Add AI_USAGE capability
05. Add AI_USAGE operation
06. Define normalized binary payload
07. Add Swift + C++ payload codecs
08. Add protocol fixtures/tests

09. Add AiUsage normalized Companion model
10. Add Codex executable discovery
11. Add Codex app-server client
12. Add Codex Plus normalization
13. Add Codex Business normalization
14. Add Cursor Keychain boundary
15. Add Cursor usage-summary HTTP adapter
16. Add Cursor Enterprise normalization
17. Add AiUsageCollector cache
18. Add background refresh/backoff
19. Add wake/recovery refresh
20. Add provider tests

21. Extend CompanionSession with AI_USAGE response
22. Add Companion tests
23. Add optional read-only diagnostics

24. Add CompanionService::requestAiUsage()
25. Add firmware payload decoding
26. Add AiUsageService
27. Add session-isolation clearing
28. Add 30-second polling
29. Add freshness handling
30. Add service tests

31. Register AI USAGE AppDescriptor
32. Add Launcher icon
33. Add AiUsageApp
34. Implement one-provider/two-metric layout
35. Implement two-provider layout
36. Implement single-metric layout
37. Implement loading/empty/stale states
38. Add dirty-region rendering
39. Add minimal metric-selection tick
40. Add UI tests

41. Add AiUsageIndicatorController
42. Add single-metric 8×8 gauge
43. Add split 4+4 gauge
44. Add marker pixels
45. Add semantic colors
46. Add Idle background claim
47. Add temporary foreground focus claim
48. Add threshold-crossing feedback
49. Add reset feedback
50. Add host-switch clearing
51. Add Puzzle tests

52. Wire composition in main.cpp
53. Verify coexistence with Pomodoro
54. Verify coexistence with LED Gallery
55. Update docs

56. Run companion checks
57. Run host checks
58. Run firmware checks
59. Run firmware-size
60. Run CI

61. Flash Cardputer
62. Validate Home Mac auto-detection
63. Validate Work Mac auto-detection
64. Validate Work → Home host switch
65. Validate Home → Work host switch
66. Validate LCD 1-provider layout
67. Validate LCD 2-provider layout
68. Validate Puzzle split visibility
69. Validate Puzzle priority with Pomodoro
70. Validate Puzzle priority with LED Gallery
71. Validate low-quota feedback
72. Validate display-off behavior
```

---

# 70. Validation commands

Use targeted tests during implementation.

Final validation should include the repository's complete applicable gates, including:

```bash
make companion-check
make host-check
make firmware-check
make firmware-size
```

plus CI.

Do not consider firmware-only success sufficient because Plan 038 changes both macOS Companion and firmware protocol behavior.

---

# 71. Physical acceptance

## Home Mac

Expected automatic result:

```text
CODEX · PLUS

5 HOUR ...
WEEK ...
```

Expected:

```text
no Cursor block
no setup prompt
no Home/Work setting
```

Puzzle:

```text
top half    5 hour
bottom half week
```

## Work Mac

Expected automatic result:

```text
CODEX · BUSINESS
credits

CURSOR · ENTERPRISE
personal spend
```

Puzzle:

```text
top half    Codex
bottom half Cursor
```

## Host switching

Switch Work → Home.

Expected:

```text
old Work LCD data disappears
old Work Puzzle data disappears
new Home snapshot loads
Cursor does not remain
```

Switch Home → Work.

Expected:

```text
Plus rolling-limit view disappears
Business + Enterprise view appears
Puzzle mapping changes automatically
```

## Cursor absent

On a Mac without Cursor:

```text
no Cursor block
no Cursor error
Codex remains usable
```

## LCD off

Allow the display to turn off while no higher-priority Puzzle owner exists.

Expected:

```text
AI quota gauge remains visible on Puzzle
LCD remains off
Puzzle does not request display wake
```

## Pomodoro

Start Pomodoro.

Expected:

```text
Pomodoro Puzzle output overrides AI overview
```

Stop/reset Pomodoro to idle.

Expected:

```text
AI overview returns
```

## LED Gallery

Open LED Gallery.

Expected:

```text
LED Gallery owns Puzzle
```

Exit.

Expected:

```text
AI overview returns
```

---

# 72. Completion criteria

Plan 038 is complete when:

```text
[ ] Companion protocol v3 is implemented
[ ] v1/v2 compatibility remains intact

[ ] AI_USAGE capability is negotiated
[ ] AI_USAGE request/response is bounded and tested

[ ] Codex is discovered without manual configuration
[ ] Codex Plus 5-hour limit is detected
[ ] Codex Plus weekly limit is detected
[ ] Codex Business credit limit is detected
[ ] rolling windows are identified by duration, not primary/secondary position

[ ] Cursor is discovered without manual configuration
[ ] Cursor absence is silent
[ ] Cursor Enterprise personal spend is detected
[ ] Cursor secrets remain Mac-only

[ ] provider refresh is cached/background
[ ] Cardputer requests never block on provider network work

[ ] host/session change clears AI state immediately
[ ] stale provider data cannot leak between Macs

[ ] AiUsageService owns firmware polling and normalized state
[ ] only one AI usage request may be in flight
[ ] completion ownership is operation-filtered

[ ] AI USAGE is registered as a Mini App
[ ] AI USAGE requires AI_USAGE capability

[ ] UI has no internal header
[ ] UI has no connection chrome
[ ] one-provider Plus layout is implemented
[ ] two-provider Work layout is implemented
[ ] single-provider/single-metric layout is implemented
[ ] progress bars represent remaining capacity
[ ] critical remaining state has a textual marker
[ ] loading/empty/stale states are implemented
[ ] unchanged UI does not repaint continuously

[ ] Puzzle background quota gauge is implemented
[ ] one metric uses full 8×8
[ ] two metrics use 4+4 split
[ ] split uses marker pixels rather than a divider row
[ ] top and bottom zones each retain 31 quota pixels
[ ] quota fill represents remaining capacity
[ ] semantic colors match LCD meaning
[ ] normal gauge is static
[ ] low-quota feedback is bounded
[ ] reset feedback is bounded
[ ] selected metric can temporarily use full 8×8

[ ] AI overview uses Idle indicator priority
[ ] Pomodoro overrides AI overview
[ ] LED Gallery overrides AI overview
[ ] global LED brightness limit remains authoritative

[ ] Work → Home switch passes
[ ] Home → Work switch passes
[ ] Cursor-absent Home behavior passes
[ ] display-off Puzzle behavior passes

[ ] Companion tests pass
[ ] firmware host tests pass
[ ] protocol tests pass
[ ] Puzzle tests pass
[ ] firmware build passes
[ ] firmware-size passes
[ ] CI passes
[ ] physical Cardputer-Adv acceptance passes
```

---

# 73. End state

Home Mac:

```text
┌────────────────────────────────────────┐
│ CODEX · PLUS                           │
│                                        │
│ 5 HOUR                        63% LEFT │
│ ████████████████████████░░░░░░░░░░░░░ │
│ RESET 2H 11M                           │
│                                        │
│ WEEK                          81% LEFT │
│ ███████████████████████████████░░░░░░░ │
│ RESET 5D 18H                           │
└────────────────────────────────────────┘

Puzzle:
top    5 hour
bottom week
```

Work Mac:

```text
┌────────────────────────────────────────┐
│ CODEX · BUSINESS                       │
│ 19,765 / 20,000 CR              1% LEFT│
│ █                                      │
│ RESET 3D 8H                            │
├────────────────────────────────────────┤
│ CURSOR · ENTERPRISE                    │
│ $94.58 / $2,550                96% LEFT│
│ ████████████████████████████████████   │
│ RESET 3D 8H                            │
└────────────────────────────────────────┘

Puzzle:
top    Codex Business
bottom Cursor Enterprise
```

The user changes computers.

Nothing is configured.

The connected Mac's Companion discovers the available accounts, normalizes their limits, and publishes only the providers that actually exist there.

The Cardputer UI and Puzzle automatically become a physical representation of that Mac's current AI quota state.
