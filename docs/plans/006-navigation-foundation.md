# Navigation Foundation Plan

Status: **Complete**

This plan describes the next granular Phase 1 System Core change after the
configuration and storage foundation.

Implementation should use:

```text
branch:   feat/006-navigation-foundation
PR title: [006] Establish navigation foundation
```

## 1. Summary

Add a hardware-independent navigation history primitive with owned, opaque
route identifiers and explicit reset, push, and Back behavior. The contract is
usable by the future application shell without introducing views, Mini Apps,
rendering, global shortcuts, or temporary device behavior.

Firmware behavior remains unchanged. Phase 4 will connect the application
shell and global controls to this primitive, and Phase 5 will define Mini App
views and lifecycle.

## 2. Public Contract and Behavior

Add the API under `src/core/navigation/`:

* `RouteId`: an owned `std::string` route identifier;
* `NavigationResult`: `Navigated` or `InvalidRoute`;
* `BackResult`: `Popped`, `AtRoot`, or `Empty`;
* `NavigationStack::resetTo(routeId)`: validate and copy a new root, replacing
  the complete previous history;
* `NavigationStack::push(routeId)`: validate and copy a new history entry;
* `NavigationStack::back()`: remove the current entry only when another entry
  remains beneath it;
* `NavigationStack::current()`: return a read-only pointer to the current route
  or `nullptr` while empty;
* `NavigationStack::depth()`: return the current entry count.

Contract rules:

* route IDs are non-empty, exact, case-sensitive strings;
* the stack owns every accepted ID, so caller mutation cannot change history;
* `resetTo()` is valid for both empty and populated stacks and always leaves
  exactly one root entry;
* `push()` is valid on an empty stack and creates its first/root entry;
* repeated and consecutive route IDs are distinct navigation entries;
* `back()` on a multi-entry stack pops once and exposes the preceding entry;
* `back()` preserves the sole root and reports `AtRoot`;
* `back()` on an empty stack reports `Empty`;
* rejected route IDs do not change the current route or depth;
* navigation is synchronous, single-threaded, and has no fixed depth limit;
* route syntax, parameters, transition effects, and state restoration remain
  responsibilities of later shell and Mini App designs.

Do not reuse `Action` parameters or introduce an `INavigationView` interface.
The foundation stores history tokens only and does not render or activate a
destination.

## 3. Granular TDD Implementation Sequence

Follow RED-GREEN-REFACTOR for each behavior and record the observed RED
failures in the plan's completion record.

1. Add `test/test_navigation/test_main.cpp` with empty-stack queries and Back
   behavior; confirm failure because the navigation contract does not exist.
2. Implement the empty stack, `current()`, `depth()`, and safe empty Back.
3. Add reset tests covering valid roots, owned IDs, replacement of populated
   history, exact case preservation, and invalid-route non-mutation.
4. Implement the minimum `resetTo()` behavior and rerun the focused suite.
5. Add push tests covering empty and populated stacks, insertion order,
   repeated routes, ownership, and invalid-route non-mutation.
6. Implement the minimum `push()` behavior and rerun the focused suite.
7. Add Back traversal tests proving one-entry-at-a-time popping, current-route
   restoration, root preservation, and stable depth at the root.
8. Implement Back traversal and refactor shared validation while keeping the
   suite green.
9. Update the Views and Navigation and development-order sections of
   `docs/ARCHITECTURE.md` to distinguish the Phase 1 history contract from
   Phase 4 shell integration and Phase 5 Mini App views.
10. Leave `SystemRuntime`, `main.cpp`, input, display, Action Bus, and hardware
    adapters unchanged.
11. Confirm that the device manual still correctly reports that no Launcher or
    navigation is user-visible, then complete the plan record.

## 4. Test and Verification Plan

Native navigation scenarios must cover:

* empty `current()`, zero depth, and `BackResult::Empty`;
* reset on empty and populated stacks;
* push on empty and populated stacks;
* owned route strings after caller mutation;
* exact, case-sensitive route preservation;
* repeated route entries and deterministic history order;
* one-level Back traversal through three or more entries;
* preservation of the root with `BackResult::AtRoot`;
* empty route rejection without state mutation.

Run and record:

```text
uv run --frozen pio test -e native -f test_navigation
make format
make format-check
make lint
make test
make build
make check
```

Physical-device validation is not required because this plan adds no runtime or
hardware-facing behavior.

## 5. Acceptance Criteria and Assumptions

The change is complete only when:

* later shell code can own navigation history without hardware or Mini App
  dependencies;
* all reset, push, Back, current-route, and depth outcomes have native
  behavioral coverage;
* invalid routes never mutate the stack;
* Back cannot remove an established root;
* no view, rendering, Action, shortcut, or application lifecycle contract is
  introduced;
* firmware boot, display, keyboard, storage, and Action Bus behavior remain
  unchanged;
* architecture documentation describes the delivered boundary;
* every applicable repository check passes.

Assumptions and defaults:

* Plan 005 is merged and implementation starts from updated `main`;
* the existing C++17 and pinned toolchain configuration remain unchanged;
* future route naming conventions can be added without changing stack
  semantics;
* route-specific state remains owned outside `NavigationStack`;
* shell integration, Home, Launcher, global shortcuts, and navigation Actions
  remain Phase 4 work;
* Mini App view objects and lifecycle remain Phase 5 work.

## 6. Completion Record

### TDD Evidence

The focused native suite recorded these expected RED failures before the
corresponding behavior was implemented:

1. the initial empty-stack test did not compile because
   `core/navigation/navigation_stack.h` did not exist;
2. reset scenarios did not compile because `NavigationStack::resetTo()` did
   not exist;
3. push scenarios did not compile because `NavigationStack::push()` did not
   exist;
4. Back traversal returned the temporary `Empty` result instead of `Popped`
   for a three-entry history.

Each failure was followed by the minimum implementation and a green focused
suite before the next behavior was added. Strict compilation also exposed two
ignored `[[nodiscard]]` results in test setup; the tests were corrected to
assert those navigation outcomes explicitly.

### Delivered Behavior

The completed change provides:

* owned, opaque `RouteId` values and explicit navigation and Back outcomes;
* validated reset and push operations with empty-route rejection and no state
  mutation on rejection;
* exact, case-sensitive history with owned caller input and distinct repeated
  entries;
* current-route and depth queries for empty and populated stacks;
* deterministic, one-entry Back traversal that preserves the established
  root;
* architecture documentation separating history storage from shell
  integration and Mini App views.

`NavigationStack` is not composed into `SystemRuntime` or `main.cpp`, and no
input, display, Action Bus, storage, or hardware-adapter behavior changed.

### Verification

The following checks passed:

```text
focused test_navigation suite 8 cases passed
make format                    passed
make lint                      native and Cardputer-Adv passed
make build                     Cardputer-Adv firmware compiled
make check                     lock, format, lint, 17 Python tests,
                               45 native cases, and firmware build passed
```

The device manual remains accurate: Launcher and navigation are still listed
as unavailable user-facing behavior. Physical-device validation was not
performed because this foundation is hardware-independent and has no runtime
consumer.
