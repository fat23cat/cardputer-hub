# Action Bus Foundation Plan

Status: **Complete**

This plan describes the next small, hardware-independent Phase 1 change after
the System Core foundation.

Implementation should use:

```text
branch:   feat/003-action-bus-foundation
PR title: [003] Establish Action Bus foundation
```

## 1. Objective

Add:

* an extensible logical `Action` model;
* synchronous registration and dispatch through `ActionBus`;
* explicit registration and dispatch outcomes;
* native behavioral tests;
* a maintained user-manual directory and documentation update requirements.

Do not yet connect keyboard input to Actions or add concrete Service actions.
Firmware behavior and the plan 002 boot screen remain unchanged.

## 2. Public Contracts

Add `src/core/actions/` with:

* `ActionValue`: `bool`, `std::int32_t`, or owned `std::string`;
* `ActionParameter`: owned name and typed value;
* `Action`: owned action ID, source ID, and ordered parameters;
* `Action::findParameter(name)`: exact, case-sensitive lookup without type
  coercion;
* `IActionHandler::handle(const Action&)`: returns `Handled` or `Rejected`;
* `ActionBus::registerHandler(actionId, handler)`: returns `Registered`,
  `InvalidId`, or `DuplicateId`;
* `ActionBus::dispatch(action)`: returns `Handled`, `Rejected`, `Invalid`, or
  `Unsupported`.

Contract rules:

* Action and source IDs are non-empty, exact, case-sensitive strings.
  Namespaced IDs such as `host.select` and `input.keyboard` are the convention.
* Parameter names must be non-empty and unique.
* Each action ID has exactly one handler. Duplicate registration leaves the
  original handler intact.
* Dispatch validates the Action before invoking a handler.
* The handler receives the original immutable Action, including its source and
  parameter types.
* Routing depends only on the action ID, so identical Actions from different
  sources reach the same handler.
* The bus copies registration IDs but holds non-owning handler references.
  Handlers must outlive the bus.
* Use deterministic linear registration lookup, suitable for the initially
  small action set.
* Dispatch is synchronous and single-threaded. Unregistration, broadcasting,
  reentrant registration, and concurrency are deferred.

## 3. Implementation Sequence

Follow RED-GREEN-REFACTOR separately for each behavior:

1. Add Action model tests covering ownership, typed parameter lookup, missing
   parameters, and exact matching. Then implement the minimum model.
2. Add registration tests. Then implement non-empty ID validation and duplicate
   rejection.
3. Add dispatch tests. Then implement successful routing, handler rejection,
   invalid-Action rejection, and unsupported-Action handling.
4. Add source-independence coverage proving keyboard and Mini App source IDs do
   not alter routing.
5. Refactor shared test fakes only after the focused suite is green.
6. Update the Action Model and Action Bus sections of
   `docs/ARCHITECTURE.md` with the stable generic contract and one-handler
   synchronous semantics.
7. Add `docs/manuals/` with an index, installation guide, and honest current
   device/controls guide. Update `AGENTS.md` and `docs/ENGINEERING.md` so future
   user-facing changes maintain these manuals.
8. Complete this plan with the observed RED failures, delivered behavior, and
   actual verification results.

No changes should be made to `SystemRuntime`, `main.cpp`, hardware adapters, or
dependency versions unless compilation exposes a necessary integration issue.

## 4. Test Plan

Add `test/test_action_bus/test_main.cpp` covering:

* owned IDs, sources, and string parameters remain valid after caller values
  change;
* boolean, integer, and string parameters retain their types;
* missing parameters are distinguishable from present values;
* a registered handler receives the Action unchanged and exactly once;
* handler rejection is propagated;
* unsupported Actions invoke no handler;
* empty IDs or sources and empty or duplicate parameter names are invalid;
* duplicate registration is rejected without replacing the first handler;
* action IDs are matched exactly and case-sensitively;
* different source IDs produce identical routing behavior.

Run and record:

```text
uv run --frozen pio test -e native -f test_action_bus
make format
make format-check
make lint
make test
make build
make check
```

## 5. Acceptance Criteria

The change is complete only when:

* every registration and dispatch result has native behavioral coverage;
* registered handlers receive Actions unchanged and exactly once;
* invalid, duplicate, rejected, and unsupported cases follow the documented
  contract;
* Cardputer-Adv firmware compilation succeeds;
* firmware behavior remains unchanged;
* no Phase 2 connectivity, Service, Mini App, or shell behavior is introduced;
* architecture documentation and this plan's completion record reflect the
  delivered contract;
* user manuals describe installation, current supported features, controls,
  key combinations, and limitations without presenting planned behavior as
  available;
* agent and engineering instructions require relevant manual updates;
* every applicable repository check passes.

## 6. Assumptions and Deferred Work

* Plan 002 is merged, and implementation begins from an updated `main`.
* Existing C++17 and pinned toolchain settings remain unchanged.
* Action IDs and parameter schemas will be declared by their future owning
  Services, not centrally hardcoded in System Core.
* Keyboard shortcut mapping, input routing, global controls, Action logging,
  concrete handlers, asynchronous execution, cancellation, serialization, and
  authorization are deferred.
* No physical-device validation is required because this change has no
  hardware-facing behavior.

## 7. Completion Record

Completed on 2026-08-31 on `feat/003-action-bus-foundation`, stacked on
`feat/002-system-core-foundation`.

The RED phases produced the expected failures:

1. the Action model suite failed because `core/actions/action.h` did not exist;
2. the registration suite failed because `core/actions/action_bus.h` did not
   exist;
3. the dispatch suite failed because `DispatchResult` and
   `ActionBus::dispatch()` did not exist.

Delivered behavior:

* owned, extensible Action and source IDs with typed named parameters;
* exact parameter lookup without type coercion;
* deterministic one-handler registration with invalid and duplicate outcomes;
* synchronous validation and dispatch with handled, rejected, invalid, and
  unsupported outcomes;
* source-independent routing that preserves the original immutable Action;
* maintained installation and device manuals with current controls, supported
  features, and limitations;
* repository instructions requiring manuals to remain synchronized with
  user-facing behavior and procedures.

Verification completed successfully:

```text
focused Action Bus test  passed; 9 native tests
make format              passed
make format-check        passed
make lint                passed; native and Cardputer-Adv Cppcheck found no defects
make test                passed; 25 native tests
make build               passed; Cardputer-Adv firmware compiled with strict warnings
make check               passed; lock, format, lint, test, and firmware build
```

Physical-device validation was not performed because this change adds no
hardware-facing or user-visible firmware behavior.
