# Capability Registry Foundation Plan

Status: **Complete**

This plan describes the next granular Phase 1 System Core change after the
navigation foundation.

Implementation should use:

```text
branch:   feat/007-capability-registry-foundation
PR title: [007] Establish capability registry foundation
```

## 1. Summary

Add a hardware-independent registry that records which logical platform
capabilities are currently available. The registry owns capability IDs and
supports exact queries and deterministic enumeration without knowing which
Service or hardware adapter provides them.

This plan does not connect capabilities to application eligibility. Phase 5
will combine AppRegistry metadata with capability availability when Mini App
integration is implemented. Firmware behavior remains unchanged.

## 2. Public Contract and Behavior

Add the API under `src/core/capabilities/`:

* `CapabilityId`: an owned `std::string` capability identifier;
* `CapabilityRegistrationResult`: `Registered`, `InvalidId`, or `DuplicateId`;
* `CapabilityRemovalResult`: `Removed`, `InvalidId`, or `NotFound`;
* `CapabilityRegistry::registerCapability(id)`: validate and copy an available
  capability;
* `CapabilityRegistry::removeCapability(id)`: make an available capability
  unavailable;
* `CapabilityRegistry::isAvailable(id)`: exact availability query;
* `CapabilityRegistry::availableCapabilities()`: read-only registration-order
  enumeration.

Contract rules:

* capability IDs are non-empty, exact, case-sensitive strings;
* uppercase IDs such as `WIFI` and `BLE_HID` are a naming convention, not a
  validation rule;
* the registry owns accepted IDs, so caller mutation cannot change its state;
* duplicate registration reports `DuplicateId` without changing order;
* removal reports `NotFound` when a valid exact ID is absent;
* invalid registration and removal never mutate the registry;
* queries for empty or unknown IDs return false without mutation;
* enumeration preserves successful registration order;
* removing and later registering an ID appends it as a new final entry;
* availability may change during runtime, but mutation and queries are
  synchronous and single-threaded;
* the registry does not infer capabilities from hardware or dependency state.

Do not add capability providers, reference counting, aliases, dependencies,
status details, observers, persistence, compile-time enumerators, or
application filtering.

## 3. Granular TDD Implementation Sequence

Follow RED-GREEN-REFACTOR for each behavior and record the observed RED
failures in the plan's completion record.

1. Add `test/test_capability_registry/test_main.cpp` with empty-registry
   queries and confirm failure because the capability contract does not exist.
2. Add registration tests for ownership, valid IDs, registration order,
   duplicate rejection, empty-ID rejection, and exact case handling.
3. Implement the minimum registration and enumeration behavior and rerun the
   focused suite.
4. Add query tests for available, missing, empty, and differently cased IDs.
5. Implement the minimum exact availability query.
6. Add removal tests for successful removal, missing IDs, invalid IDs, stable
   ordering of remaining entries, and removal followed by re-registration.
7. Implement removal and refactor shared lookup and validation while keeping
   the suite green.
8. Update the Capabilities and development-order sections of
   `docs/ARCHITECTURE.md` with the owned dynamic registry contract and the
   Phase 5 integration boundary.
9. Leave `SystemRuntime`, `main.cpp`, AppRegistry, navigation, Services, and
   hardware adapters unchanged.
10. Confirm that no device-manual behavior changes, then complete the plan
    record with RED failures and actual verification results.

## 4. Test and Verification Plan

Native capability scenarios must cover:

* empty-registry query and enumeration;
* owned capability IDs after caller mutation;
* successful registration and registration-order enumeration;
* duplicate registration without mutation or reordering;
* empty-ID rejection for registration and removal;
* exact, case-sensitive availability queries;
* successful and missing removal outcomes;
* preservation of remaining enumeration order after removal;
* removed capability becoming unavailable;
* re-registration appending the capability after existing entries.

Run and record:

```text
uv run --frozen pio test -e native -f test_capability_registry
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

* future Services can publish logical availability without exposing hardware
  implementation types;
* registration, removal, querying, and enumeration outcomes have native
  behavioral coverage;
* invalid and duplicate operations leave registry state unchanged;
* identifiers remain extensible strings rather than application-specific
  enumerators;
* no AppRegistry filtering, Mini App lifecycle, persistence, observer, or
  provider model is introduced;
* firmware boot and all existing System Core behavior remain unchanged;
* architecture documentation describes the delivered contract and deferred
  integration;
* every applicable repository check passes.

Assumptions and defaults:

* Plan 006 is merged and implementation starts from updated `main`;
* the existing C++17 and pinned toolchain configuration remain unchanged;
* one logical owner coordinates each capability in the initial system;
* multi-provider accounting requires a later explicit design if it becomes
  necessary;
* concrete capabilities are registered only when their owning layers are
  implemented;
* capability-based application eligibility remains Phase 5 work.

## 6. Completion Record

### TDD Evidence

The focused native suite recorded these expected RED failures before the
corresponding behavior was implemented:

1. the initial empty-registry test did not compile because
   `core/capabilities/capability_registry.h` did not exist;
2. the exact-query scenario reported a registered `WIFI` capability as
   unavailable while query behavior was still a placeholder;
3. removal scenarios did not compile because
   `CapabilityRegistry::removeCapability()` did not exist.

Each failure was followed by the minimum implementation and a green focused
suite before the next behavior was added. Registration, querying, and removal
were then refactored to share the same non-empty validation and exact lookup
rules.

### Delivered Behavior

The completed change provides:

* owned, extensible `CapabilityId` strings and explicit registration and
  removal outcomes;
* exact, case-sensitive availability queries with empty and unknown IDs
  reported unavailable;
* deterministic successful-registration ordering and duplicate rejection;
* removal that preserves remaining order, plus re-registration that appends a
  new final entry;
* architecture documentation separating the dynamic registry from providers,
  hardware inference, persistence, observers, and Phase 5 application
  filtering.

`CapabilityRegistry` is not composed into `SystemRuntime` or `main.cpp`, and no
AppRegistry, navigation, Service, input, display, storage, or hardware-adapter
behavior changed.

Plan 007 began as a stacked branch while plan 006 was awaiting merge, then was
rebased onto updated `main` after that dependency landed.

### Verification

The following checks passed:

```text
focused test_capability_registry suite 6 cases passed
make format                         passed
make lint                           native and Cardputer-Adv passed
make build                          Cardputer-Adv firmware compiled
make check                          lock, format, lint, 17 Python tests,
                                    51 native cases, and firmware build passed
```

The device manual remains accurate because no capability is registered by the
runtime and no user-visible behavior changed. Physical-device validation was
not performed because this foundation is hardware-independent and has no
runtime consumer.
