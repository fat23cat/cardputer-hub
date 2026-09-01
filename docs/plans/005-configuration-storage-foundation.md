# Configuration and Storage Foundation Plan

Status: **Complete**

This plan describes the next isolated Phase 1 System Core change after the
Action Bus foundation.

Implementation should use:

```text
branch:   feat/005-configuration-storage-foundation
PR title: [005] Establish configuration storage foundation
```

## 1. Summary

Add:

* a hardware-neutral, configuration-facing opaque-record storage contract;
* explicit read, write, and removal outcomes;
* shared validation for portable storage addresses;
* an ESP32 NVS blob adapter using a dedicated configuration partition and the
  pinned framework;
* native behavioral tests around the hardware-neutral facade;
* architecture documentation defining the boundary with the future
  `ConfigurationService`.

The validated `Storage` facade is the Phase 1 configuration interface used by
future Services. Its adapter boundary is the persistence primitive that keeps
ESP32 NVS details outside core logic. This change does not introduce a
configuration schema or persist product data. Firmware behavior and the
current boot screen remain unchanged.

This plan covers internal record persistence only. The optional microSD card
has different availability, path, capacity, and file-lifecycle semantics and
must not be hidden behind the NVS-oriented `Storage` contract. Its separate
`FileStorage` facade and Cardputer adapter are delivered by plan 009.

Deferred work includes microSD file storage, navigation, capabilities,
`AppRegistry`, `ConfigurationService`, serialization, defaults, domain
validation, migrations, credential provisioning, connectivity, Services, and
Mini Apps.

## 2. Public Contracts and Backend Behavior

Add the hardware-neutral API under `src/core/storage/`:

* `StorageBytes`: owned `std::vector<std::uint8_t>`;
* `StorageAddress`: owned `scope` and `key` strings;
* `StorageReadStatus`: `Found`, `NotFound`, `InvalidAddress`, or
  `BackendError`;
* `StorageReadResult`: status plus owned bytes;
* `StorageWriteStatus`: `Stored`, `InvalidAddress`, `InvalidData`,
  `CapacityExceeded`, or `BackendError`;
* `StorageRemoveStatus`: `Removed`, `NotFound`, `InvalidAddress`, or
  `BackendError`;
* `IStorageAdapter`: non-owning, synchronous persistence interface for
  reading, writing, and removing records;
* `Storage`: validated configuration-facing facade used by future Services and
  composed with an injected `IStorageAdapter`.

Contract rules:

* `scope` and `key` are exact, case-sensitive identifiers containing 1-15
  bytes and no embedded NUL;
* the 15-byte portable limit matches the selected ESP32 NVS backend and is
  enforced before calling an adapter;
* record data is opaque, binary-safe, and may contain zero bytes;
* writes require a non-empty record; absence is represented by `NotFound` and
  deletion by `remove()`;
* a successful write replaces the previous record at the same address;
* a successful read returns an owned copy; every unsuccessful read returns
  empty data;
* invalid addresses and empty writes never invoke the adapter;
* calls are synchronous and single-threaded, and adapters must not retain
  references passed by the caller;
* `Storage` holds a non-owning adapter reference, so the adapter must outlive
  it;
* typed getters, serialization, schema knowledge, iteration, wildcard
  removal, namespace clearing, and automatic fallback values are not added.

Add `Esp32NvsStorageAdapter` under `src/hardware/esp32/`:

* use blob records in the dedicated `hub_config` NVS partition without adding
  a dependency;
* explicitly initialize only `hub_config` and open every namespace through
  the partition-specific API;
* open the requested namespace for each operation and close every acquired
  handle on all paths;
* read blobs by querying their size, allocating the exact temporary buffer,
  and then loading their contents;
* call `nvs_commit()` before reporting a write or removal as successful;
* map missing namespaces or keys to `NotFound`;
* map NVS space exhaustion and oversized-value errors to
  `CapacityExceeded`;
* map type mismatches, initialization failures, invalid NVS state, commit
  failures, and other backend errors to `BackendError`;
* never erase or reinitialize the NVS partition as error recovery;
* do not log scopes, keys, record sizes, or record contents;
* do not construct the adapter in `main.cpp` or alter `SystemRuntime` until a
  real configuration consumer is introduced.

## 3. Granular TDD Implementation Sequence

Follow RED-GREEN-REFACTOR for each behavior and record the observed RED
failures in the plan's completion record.

1. Add `test/test_storage/test_main.cpp` with address-boundary tests and
   confirm failure because the storage contract does not exist.
2. Implement storage types and validation for empty, 15-byte, 16-byte, and
   embedded-NUL identifiers.
3. Add read tests using a recording fake adapter:
   * valid addresses are forwarded unchanged;
   * found binary records are returned as owned bytes;
   * missing and backend-error reads return empty data;
   * invalid addresses do not reach the adapter.
4. Implement the minimum read-facade behavior and rerun the focused suite.
5. Add write tests:
   * valid non-empty binary data is forwarded unchanged;
   * caller mutation after the call cannot change stored fake-backend data;
   * replacement outcomes propagate;
   * empty data is rejected;
   * invalid addresses, capacity failures, and backend failures are
     distinguishable.
6. Implement the minimum write behavior and rerun the focused suite.
7. Add removal tests:
   * valid addresses are forwarded unchanged;
   * removed, missing, and backend-error outcomes propagate;
   * invalid addresses do not reach the adapter.
8. Implement removal and refactor shared validation and test fakes while
   keeping the suite green.
9. Add the ESP32 NVS adapter and a version-controlled partition table that
   keeps authoritative configuration separate from the framework's default
   NVS. Treat direct NVS interaction as a thin-adapter TDD exception because
   vendor APIs are excluded from native builds; protect the partition choice
   with a Python safety-invariant test and verify its boundary through strict
   Cardputer-Adv compilation.
10. Update the configuration, persistence, and hardware-abstraction sections
    of `docs/ARCHITECTURE.md`:
    * the `Storage` facade is System Core's configuration-facing interface;
    * the adapter contract is the hardware-independent persistence primitive;
    * System Core persists opaque records only;
    * `ConfigurationService` owns schemas, serialization, defaults,
      validation, and migrations;
    * backend errors remain distinguishable from missing configuration;
    * NVS is the authoritative backend for boot-critical configuration, while
      removable microSD files use the separate plan 009 contract;
    * no configuration value is currently persisted by the firmware.
11. Update the installation manual with the one-time migration from the prior
    flash layout and the paired application/partition release assets; confirm
    the device guide's "configuration or persistence" limitation remains
    accurate.
12. Complete the plan record with RED failures, delivered behavior, any NVS
    compilation corrections, verification results, and physical-device
    validation status.

## 4. Test and Verification Plan

Native storage scenarios must cover:

* identifier lengths of 0, 1, 15, and 16 bytes;
* embedded NUL rejection in both address components;
* exact and case-sensitive address forwarding;
* arbitrary non-empty binary records, including internal zero bytes;
* owned read results and unchanged write data;
* invalid input short-circuiting without adapter calls;
* found, missing, stored, removed, capacity, and backend-error outcomes;
* unsuccessful reads never exposing partial or stale bytes;
* repeated writes to the same address using replacement semantics;
* removal of an absent record returning `NotFound`.

Run and record:

```text
uv run --frozen pio test -e native -f test_storage
make format
make format-check
make lint
make test
make build
make check
```

Physical NVS writes are not required for completion because no runtime
consumer is introduced. If optional device validation is performed, use a
dedicated disposable namespace, record exactly what was written and removed,
and leave no test records behind.

## 5. Acceptance Criteria and Assumptions

The change is complete only when:

* future Services can depend on `Storage` without including Arduino, ESP32, or
  NVS headers;
* storage addresses and empty records follow the documented validation
  contract;
* all result states have native behavioral coverage;
* invalid operations never reach the hardware adapter;
* the ESP32 adapter commits successful mutations and never performs
  destructive partition recovery;
* authoritative records use a dedicated NVS partition that Arduino framework
  initialization cannot automatically erase;
* Cardputer-Adv firmware compiles with the pinned ESP32 framework and strict
  warnings;
* no product configuration, credentials, or user data are written;
* boot, display, keyboard, and Action Bus behavior remain unchanged;
* architecture documentation describes the delivered configuration and
  persistence boundary, including the separation from microSD file storage;
* every applicable repository check passes.

Assumptions and defaults:

* Plans 001-003 are merged and implementation starts from updated `main`;
* the existing C++17 and pinned toolchain configuration remain unchanged;
* Arduino owns and initializes the default NVS partition before application
  code runs, while the adapter initializes the separate `hub_config` partition
  lazily and treats initialization failure as `BackendError`;
* one NVS record is the atomic persistence unit;
* plan 009 adds optional microSD file storage without changing this record
  contract;
* the future `ConfigurationService` chooses record names and serialization
  formats;
* NVS encryption, configuration schema versioning, migrations, recovery
  policy, and secret handling require explicit later designs before
  credentials are persisted;
* navigation, the Capability Registry, and `AppRegistry` remain separate Phase
  1 follow-up plans 006, 007, and 008 respectively.

## 6. Completion Record

### TDD Evidence

The focused native suite recorded these expected RED failures before the
corresponding behavior was implemented:

1. the initial address-boundary test did not compile because
   `core/storage/storage.h` did not exist;
2. unsuccessful adapter reads exposed stale bytes instead of returning empty
   data;
3. an empty write reached the adapter and returned `Stored` instead of
   `InvalidData`;
4. valid removal scenarios returned the temporary `BackendError` placeholder
   instead of forwarding `Removed` and `NotFound` outcomes.
5. the partition safety regression test failed because the build used the
   framework partition table, `hub_config` did not exist, and the adapter used
   default-partition NVS APIs.
6. release, rebuild, CI-artifact, and installation migration checks failed
   because only `firmware.bin` was distributed and no one-time provisioning
   path existed for the range previously owned by SPIFFS.
7. the migration port-safety test failed because the destructive erase
   independently auto-detected a serial device instead of requiring and reusing
   the upload port.

Each failure was followed by the minimum implementation and a green focused
suite before the next behavior was added.

### Delivered Behavior

The completed change provides:

* the hardware-neutral `Storage` facade, owned byte records, addresses, result
  types, and injected `IStorageAdapter` contract;
* shared 1-15 byte, non-empty, embedded-NUL-free address validation;
* binary-safe reads and writes, owned read results, empty-write rejection,
  replacement forwarding, and distinct missing, capacity, and backend errors;
* an ESP32 NVS blob adapter using the dedicated `hub_config` partition that
  closes every opened handle, commits mutations, maps initialization and other
  backend outcomes, and performs no destructive recovery or logging;
* a version-controlled 8 MiB flash layout that preserves the framework's
  default NVS while reserving 64 KiB for authoritative configuration;
* release, historical-rebuild, and CI artifacts that pair the application
  image with its partition table and checksum both release files;
* an explicit one-time storage-layout migration that erases only the newly
  allocated range on the explicitly selected upload device, while routine
  uploads preserve configuration;
* architecture documentation for the record-storage, configuration-policy,
  and removable-file-storage boundaries.

The NVS adapter compiled against the pinned ESP32 framework on its first strict
firmware build; no vendor-API corrections were required. It is not constructed
in `main.cpp`, and no configuration or product data is currently persisted.

### Verification

The following checks passed:

```text
focused test_storage suite     12 cases passed
make format                    passed
make lint                      native and Cardputer-Adv passed
make build                     Cardputer-Adv firmware compiled
make check                     lock, format, lint, 17 Python tests,
                               37 native cases, and firmware build passed
```

The installation manual documents the required one-time flash-layout migration
and paired release assets. The device manual remains accurate: configuration
and persistence are still listed as unavailable user-facing behavior. Physical
device validation was not performed because the adapter has no runtime
consumer and the plan does not require physical NVS writes.
