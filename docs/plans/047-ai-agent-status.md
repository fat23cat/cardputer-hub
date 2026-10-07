# 047 — AI Agent Status Mini App

## Current status

Implemented on **2026-10-05**: `AI_AGENT_STATUS` (operation 13), the
Companion hook helper, receiver, `AgentStatusStore`, per-application installer
and Diagnostics, firmware `AiAgentStatusService`, the AI STATUS Mini App and
its open-only Unit Puzzle view. Firmware unit tests, Companion core checks and
the firmware build pass. The quick hook check on both computers (section 2)
and physical acceptance (section 9) have not been run; the Codex desktop
app's hook loading and workplace hook policy remain unverified.

AI USAGE was changed in the same work to use Unit Puzzle only while it is
open, matching AI STATUS.

Superseded on **2026-10-07**: the Claude Code interruption gap (section 5,
S7) is closed by reading only the transcript tail for Claude's interruption
marker; `docs/ARCHITECTURE.md` owns the current rule.

## 1. Goal

Provide one glanceable **AI STATUS** Mini App showing whether Codex, Claude Code,
and Cursor agents on the selected computer are working, need the user's
attention, or finished.
Each application has a full-width colored plate with its name and a short status.
The user continues working in desktop UI applications, not terminal agents.
No chat count, approval details, transcript, quota, or account analytics appear.

The intended environments are personal Codex Plus and Claude Pro, and workplace
Codex Business and Cursor Enterprise. These describe the user's use cases;
they are not hardcoded host identities or separate provider implementations.

## 2. Scope

### First implementation

- Claude Code (desktop **Code** tab, and the CLI for free), Codex, and Cursor
  Agent Chat on the existing macOS Companion and authenticated selected-host BLE
  transport. Confirm the workplace computer runs macOS before promising support.
- One status row per application on the selected host. Switching hosts replaces
  the entire snapshot; it does not combine personal and workplace computers.
- Automatic background observation on the computer, including while AI STATUS
  is closed. Firmware polls the cached snapshot only while the Mini App is open.
- An optional Unit Puzzle view, shown only while AI STATUS is open, that adapts
  to the number of AI applications with installed hooks (section 5).
- An explicit **Install hooks** / **Remove hooks** action per application in
  Companion, with hook health and the last event time in Companion Diagnostics.

### Quick check before implementation

On each computer, confirm that hooks fire in the real desktop applications
before protocol or firmware work. The reference apps in section 3.1 are the
fastest way: [claude-status-bar](https://github.com/phalla-doll/claude-status-bar)
covers Claude Desktop and Codex, and
[Cursor-Notch](https://github.com/jb-holographik/Cursor-Notch) covers Cursor
Agent Chat. Record the result in section 9, especially:

- whether the Codex desktop app loads `~/.codex/hooks.json` and how hook trust
  is granted there (the CLI uses `/hooks`);
- whether workplace policy disables user hooks (Codex `hooks = false` or
  `allow_managed_hooks_only`; Cursor enterprise or team hooks).

An application whose hooks do not fire is shown as `--` and documented; there
is no fallback such as UI scraping, transcript parsing or process heuristics.

### Implementation order

1. Run the quick check on both computers.
2. Companion: hook helper, local receiver, per-session store and aggregation,
   per-application installer, Diagnostics.
3. Add bounded `AI_AGENT_STATUS` request/response fixtures to the existing
   protocol generator, update its fingerprint, and implement both endpoints.
4. Add `AiAgentStatusService`, register AI STATUS, and implement the plate UI.
5. Add `AiAgentStatusIndicatorController` and the adaptive Unit Puzzle layout.
6. Update owning documentation and setup instructions, run final gates once,
   and perform the physical acceptance matrix.

## 3. Architecture

```text
Desktop application lifecycle hooks
    -> bundled hook helper
    -> local Companion receiver (Unix-domain socket)
    -> AgentStatusStore (CompanionCore)
    -> cached AI_AGENT_STATUS response (CompanionSession)
    -> existing authenticated BLE session (CompanionService)
    -> AiAgentStatusService
    -> AiAgentStatusApp (LCD)
    -> AiAgentStatusIndicatorController -> IndicatorService -> Unit Puzzle
```

Names of new components and the operation above are proposed. Existing
ownership follows [Architecture §7–8](../ARCHITECTURE.md#7-mini-app-model) and
[§35](../ARCHITECTURE.md#35-host-companion-boundary).

**Hook helper.** A small executable bundled with Companion. It reads the hook
JSON, forwards it to a private socket owned by the current user with a short
connect/write timeout, prints the provider's neutral response (`{}` for Codex
and Cursor, `{"continue":true}` for Cursor `beforeSubmitPrompt`, nothing for
Claude Code), and exits 0. If Companion is not running it exits immediately.
It never returns a permission decision.

**Receiver.** Keeps only an allowlist of fields: application, event name,
session/conversation id, turn/generation id, status/reason/notification type,
tool name, and the hook process's owner pid. Everything else (prompts, paths,
commands, output, e-mail) is discarded on receipt and never logged.

**Installer.** On explicit user action, merges only Companion's own entries into
the user-level configuration (`~/.claude/settings.json`,
`$CODEX_HOME/hooks.json`, `~/.cursor/hooks.json`), keeps a backup, writes
atomically through symlinks, is idempotent, and removes only its own entries.
It refuses files that are not plain JSON instead of rewriting them.

**BLE snapshot.** Three application identifiers with a status value each; no
chat identifiers, counts, prompts, paths, e-mail, credentials, or transcript
data. Reuse the selected-host Companion session and operation-filtered
completions; a malformed status response fails only that telemetry request.
Polling follows `MacStatusService`: one-second foreground polling, one request
outstanding, a three-second delivery freshness threshold on monotonic time, and
session-epoch rejection of obsolete completions. Extract a shared helper if
implementation would otherwise copy that logic a third time alongside
`AiUsageService`.

### 3.1 Reference implementations

Reviewed on 2026-10-05. Borrow ideas and, where the license allows, code.

| Project | License | Borrow |
| --- | --- | --- |
| [claude-status-bar](https://github.com/phalla-doll/claude-status-bar) | MIT | Claude Code and Codex event mapping; Desktop uses `PermissionRequest`, the CLI `Notification`; owner-pid liveness with `kill(pid, 0)`; age caps for frozen sessions; merge-own-entries installer |
| [coucou](https://github.com/louis-cfm/coucou) | MIT | Codex `hooks.json` installer; Unix-socket hook relay that never blocks the agent |
| [Cursor-Notch](https://github.com/jb-holographik/Cursor-Notch) | none stated (read only) | Cursor Agent Chat mapping; debounce `stop`, which can fire between model turns |
| [claude-monitor](https://github.com/icefoxj/claude-monitor) | CC BY 4.0 | ESP32 device: priority aggregation, host-lost presentation |

Not borrowed: transcript parsing for "interrupted by user", process-tree
inference of approvals, and approving or answering from the device.

## 4. Ownership & Boundaries

| Component | Owns | May call | Must not call / own |
| --- | --- | --- | --- |
| Hook helper | Forwarding one hook event and the neutral response | Private Companion socket | BLE, UI, approval decisions, content logging |
| Receiver and installer | Field allowlist, provider event mapping, user-level hook configuration | `AgentStatusStore`, provider config files on user action | Firmware, display rendering, credentials |
| `AgentStatusStore` in CompanionCore | Per-session state, liveness, age caps, per-application aggregation | Injected clock and pid-liveness probe | AppKit, sockets, BLE |
| `CompanionSession` | Encode cached status response for a live protocol session | Injected status snapshot boundary | Provider collection, UI state |
| `CompanionService` | Authentication/session state, request correlation and filtered completions | Existing transport and `HostService` | Provider hooks, aggregation, Mini App methods |
| `AiAgentStatusService` | Foreground poll lifecycle, freshness, firmware snapshot and last known states | `CompanionService` and injected clock | Desktop details, rendering, hardware adapters |
| `AiAgentStatusIndicatorController` | Adaptive Puzzle layout, matrix colors and its `IndicatorClaim` while the app is open | Service snapshot, `IndicatorService` claims | `CompanionService`, LED hardware, `AiUsageIndicatorController` |
| `AiAgentStatusApp` | Plate layout, state surfaces and close input | Service monitoring operations, snapshot, shared Action infrastructure | `CompanionService`, sockets, service `update()` |
| Firmware composition / `app_main` | Inject dependencies and update services once per loop | Service lifecycle | Provider behavior and UI rendering |

## 5. State Model

### User-visible states

The existing [UI palette](../UI_REQUIREMENTS.md#3-color-system) remains
authoritative and each plate uses a token in its documented role as a state
surface. Do not introduce a yellow token or reuse Ordinal (row numbers) as a
status color.

| State | Label | Plate | Meaning |
| --- | --- | --- | --- |
| `Working` | `WORKING` | Blue `#1B4FD0`, Bone text | An agent is processing and no session needs the user |
| `NeedsYou` | `NEEDS YOU` | Vermilion `#E2451E`, Bone text | A permission or question wait, or an execution error |
| `Finished` | `DONE` | Leaf `#4CB949`, Ink text | The latest work ended or was stopped by the user within ten minutes, and nothing is active |
| `FinishedEarlier` | `DONE` | Light neutral `#CED0CB`, Ink text | The same, with the latest finish more than ten minutes old |
| `Unknown` | `--` | Pale `#DEDBD1`, Ink text | No live session, hooks not firing, or stale delivery |

`DONE` means the agent is no longer working and nothing needs the user, not
that the project succeeded. A user's own interruption is never `NEEDS YOU`.
Timeouts never produce `NEEDS YOU` or `DONE`; an expired session simply drops
out of aggregation. Companion reports done earlier once the latest finish of
that application is ten minutes old, because only the Mac knows when it
finished; the change only settles the color.

When BLE delivery is stale, all plates are presented as `--`.

AI STATUS has no header. It shows one full-width plate per application whose
hooks Companion has installed, in the order CODEX, CLAUDE, CURSOR, sharing the
240×135 display equally with a 2-pixel Bone line between plates;
AI_AGENT_STATUS carries only those applications. Names and labels use
double-size text. Before the first answer the screen reads `CHECKING AI`, and
with no hooks installed it reads `NO AI HOOKS` and `INSTALL IN COMPANION`. No
selectable chat list, details page, counts, refresh button, transition
animation or persistent Escape hint. Escape uses shared close handling. Only a
plate whose state changes is redrawn, and settled screens draw nothing
(UI Requirements §6 and §10). Labels carry the same meaning as color.

### Event mapping

Per session, the latest mapped event sets the state. Events not listed leave it
unchanged.

| Application | `Working` | `NeedsYou` | `Finished` | Session ends |
| --- | --- | --- | --- | --- |
| Claude Code | `UserPromptSubmit`, `PreToolUse` (not `AskUserQuestion`), `PostToolUse`, `PostToolUseFailure` | `PermissionRequest` unanswered for 15 s; `Notification` with `permission_prompt` or `elicitation_dialog`; `PreToolUse` for `AskUserQuestion`; `StopFailure` | `Stop` | `SessionEnd`; owner process exit |
| Codex | `UserPromptSubmit`, `PreToolUse`, `PostToolUse` | `PermissionRequest` unanswered for 15 s | `Stop`, `Interrupt` | `SessionEnd`; owner process exit |
| Cursor Agent Chat | `beforeSubmitPrompt`, `postToolUse`, `postToolUseFailure` | `stop` with `error` | `stop` with `completed` or `aborted`, after a short debounce | `sessionEnd` |

A `PermissionRequest` also fires when auto-review (Codex
`approvals_reviewer = "auto_review"`), a policy or another hook answers without
the user, so it becomes `NeedsYou` only if no other event of that session
follows within 15 seconds. Real Claude Code waits also send `Notification`
`permission_prompt` after about six seconds, which counts at once.

Known gaps, documented rather than worked around:

- Claude Code sends no hook when the user interrupts a run or denies a
  permission, so that session keeps its last state until an age cap expires.
- Cursor exposes no observe-only approval-wait event; a waiting Cursor agent
  shows `WORKING`.

A prompt-start event establishes the current turn or generation ID. Activity,
waits and stops carrying a different ID are ignored, so a late tool result
cannot restore an earlier turn or clear the current wait. When Companion has
not observed a prompt (for example after restart), the first identified
activity establishes the turn. A new prompt without an ID clears the old ID.

### Aggregation and expiry

For each application: any `NeedsYou` session gives `NEEDS YOU`; otherwise any
`Working` session gives `WORKING`; otherwise a `Finished` session gives `DONE`,
or done earlier when the latest finish is ten minutes old; otherwise `--`.

A session leaves aggregation when its provider reports the session end, its
owner process exits, or it ages out with no events: `Working` after 15 minutes
and `NeedsYou` after 2 hours (proposed, tuned during acceptance). `Finished`
sessions stay until they end or the owner process exits. Companion keeps no
state across restarts; after a restart rows are `--` until new events arrive.
Device disconnect clears firmware data, not the computer's store.

### Unit Puzzle

The controller follows `AiUsageIndicatorController`
([Architecture §28–29](../ARCHITECTURE.md#28-rgb-indicator)): it writes only
through `IndicatorService` and holds one `ForegroundApplication` claim only
between `AiAgentStatusApp` activation and deactivation, like AI USAGE. Closing
the Mini App releases the matrix to Pomodoro or any other owner.

The matrix shows the applications whose hooks are installed, in the fixed
order CODEX, CLAUDE, CURSOR, and recomputes the layout on every snapshot. An
installed application with state `--` is a dim neutral band:

| Installed | Matrix |
| --- | --- |
| 0 | No frame; the claim is released |
| 1 | The whole 8×8 in that application's state color, without markers |
| 2 | Rows 0–3 and 4–7, one application each |
| 3 | Rows 0–2, 3–5 and 6–7 |

Bands follow the screen rows without a gap. When the matrix is split, each
band's first and last pixel is the AI USAGE purple marker, so neighbouring
applications in the same state stay distinguishable.

Colors are matrix-specific muted tones, as for Pomodoro: the existing muted
steel blue for `WORKING`, the existing sage for `DONE`, a dimmer green for done
earlier, and a new muted vermilion for `NEEDS YOU`. All pass the shared 1%–10%
brightness limit. There is no animation. When delivery is stale, the frame is cleared instead of
showing old colors. Unit NFC shares the Grove pins; while it owns them the
Puzzle stays off as today.

### Deferred

Separate `ERROR`, `PAUSED` and `STOPPED` labels are not part of the first
delivery.

## 6. BDD Scenarios

Each scenario describes one behavior. "Row" means the application's row on the
open AI STATUS screen with installed hooks and fresh delivery, unless stated
otherwise.

### S1 — Prompt starts work

Given an idle row, when the user sends a prompt, then the row shows `WORKING`.

### S2 — Normal finish

Given one working session, when it finishes normally, then the row shows `DONE`.

### S3 — Permission or question wait

Given a working session, when the agent waits for approval or an answer, then the
row shows `NEEDS YOU`.

### S4 — Answer resumes work

Given a `NEEDS YOU` row caused by a wait, when the next tool or prompt event
arrives, then the row shows `WORKING`.

### S5 — Execution error

Given a working session, when the provider reports an execution error, then the
row shows `NEEDS YOU`, never `DONE`.

### S6 — Reported user interruption

Given a working Codex or Cursor session, when the user stops it, then the row
shows `DONE`.

### S7 — Unreported interruption expires

Given a Claude Code session left `WORKING` by an unreported interruption, when
15 minutes pass without events, then the session leaves aggregation and the row
reflects the remaining sessions or `--`.

### S8 — Wait during concurrent work

Given one working chat, when another chat of the same application starts
waiting, then the row shows `NEEDS YOU`.

### S9 — One of two chats finishes

Given two working chats, when one finishes, then the row stays `WORKING`.

### S10 — Cursor stop between turns

Given a working Cursor session, when `stop` arrives and new activity follows
within the debounce, then the row never shows `DONE`.

### S11 — Session ends

Given a session in any state, when its provider reports the session end or its
owner process exits, then it leaves aggregation.

### S12 — Old stop ignored

Given a session working on a new turn, when a stop for an earlier turn arrives,
then the row stays `WORKING`.

### S13 — Only installed applications have rows

Given Companion has installed hooks for some applications, when AI STATUS
receives an answer, then only those applications have rows, an installed one
without events shows `--`, and with none installed the screen reads
`NO AI HOOKS`.

### S14 — Companion capability lost

Given AI STATUS is open, when the COMPANION capability disappears, then the Mini
App deactivates and Launcher becomes visible under existing availability rules.

### S15 — Delivery becomes stale

Given a presented snapshot, when a poll fails or the last valid answer is older
than three seconds, then all plates are presented as `--`.

### S16 — Old finish settles

Given a `DONE` application, when its latest finish becomes ten minutes old, then
its plate turns Light neutral and still reads `DONE`.

### S17 — Obsolete completion

Given an outstanding poll, when its completion arrives after stop, host change or
session reset, then it is ignored.

### S18 — Bad status payload

Given a healthy Companion session, when a malformed status response arrives,
then only that request fails.

### S19 — Host change clears status

Given status from host A, when A disconnects or host B is selected, then A's
snapshot is cleared.

### S20 — Fetch after reconnect

Given a cleared snapshot, when a compatible session to the selected host becomes
ready, then the service fetches that host's current snapshot without reopening
the app.

### S21 — Companion restart

Given live sessions, when Companion restarts, then rows show `--` until new
events arrive.

### S22 — Unchanged polls

Given a settled snapshot, when identical polls arrive, then nothing animates and
no animation frames are requested.

### S23 — Repeated close input

Given AI STATUS is open, when close input repeats, then exactly one shared close
occurs.

### S24 — Companion not running

Given Companion is not running, when a hook fires, then the helper exits
immediately with the neutral response and the agent continues unaffected.

### S25 — No content leaves the receiver

Given hook input containing prompts, paths or commands, when it is received and
served, then none of that content appears in logs or BLE payloads.

### S26 — Monitoring while closed

Given AI STATUS is closed, when a session starts or finishes, then the store
updates without firmware polling, and opening AI STATUS shows the current state
after the first poll.

### S27 — Puzzle with no installed application

Given AI STATUS is open and no hooks are installed, when a snapshot arrives,
then the matrix shows no AI STATUS frame.

### S28 — Puzzle with one installed application

Given AI STATUS is open and only Codex is installed, when it is `WORKING`, then
the whole matrix shows the working color.

### S29 — Puzzle with two installed applications

Given AI STATUS is open with Codex and Claude installed, when a snapshot arrives,
then Codex fills rows 0–3 and Claude rows 4–7, each band marked purple at both ends.

### S30 — Second application installed

Given one application fills the matrix, when hooks for a second application
are installed, then the next frame switches to the two-zone layout.

### S31 — Closing AI STATUS releases the Puzzle

Given AI STATUS shows a frame over Pomodoro, when the Mini App closes, then the
claim is released and Pomodoro is visible again.

### S32 — Puzzle cleared on stale delivery

Given AI STATUS shows a frame, when delivery becomes stale or the Companion
session ends, then the frame is cleared.

## 7. Architecture Invariants

- Mini Apps depend on Services; provider integrations remain on the computer.
- `app_main` updates `AiAgentStatusService` exactly once per loop.
- Only `AiAgentStatusService` consumes `AI_AGENT_STATUS` completions in firmware.
- Closing AI STATUS stops its polls and discards pending presentation completions.
- One foreground status request is outstanding; session epochs guard responses.
- The store and firmware service are host-testable with injected clocks.
- AI STATUS does not call `AiUsageService` or infer activity from quota usage.
- Provider subscriptions and personal machines are never special-case identities.
- Hooks never return a decision and never block or slow agent work noticeably.
- Provider configuration changes only on explicit user action, with a backup.
- A wait or error is never hidden by another session's activity.
- Timeouts never produce `NEEDS YOU` or `DONE`.
- Protocol fields, sizes and fingerprint come from the existing fixture generator.
- No provider content or identities beyond application names cross to firmware.
- Settled status rows are stationary and request no animation frames.
- Only `IndicatorService` writes Unit Puzzle; AI STATUS holds a claim only
  while its Mini App is open and never shows stale or another host's colors.
- Companion loss affects this Mini App through existing capability handling;
  unrelated firmware and HID continue operating.

## 8. Tests mapped to scenarios

Use focused RED → GREEN → REFACTOR. Event fixtures follow the documented hook
payloads; the physical check confirms the real applications. Test names are
planned, not existing tests.

| Scenario | Test |
| --- | --- |
| S1–S12 | `agentStatusChecks` in `CompanionCoreCheck`: one named check per scenario (prompt, finish, wait, resume, error, interrupt, expiry, concurrent wait and finish, Cursor debounce, session end and owner exit, old-turn stop) |
| S13 | `test_checking_until_first_answer`, `test_only_installed_applications_have_rows`, `test_no_hooks_installed_says_so`; `agentStatusChecks`: report lists installed applications |
| S14 | Existing `MiniAppRuntime` capability-loss tests; AI STATUS is registered with the `COMPANION` capability |
| S15 | `test_delivery_stale_after_three_seconds`, `test_stale_delivery_presents_rows_as_unknown` |
| S16 | `test_done_earlier_is_a_settled_plate`; `agentStatusChecks`: an old finish becomes done earlier |
| S17 | `test_obsolete_status_completion_ignored` |
| S18 | `test_bad_status_payload_isolated`, `test_agent_status_round_trip_and_rejects_bad_pairs` |
| S19, S20 | `test_host_switch_clears_and_reconnect_fetches_current_snapshot` |
| S21 | `agentStatusChecks`: a new store starts empty |
| S22 | `test_unchanged_snapshot_does_not_redraw`, `test_state_change_redraws_one_plate_and_settles`, `test_plates_fill_the_screen_without_header` |
| S23 | `test_repeated_close_is_safe` |
| S24 | Manual: the helper prints the neutral answer and exits 0 with no listener (measured below 10 ms after the first run) |
| S25 | `agentStatusChecks`: hook wire carries no prompt, path, command or e-mail |
| S26 | `test_closed_app_polls_nothing`; the store needs no firmware poll |
| S27 | `test_puzzle_empty_with_no_installed_app`, `test_puzzle_shows_installed_app_without_state_dimly` |
| S28 | `test_puzzle_single_app_fills_matrix` |
| S29 | `test_puzzle_two_apps_use_split_layout`, `test_puzzle_bands_have_end_markers_only_when_split` |
| S30 | `test_puzzle_layout_follows_running_count` |
| S31 | `test_closing_ai_status_releases_puzzle` |
| S32 | `test_puzzle_cleared_when_stale` |
| Installer | `agentStatusChecks`: install, reinstall without duplicates, removal keeping other hooks, Cursor format, Codex timeouts, unexpected shape refused |
| Ownership | Existing architecture checks plus focused dependency/lifecycle checks for new components |

Keep compiler caches. During implementation run only affected tests. After the
last material implementation change run `make check` and, for macOS Companion
changes, `make companion-check` once. Visual captures verify geometry, labels,
colors and plate layouts for one, two and three applications.
Do not mark this plan complete with required failures or untested provider claims.

Update Architecture §35/service ownership, UI Requirements' current-firmware
boundary, `companion/macos/README.md`, protocol documentation, and device/setup
manuals when implementation delivers the corresponding contracts, including
hook installation, policy restrictions, `DONE`/`NEEDS YOU` semantics and the
known gaps. Do not update manuals now to present planned behavior as supported.

## 9. Physical Acceptance

### Quick check results

| Installation | OS / app version | Hooks fire | Wait seen | Notes | Result |
| --- | --- | --- | --- | --- | --- |
| Personal Claude Pro, Desktop Code tab | Pending | Pending | Pending | Pending | Not verified |
| Personal Codex Plus app | Pending | Pending | Pending | Hook trust in the app? | Not verified |
| Workplace Codex Business app | Pending | Pending | Pending | Policy? | Not verified |
| Workplace Cursor Enterprise Agent Chat | Pending | Pending | n/a (known gap) | Policy? | Not verified |

These accounts are acceptance environments; implementation does not branch on
their names.

### Device and end-to-end

- One, two and three plates fill 240×135 with double-size text; `NEEDS YOU`
  does not clip.
- Plates distinguish `WORKING`, `NEEDS YOU`, `DONE`, an old `DONE` and `--` at
  normal and 10% brightness; labels suffice when colors cannot be told apart.
- Visible update within about two seconds of a hook event on a healthy link.
- Two chats, a permission wait, an error and a user stop each show the expected
  row; a wait in one chat stays visible while another works.
- BLE loss, lid close/wake, Companion restart and host switching clear stale data
  and recover without mixing computers or manual re-pairing.
- Unchanged polling does not flicker; keyboard and shared
  display dim/off/wake behavior remain correct.
- Install and remove hooks leave unrelated user hooks intact.
- Unit Puzzle: one, two and zero installed applications switch layouts
  automatically; colors are distinguishable at 1% and 10% brightness; closing
  AI STATUS returns the matrix to Pomodoro.
- Updating firmware and Companion together passes fingerprint compatibility;
  mismatched builds retain the existing update advice.

## 10. Out of Scope

- Ordinary Claude chat/Cowork, Cursor Tab completions, and CLI-only workflows
  beyond what the same hooks provide.
- Cloud/remote agents whose hooks run away from the selected computer.
- Windows/Linux Companion ports and simultaneous two-computer aggregation.
- Chat counts, titles, progress percentages, approval UI, transcripts and history.
- Agent start/stop/resume or approval controls from Cardputer, usage and billing.
- Public HTTP receivers, VPS relays, new Wi-Fi/USB transports or API-created runs.
- Accessibility scraping, screenshots/OCR, transcript or private database
  parsing, process-load inference, and prompts instructing models to report status.
- Continuous dot or matrix animation, sound, the Cardputer RGB LED, and display wake.
- Silent changes to provider configuration or bypass of enterprise policy.
