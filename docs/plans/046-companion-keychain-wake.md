# 046 — Companion Keychain Access After Wake

Status: **Software implemented; local Companion checks pass; physical acceptance pending**

## Current status

Implemented on 2026-10-04. Companion now permits Claude authorization only on
the first credential read after launch. All later reads are noninteractive,
including wake, periodic rechecks and expired-token recovery. Bundle packaging
signs and strictly verifies the completed app.

Verification: `make companion-check`, 763 native tests, `make firmware-check`,
and `git diff --check` passed. Regression tests were observed failing before
the policy and synchronization fixes, then passed. An isolated process also
confirmed that the actual legacy Keychain interaction switch is disabled and
restored without reading credentials.

The local `make check` attempt stopped on two unchanged CRUB upload tests:
the sibling firmware-manager now has a shared `extra` slot rather than the
`hub` partition those tests expect. Its layout was not modified. Physical
lid-close/wake acceptance remains pending.

## 1. Goal

Prevent repeated Claude credential authorization dialogs when the Mac wakes,
while retaining usage data and the initial authorization path.

## 2. Scope

Claude credential read policy, coordination with Cursor's Keychain reads,
completed-bundle signing, regression tests, and corresponding documentation.

## 3. Architecture

Keep authentication in macOS `CompanionProviders`. `ClaudeUsageProvider`
selects interactive versus silent reads. `LegacyKeychainRead` owns the
process-wide legacy interaction switch required by file-based Keychain ACLs.
Both Claude and Cursor use its shared instance; the BLE protocol is unchanged.

## 4. Ownership & Boundaries

| Component | Owns | May call | Must not call |
| --- | --- | --- | --- |
| `ClaudeUsageProvider` | Read policy, token cache and retry timing | Credential reader and HTTP boundary | Keychain mutation or firmware UI |
| `KeychainClaudeCredentials` | Credential decoding and status mapping | `LegacyKeychainRead` | Credential writes or token refresh |
| `LegacyKeychainRead` | Concurrent-read coordination and scoped interaction policy | Security framework | HTTP or provider state |
| Packaging script | Final bundle metadata and signature verification | Swift build and codesign | User credential or partition changes |

## 5. State Model

The first read may prompt. A denial hides Claude for an hour; subsequent reads
are silent. Rechecks preserve a valid cached token if authorization becomes
unavailable. Without a valid token, unavailable authorization retains stale
usage data and retries after five minutes. Relaunch permits authorization again.

Interactive reads can coexist. A silent read waits for active interactive
reads, exclusively disables interaction, reads, and restores the prior policy
before admitting another read. Failure to disable interaction prevents the read.

## 6. BDD Scenarios

### Scenario: Wake with cached credentials

Given a successful initial authorization and an unexpired cached token,
when waking after five minutes requires authorization for a recheck,
then no dialog opens and usage requests continue with the cached token.

### Scenario: Expired token and unavailable authorization

Given an expired cached token and a Keychain item requiring authorization,
when periodic or wake refreshes run repeatedly,
then no expired token is sent, no dialog opens, and quiet reads resume after
five minutes and recover when the renewed credential becomes accessible.

### Scenario: Other providers during initial authorization

Given Claude's first authorization is in flight,
when Cursor reads its credential,
then its read can proceed independently; a silent read cannot change the
process-wide policy while either interactive read is active.

### Scenario: Failed silent access

Given silent reading is requested,
when disabling interaction fails or the credential read fails,
then no potentially interactive read occurs on disable failure, and the prior
policy is restored after a completed read. A silent authorization failure is
not treated as the user declining the initial prompt.

### Scenario: Completed app bundle

Given packaging has written all bundle metadata,
when it signs the completed app,
then strict verification succeeds with identifier `org.cardputer.companion`.

Reconnect uses the same wake/read policy. BLE reconnect and stale-completion
handling are unchanged; existing checks retain coverage of delayed initial
credential reads and collector refresh coalescing.

## 7. Architecture Invariants

- Tokens stay in memory on the Mac and never enter the BLE protocol.
- Companion never writes or renews Claude Code's credential.
- All production legacy credential reads use the shared coordinator.
- Only the first Claude credential read after launch may prompt.
- A valid cached token survives unavailable background authorization.
- Packaging signs after metadata changes and rejects an invalid signature.

## 8. Tests mapped to scenarios

| Scenario | Check |
| --- | --- |
| Wake with cached credentials | `claudeWakeRecheckDoesNotPromptOrLoseWorkingToken` |
| Expired token and quiet recovery | `claudeExpiredTokenWaitsQuietlyForKeychainAccess` |
| Independent interactive reads | `legacyKeychainCoordinatesConcurrentProviderReads` |
| Failed access and policy restoration | `legacyKeychainSilentReadFailsClosed`, `legacyKeychainSilentReadRestoresInteractionOnFailure` |
| Silent authorization status | `claudeSilentAuthorizationFailureIsNotUserDenial` |
| Initial denial and delayed completion | `claudeDenialPausesKeychainReads`, `claudeKeychainPromptIsUnavailableNotFailure` |
| Bundle signature | `make companion-check` packaging verification |

## 9. Physical Acceptance

Pending: launch the packaged app, authorize Claude if requested, close the lid
for more than five minutes, reopen, and confirm no new Companion authorization
dialog appears and AI USAGE recovers. Repeat with an expired token and confirm
stale data without recurring dialogs. Relaunch to verify authorization recovery.

## 10. Out of Scope

Changing Claude Code's credential ownership or ACLs, creating signing
certificates, firmware/protocol changes, and migrating the sibling CRUB layout.
Default ad-hoc signing remains specific to each binary; an installed certificate
identity can be selected for stable identity across builds.
