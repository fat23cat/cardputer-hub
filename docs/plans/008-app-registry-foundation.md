# AppRegistry Foundation Plan

Status: **Complete**

This plan describes the final hardware-independent registry change in Phase 1.
It follows the capability registry foundation without introducing the Phase 4
application shell or Phase 5 Mini App lifecycle. The separate microSD file
storage foundation in plan 009 closes the expanded Phase 1 checklist.

Implementation should use:

```text
branch:   feat/008-app-registry-foundation
PR title: [008] Establish AppRegistry foundation
```

## 1. Summary

Add a metadata-only `AppRegistry` that lets future applications declare stable
launcher metadata without requiring Launcher logic to know individual apps.
The registry owns descriptors, validates them, provides exact lookup, and
preserves deterministic registration order.

No application instance, factory, lifecycle, rendering, capability filtering,
or runtime registration is added. Firmware behavior and the current device
manual remain unchanged.

## 2. Public Contract and Behavior

Add the API under `src/core/app_registry/`:

* `AppDescriptor` with owned `id`, `displayName`, optional `iconId`,
  `entryRoute`, and ordered `requiredCapabilities`;
* `AppRegistrationResult`: `Registered`, `InvalidDescriptor`, or
  `DuplicateId`;
* `AppRegistry::registerApp(descriptor)`: validate and copy application
  metadata;
* `AppRegistry::find(id)`: return a read-only pointer to the exact registered
  descriptor or `nullptr`;
* `AppRegistry::apps()`: return read-only descriptors in registration order.

Descriptor and registry rules:

* app ID, display name, and entry route are non-empty;
* icon ID is an optional opaque string and may be empty for a text-only
  fallback;
* required capability IDs are non-empty, exact, case-sensitive, and unique
  within a descriptor;
* the registry owns all accepted strings and collections, so later caller
  mutation cannot change registered metadata;
* app IDs are matched exactly and case-sensitively;
* duplicate app IDs report `DuplicateId` and preserve the first descriptor;
* invalid descriptors report `InvalidDescriptor` without changing registry
  contents or order;
* registration order is stable and supplies the default future Launcher order;
* the registry does not inspect `CapabilityRegistry` or hide, disable, or sort
  applications;
* the entry route remains an opaque ID interpreted only after shell and Mini
  App integration exists;
* registration and queries are synchronous and single-threaded;
* static composition is the initial model, so unregistering is not added.

Do not introduce `IMiniApp`, app factories, app instances, lifecycle callbacks,
view objects, enabled-app configuration, icon rendering, Launcher UI, or
runtime composition.

## 3. Granular TDD Implementation Sequence

Follow RED-GREEN-REFACTOR for each behavior and record the observed RED
failures in the plan's completion record.

1. Add `test/test_app_registry/test_main.cpp` with empty lookup and enumeration
   tests; confirm failure because the registry contract does not exist.
2. Add successful registration tests covering all metadata, owned strings and
   collections, exact lookup, and registration-order enumeration.
3. Implement the minimum descriptor storage and query behavior and rerun the
   focused suite.
4. Add descriptor-validation tests for empty ID, display name, and entry route,
   plus empty and duplicate capability requirements.
5. Implement validation and prove rejected descriptors do not mutate contents
   or ordering.
6. Add duplicate-ID tests proving exact, case-sensitive matching and
   preservation of the original descriptor.
7. Implement duplicate rejection and refactor shared validation and lookup
   while keeping the suite green.
8. Update the App Registry, Mini App Model, Capabilities, and development-order
   sections of `docs/ARCHITECTURE.md` with the metadata-only boundary and the
   Phase 5 integration deferrals.
9. Update `README.md` to record the delivered AppRegistry foundation and list
   the new native coverage without claiming that Phase 1, Launcher, navigation
   UI, or Mini Apps are operational.
10. Audit `docs/manuals/device-guide.md`; retain its current limitations unless
    wording must change to distinguish internal foundations from supported
    controls.
11. Leave `SystemRuntime`, `main.cpp`, display, navigation runtime wiring,
    Actions, Services, and hardware adapters unchanged.
12. Complete the plan record with RED failures, delivered behavior, the
    remaining Phase 1 checklist item, actual verification results, and
    physical-device validation status.

## 4. Test and Verification Plan

Native AppRegistry scenarios must cover:

* empty-registry lookup and enumeration;
* successful registration with required and optional metadata;
* empty optional icon acceptance;
* ownership after caller strings and capability collections change;
* exact, case-sensitive app lookup;
* stable registration-order enumeration;
* invalid empty ID, display name, and entry route;
* invalid empty or duplicate required capability IDs;
* invalid descriptor rejection without state mutation;
* duplicate app rejection without replacement or reordering;
* differently cased app IDs remaining distinct.

Run and record:

```text
uv run --frozen pio test -e native -f test_app_registry
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

* future Launcher code can enumerate application metadata without hardcoded
  knowledge of individual apps;
* descriptors are owned, validated, exactly addressable, and deterministically
  ordered;
* registration and failure outcomes have native behavioral coverage;
* duplicate or invalid registrations cannot replace or reorder existing apps;
* no Mini App instance, lifecycle, view, filtering, or rendering contract is
  introduced ahead of its documented phase;
* README and architecture documentation record the AppRegistry foundation
  without marking the expanded Phase 1 checklist complete;
* the device manual remains accurate;
* every applicable repository check passes.

After this plan, Phase 1 remains open only for the microSD file-storage
foundation. The complete checklist is:

```text
Boot and Logging                 plan 002
Input and Display                plan 002
Action model and Action Bus      plan 003
Configuration and Record Storage  plan 005
Navigation                        plan 006
Capability Registry               plan 007
AppRegistry                       plan 008
microSD File Storage              plan 009
```

Assumptions and defaults:

* Plan 007 is merged and implementation starts from updated `main`;
* the existing C++17 and pinned toolchain configuration remain unchanged;
* optional icon IDs are logical references; their asset format and rendering
  are deferred;
* registration order is the initial default order, while user-configurable
  ordering requires later configuration design;
* Home, Launcher, navigation integration, and global shortcuts remain Phase 4
  work;
* `IMiniApp`, AppRegistry integration, app lifecycle, and capability checks
  remain Phase 5 work.

## 6. Completion Record

### TDD Evidence

The focused native suite recorded these expected RED failures before the
corresponding behavior was implemented:

1. the initial empty-registry test did not compile because
   `core/app_registry/app_registry.h` did not exist;
2. descriptors with empty required fields, an empty required capability, or a
   duplicate required capability returned `Registered` instead of
   `InvalidDescriptor` and mutated the registry;
3. a second descriptor with the same exact app ID returned `Registered`
   instead of `DuplicateId`.

Each failure was followed by the minimum implementation and a green focused
suite before the next behavior was added. Validation and exact app lookup were
then centralized while the suite remained green.

### Delivered Behavior

The completed change provides:

* owned `AppDescriptor` metadata with an optional empty icon ID and ordered
  required capability IDs;
* exact, case-sensitive lookup and deterministic registration-order
  enumeration;
* validation for required descriptor fields and capability requirements;
* duplicate-ID rejection that preserves the first descriptor and its order;
* architecture documentation separating metadata registration from Launcher,
  Mini App lifecycle, navigation, rendering, and capability eligibility.

`AppRegistry` is not composed into `SystemRuntime` or `main.cpp`, and no
display, navigation wiring, Action, Service, or hardware-adapter behavior
changed. Plan 008 began as a stacked branch while plan 007 remained under
review, then was rebased onto updated `main` after that dependency landed.

The remaining Phase 1 checklist item is the microSD file-storage foundation in
plan 009. Launcher behavior remains Phase 4, while Mini App and capability
integration remain Phase 5.

### Verification

The following checks passed:

```text
focused test_app_registry suite     6 cases passed
make format                         passed
make check                          lock and format checks passed
                                    native and Cardputer-Adv lint passed
                                    17 Python tests passed
                                    57 native cases passed
                                    Cardputer-Adv firmware build passed
```

The device guide was audited and remains accurate because the metadata
registry has no runtime consumer or user-visible behavior. Physical-device
validation was not performed because this foundation is hardware-independent
and is not composed into the firmware runtime.
