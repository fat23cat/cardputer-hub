# 041 — Claude subscription usage and four-metric AI USAGE

## Current status

Software implemented on branch `codex/041-claude-usage`; physical Cardputer-Adv
and Keychain-prompt acceptance pending.

---

## 1. Goal

A personal Mac with both Codex Plus and a Claude Pro/Max subscription shows all
four rolling limits on one AI USAGE screen and on Unit Puzzle:

```text
CODEX · PLUS   R×2          LEFT  RESET
5H  ████████████░░░░         64%  2H 10M
WK  ██████████████░░         81%   4D 6H
─────────────────────────────────────────
CLAUDE · PRO                LEFT  RESET
5H  ██████░░░░░░░░░░         35%  1H 45M
WK  ███████████████░         88%  5D 22H
```

Zero configuration remains the rule: the Companion detects the existing Claude
Code sign-in and omits Claude when it is absent.

## 2. Scope

In scope:

* Companion `ClaudeUsageProvider` reading the Claude Code OAuth credential from
  the macOS Keychain and the Claude subscription usage endpoint.
* Companion protocol v5 with AI_USAGE schema 3: provider `3=Claude`, plans
  `4=Pro`, `5=Max`.
* Firmware decoding of schema 3 and a Claude provider title.
* Two-provider layout where a provider with two rolling windows renders two
  compact rows; hover (Up/Down, or the physical `;` / `.` keys without Fn)
  across up to four rows.
* Enter opens LIMITS details for the hovered provider when it has rolling
  windows (Codex Plus or Claude); the details show no selection marker. RESETS stays Codex Plus only.
* Unit Puzzle overview with three or four metrics: four two-row bands.
* Manuals, protocol README, fixtures, and architecture notes.

### Confirmed provider source

Verified on the personal Mac on 2026-09-28 (Pro subscription, HTTP 200):

```text
Keychain generic password, service "Claude Code-credentials"
    JSON claudeAiOauth { accessToken, expiresAt (ms), subscriptionType, ... }

GET https://api.anthropic.com/api/oauth/usage
    Authorization: Bearer <accessToken>
    anthropic-beta: oauth-2025-04-20

limits[]:
    { kind: "session",    percent: 8, resets_at: ISO8601, scope: null }
    { kind: "weekly_all", percent: 1, resets_at: ISO8601, scope: null }
five_hour / seven_day:
    { utilization: 8.0, resets_at: ISO8601 }        (legacy shape)
```

`limits[]` is preferred; `five_hour` / `seven_day` are the fallback. Scoped or
model-specific limits (`seven_day_opus`, entries with a non-null `scope`) and
code-named experimental fields are ignored. The endpoint is not a public API
and is isolated behind the provider adapter, like the Cursor endpoint.

## 3. Architecture

```text
AiUsageCollector (Mac)
    ├─ CodexUsageProvider
    ├─ CursorUsageProvider
    └─ ClaudeUsageProvider ── ClaudeCredentialReading (Keychain)
                           └─ AiUsageHTTPTransport (shared with Cursor)
        ↓ AiUsageNormalization.claude()
AiUsageSnapshot ── encode(v5 → schema 3; v3/v4 omit Claude)
        ↓ BLE
readAiUsage → AiUsageService → aiUsageVisibleMetrics()
        ├─ AiUsageApp (rows, hover, details)
        └─ AiUsageIndicatorController (full / halves / quarter bands)
```

### Protocol v5

* HELLO offers `[5, 4, 3, 2]`. HELLO still carries at most four versions, so a
  v4 firmware selects v4 and keeps working without Claude. A firmware that
  supports only v1 (older than plan 037) no longer shares a version.
* AI_USAGE schema 3 is byte-for-byte schema 2 with schema byte `3`, provider
  `3=Claude`, and plans `4=Pro`, `5=Max`. Reset details remain Codex Plus only.
* For v3/v4 sessions the Companion drops Claude before encoding, so an older
  firmware still receives Codex and Cursor.
* At most two providers per snapshot remain on the wire. The collector keeps
  every detected provider in the order Codex, Cursor, Claude; the encoder drops
  unsupported providers for older firmware first and then sends the first two.
  `sentProviders` applies both rules; the encoder and the Companion
  diagnostics use it, so diagnostics mark any provider the Cardputer does not
  receive as not sent.

### Credential policy

* The Companion only reads the credential. It never refreshes, writes, or
  deletes it; Claude Code owns token refresh.
* The token is kept in memory until its `expiresAt` or a 401/403 response.
  Every five minutes the Keychain is read again beside a request with the
  cached token, so sign-out or an account switch shows up within about five
  minutes, and a failed or slow recheck never costs a working token.
* Providers report one of five outcomes: sample, absent, failed, unavailable,
  or waiting. Unavailable means the source cannot be read now without
  anything failing; the collector keeps any prior sample, marks it stale, and
  does not enter failure backoff. Waiting is the same while the source waits
  for the user, and it also keeps discovery (`CHECKING AI`) open while no
  other provider can be shown.
* The Keychain read runs on its own queue because macOS may block it on the
  access prompt. A refresh waits at most five seconds and then reports
  waiting, so an open prompt never stalls or multiplies Codex or Cursor
  refreshes.
* Stopping the collector cancels requests; the HTTP transport then starts a
  new session so a later start can make requests again.
* An expired token means Claude Code has not run recently and is reported as
  unavailable; with no earlier sample Claude is simply not shown.
* The usage service rate-limits callers (observed on 2026-09-28: HTTP 429
  `rate_limit_error` after polling every 30 seconds, with no documented limit).
  The 30-second polls were multiplied by failure retries after 1, 2 and 4
  seconds. The provider now asks at most once a minute; refreshes in between
  report the last sample, aged to the current time, for up to one minute plus
  the 90-second freshness window.
* HTTP 429 is not a failure: the provider pauses for the larger of
  `Retry-After` and a pause that doubles from one minute up to 30 minutes,
  and reports the last sample while it is still current, otherwise
  unavailable. The collector therefore never retries into a rate limit.
* HTTP 401/403 or a malformed response is a provider failure: the previous
  sample turns stale after 90 seconds and the collector retries with backoff;
  the provider itself sends at most one request a minute after other
  failures.
* A missing Keychain item is absence. A declined or cancelled Keychain prompt
  hides Claude for one hour before the Companion asks again.
* The token never leaves the Mac and never appears in logs or diagnostics.

### Stale samples age

The collector brings every stale sample up to the current time: reset and
reset-credit countdowns are recomputed from their epochs, so a reconnecting
Cardputer does not start from an old countdown. A stale 5-hour or weekly window
whose reset time has passed no longer describes current usage; it is reported
as 100% left with an unknown next reset (`--`) and keeps the `STALE` mark.

## 4. Ownership & Boundaries

| Component | Owns | May call | Must not call |
| --- | --- | --- | --- |
| `ClaudeUsageProvider` | credential read and cache, one usage request, refresh outcome | `ClaudeCredentialReading`, `AiUsageHTTPTransport`, `AiUsageNormalization` | token refresh, credential writes, `CompanionSession`, cached samples |
| `AiUsageCollector` | cache, freshness, provider order, stale-sample ageing | provider `refresh` | provider JSON parsing |
| `AiUsageSnapshot.encode` | wire bounds, version filter, two-provider cap | — | providers |
| `aiUsageVisibleMetrics` | flattened metric order shared by UI and Puzzle | snapshot | display, indicator |
| `AiUsageApp` | rows, hover, detail view state | `AiUsageService`, `AiUsageIndicatorController::focus` | `CompanionService`, `IndicatorService` |
| `AiUsageIndicatorController` | Puzzle overview, focus, feedback | `IndicatorService` | display, protocol |

## 5. State Model

Visible metrics are the provider metrics flattened in snapshot order (at most
2 providers × 2 metrics). Their count selects the Puzzle overview:

```text
1 metric   → full 8×8 (64 quota pixels)
2 metrics  → two four-row halves (30 quota pixels + 2 purple markers each)
3–4        → four two-row bands (14 quota pixels + 2 purple markers each);
             an unused fourth band stays dark
```

App view state:

```text
Main ──Enter──▶ Limits(provider) ──Enter──▶ Main
                  │  ◀─Left/Right─▶ Resets   (Codex Plus only)
Session change or provider disappearance → Main
```

Enter on Main uses the hovered provider when hover is visible and that
provider has rolling windows; without hover it uses the first provider with
rolling windows. A hovered provider without rolling windows ignores Enter.
Hover clears when a new snapshot has fewer visible metrics than its index.

## 6. BDD Scenarios

### Scenario: Personal Mac shows Codex Plus and Claude on one screen

Given:
- protocol v5 session
- AI_USAGE has Codex Plus (5H, WK) and Claude Pro (5H, WK)

When:
- AI USAGE renders

Then:
- both provider titles, four rows, `LEFT` and `RESET` column labels render
- Puzzle shows four two-row bands with purple ends

### Scenario: Hover the Claude 5-hour row and open details

Given:
- the screen above

When:
- user presses Down three times, then Enter

Then:
- hover moves Codex 5H → Codex WK → Claude 5H; Puzzle focuses Claude 5H
- Enter opens Claude LIMITS; the details do not mark a column
- no LIMITS/RESETS footer appears; Left/Right do nothing
- Enter returns to Main

### Scenario: Claude absent (unavailable dependency)

Given:
- Keychain has no `Claude Code-credentials`

When:
- the collector refreshes

Then:
- no HTTP request is made and Claude is omitted; the Codex Plus layout is
  unchanged

### Scenario: Token expired (Claude Code idle)

Given:
- a fresh Claude sample exists

When:
- the stored token expires

Then:
- no request with an expired token is sent; no token refresh is attempted
- the provider reports unavailable; the collector keeps the last sample
  stale without failure backoff
- its countdowns follow the clock; once a window's reset time passes it shows
  100% left and `--`

### Scenario: Request fails (in-flight failure)

Given:
- a fresh Claude sample exists

When:
- the endpoint returns 401

Then:
- the cached token is dropped and re-read on the next refresh
- the prior sample becomes stale after 90 seconds; retries use backoff

### Scenario: Keychain prompt left open

Given:
- the Keychain prompt is waiting for the user

When:
- the collector refreshes

Then:
- the Claude refresh reports waiting after five seconds; Codex and Cursor
  keep their normal refresh rate; no second read starts while the prompt is
  open
- with no other provider, AI USAGE keeps showing `CHECKING AI`

### Scenario: Older firmware (compatibility)

Given:
- a v4 firmware

When:
- the Companion answers AI_USAGE

Then:
- the payload uses schema 2 without Claude; Codex/Cursor still decode

### Scenario: Host switch (reconnect)

Given:
- the personal Mac layout with details open

When:
- Cardputer switches to the work Mac

Then:
- details close, hover and Puzzle claims clear, work providers replace the
  personal set (existing 038 behavior, now with four tracked metrics)

Repeated input: Up/Down wrap across the visible rows, as before. Stale
completion: covered by the existing session/generation handling in 038 and not
changed by this plan.

## 7. Architecture Invariants

- Credentials are read only by `ClaudeCredentialReading`; no code path writes,
  refreshes, or logs them.
- Provider JSON never crosses the Companion boundary; only normalized enums
  and numbers are encoded.
- v3/v4 payloads never contain provider `3` or plans `4`/`5`.
- UI hover index and Puzzle focus index come from the same
  `aiUsageVisibleMetrics` order.
- All Puzzle output still goes through `IndicatorService`.

## 8. Tests mapped to scenarios

| Scenario | Test |
| --- | --- |
| Normalization of `limits[]` and legacy fields | `claudeNormalizesLimitsAndLegacyWindows` (CompanionCoreCheck) |
| v5 schema 3 round trip, v4 filter | `claudeSchemaThreeAndOlderVersionFilter` (CompanionCoreCheck) |
| HELLO offers 5,4,3,2 | existing hello-offer check, updated |
| Claude request headers and plan | `claudeBuildsOAuthUsageRequest` (CompanionProvidersCheck) |
| Claude absent | `claudeOmitsAbsentCredential` |
| Token cache, five-minute recheck, sign-out | `claudeCachesTokenAndRechecksKeychain` |
| One-minute request interval, aged cached sample | `claudeThrottlesRequestsAndServesAgedSample` |
| 429 pause with Retry-After and doubling | `claudeRateLimitBacksOffWithoutFailure` |
| Failed recheck keeps the token | `claudeRecheckKeepsTokenWhenKeychainFails` |
| Keychain prompt left open | `claudeKeychainPromptIsUnavailableNotFailure`, `collectorKeepsDiscoveringWhileWaitingForUser` |
| Transport reuse after stop | `httpTransportCanBeReusedAfterStop` |
| Expired token / 401 | `claudeExpiredTokenIsUnavailable` |
| Unavailable keeps prior sample without retry | `collectorKeepsUnavailableSampleStaleWithoutRetry` |
| Keychain denial pauses reads | `claudeDenialPausesKeychainReads` |
| Collector order and wire cap | `collectorPublishesPersonalCodexAndClaude`, CompanionCoreCheck cap checks |
| Stale sample ageing | `collectorShowsElapsedStaleWindowsAsReset`, CompanionCoreCheck `aged` checks |
| Firmware schema 3 decode and v4 rejection of Claude | `test_v5_claude_schema_and_v4_rejects_claude` (test_companion_protocol) |
| Personal screen rows, widest value spacing | `test_personal_mac_renders_codex_and_claude_rows` (test_ai_usage) |
| Four-band Puzzle | `test_four_metrics_use_two_row_bands_with_purple_ends` |
| Hover and Claude details | `test_hover_rows_and_open_claude_limits` |
| `;` / `.` without Fn move hover | `test_plain_arrow_keys_move_hover_without_fn` |
| Hover clears when metrics shrink | `test_hover_clears_when_visible_metrics_shrink` |
| Mixed layout and stale header | `test_mixed_layout_maps_hover_rows_and_stale_header` |
| Empty state names the checked providers | `test_no_accounts_names_every_provider` |
| Claude as the only provider | `test_claude_alone_uses_single_provider_layout_and_details` |
| Host switch | existing `test_expanded_resets_clear_on_real_session_switch` |

## 9. Physical Acceptance

- First Companion run shows one macOS Keychain prompt for
  `Claude Code-credentials`; after "Always Allow" no prompt reappears across
  Claude Code token refreshes (verify over at least one refresh, ~8 hours).
- Personal Mac: four rows fit at 1.20× font; `6D 23H` and `100%` do not touch
  the bar; Puzzle bands are readable at the configured LED brightness.
- Work Mac layout is unchanged.

## 10. Out of Scope

- Claude extra usage / usage credits and any spend display.
- Model-specific weekly limits (Opus, Sonnet) and code-named fields.
- Claude Team/Enterprise specific plans beyond an `ACCOUNT` fallback title.
- Refreshing Claude tokens or reading `~/.claude/.credentials.json`.
- More than two providers per host.
