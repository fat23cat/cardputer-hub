# 048 — Combined AI application

## Current status

Software implemented. Local `make check` passed: 799 C++ tests, 100 Python
tests, static and format checks, and the production ESP-IDF build. Regression
tests reproduced the frame, page-motion, stale no-hooks, and header-redraw
issues before their fixes. Physical Cardputer-Adv acceptance remains pending.

This records the user-approved scope and delivered implementation. The owning
contracts remain in Architecture §7, UI Requirements, and the device manual.

## 1. Goal

Combine AI STATUS and AI USAGE into one AI Launcher entry while preserving
their account fields, agent states, usage details, and Unit Puzzle behavior.

## 2. Scope

STATUS and USAGE share a 16-pixel navigation strip. Left/Right cycle between
them with the common slide in the pressed direction. First entry opens STATUS;
subsequent entries remember the last page within the firmware session.
USAGE displays a fresh agent-attention mark. Details retain the full screen;
Escape returns from details to USAGE before using the shared app exit.

## 3. Architecture

`AiApp` composes the existing presentations and their injected Services. Its
private `ContentDisplay` fits vertical coordinates into the 119-pixel body
without shrinking text. Details use 240x135. The shell retains ownership of
the display frame; page changes request a transition before drawing.

## 4. Ownership & Boundaries

| Component | Owns | May call | Must not call |
| --- | --- | --- | --- |
| `app_main` | Service update lifecycle and registration | Service updates, shell update | AI rendering |
| `ApplicationShell` | Tick frame, app entrance/exit, capability gate | Runtime and display | AI Service polling |
| `AiApp` | Page selection, strip, monitoring activation | View lifecycle, monitoring start/stop, transition request | Service updates, real frame begin/end |
| Content views | Existing layout and local interaction | Injected Services and viewport | Other view lifecycle |
| `ContentDisplay` | Content coordinate mapping | Display drawing | Real frame begin/end |

## 5. State Model

AI is inactive, STATUS, or USAGE; USAGE has Main, LIMITS, and RESETS states.
Main pages have the navigation strip. Each page owns its Puzzle controller
only while visible; agent monitoring spans the entire AI activation.
Fresh agent data may set the attention mark. Stale data removes it and renders
unknown plate states while retaining the known installed set, including zero
hooks. An empty session renders CHECKING AI. Closing releases monitoring and
controller claims. Session replacement cannot reuse the previous host's data.

## 6. BDD Scenarios

1. **Open and switch:** Given Companion is ready, when AI opens and Right is
   pressed, then STATUS opens first and USAGE slides in with all quota fields.
   Closing and reopening retains USAGE.
2. **Details:** Given rolling-window usage, when Enter and local detail arrows
   are pressed, then LIMITS/RESETS keep their existing content; Escape returns
   to USAGE, and another Escape closes AI.
3. **Unavailable dependency / in-flight failure:** Given AI requires Companion,
   when the capability disappears, then the shared runtime deactivates AI and
   the shell returns to Launcher without forwarding input to the closed app.
4. **Reconnect:** Given a previous host had quotas and NEEDS YOU, when the
   session disappears and a new session opens, then old quota and attention
   are absent until new answers arrive. Reconnect does not reopen AI itself.
5. **Repeated input:** Given the shell has an active frame, when Right is
   followed by Escape in the same tick, then both transition requests remain
   valid. Repeated Left/Right cycles follow the pressed direction.
6. **Stale delivery:** Given the latest answer installed zero hooks, when it
   becomes stale while USAGE is visible and STATUS is reopened, then NO AI
   HOOKS remains visible. Late completions are filtered by the existing
   session-scoped Services; this change adds no request/completion protocol.
7. **Idle and refresh:** Given USAGE is visible, when only agent attention or
   quota data changes, then only the relevant content is redrawn. Unchanged
   attention preserves the strip during quota refreshes; settled ticks draw
   nothing.

## 7. Architecture Invariants

- One `ai` registry entry requires `COMPANION`.
- Service updates remain outside Mini Apps.
- The shell's frame stays active across every input event in its tick.
- Content view frame calls are no-ops through the viewport.
- Monitoring continues across page and detail changes.
- Only the visible view owns Puzzle claims.
- Text size and horizontal geometry preserve the existing fields.

## 8. Tests mapped to scenarios

| Scenario | Test / verification |
| --- | --- |
| Open and switch | `test_switch_pages_preserves_work_quota_fields_and_puzzle_ownership`; `test_activation_leaves_the_shell_frame_open_for_its_transition` |
| Details | `test_detail_arrows_are_local_and_escape_returns_to_usage`; `test_escape_returns_from_usage_details_without_closing` |
| Dependency loss | Existing `test_required_capability_loss_deactivates_before_update`; `test_capability_loss_returns_shell_to_launcher_without_forwarding_input`; AI registration inspection |
| Session replacement | `test_new_companion_never_reuses_previous_host_quota_or_attention` |
| Repeated input and direction | `test_page_switch_keeps_outer_frame_open_for_escape_in_the_same_tick`; `test_page_slides_follow_key_direction_in_both_cyclic_directions` |
| Stale delivery | `test_stale_no_hooks_answer_survives_leaving_and_returning_to_status`; existing Service completion filtering |
| Idle and refresh | `test_hidden_status_updates_attention_without_redrawing_quota`; `test_usage_refresh_preserves_header_when_attention_is_unchanged` |
| Modified keys | `test_modified_horizontal_keys_do_not_switch_pages` |

## 9. Physical Acceptance

Pending: inspect STATUS with one to three agents, Business/Enterprise quota
rows, rolling-window rows and details for readability on Cardputer-Adv;
exercise repeated Left/Right and Escape during slides; verify NEEDS YOU and
Puzzle controller handoff; disconnect and reconnect Companion.

## 10. Out of Scope

Companion protocol or provider changes, new quota metrics, persistent page
selection, hook lifecycle changes, old-icon cleanup, and firmware deployment.
