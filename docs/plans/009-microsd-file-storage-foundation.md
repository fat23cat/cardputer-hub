# microSD File Storage Foundation Plan

Status: **Complete**

This plan describes the final granular Phase 1 change after the AppRegistry
foundation. It adds optional removable file storage without making the
firmware or its authoritative configuration depend on a microSD card.

Implementation should use:

```text
branch:   feat/009-microsd-file-storage-foundation
PR title: [009] Establish microSD file storage foundation
```

## 1. Summary

Add:

* a hardware-neutral `FileStorage` facade for known-path files;
* explicit media, read, replace, and removal outcomes;
* shared validation for logical paths below a Cardputer Hub-owned root;
* a Cardputer microSD adapter using the pinned board framework;
* native behavioral tests around the hardware-neutral facade;
* documentation that distinguishes removable files from NVS configuration
  records.

The card is optional. Phase 1 provides the boundary and physical adapter, but
does not add a file browser, automatic backup, configuration import/export,
application assets, runtime capability publication, or product data. Boot and
the current device screen remain unchanged.

`FileStorage` is separate from plan 005's NVS-oriented `Storage` facade. NVS
remains authoritative for configuration required during normal boot. Future
Services may use microSD for explicit exports, backups, assets, and larger app
data without including Arduino, SPI, filesystem, or board-library headers.

## 2. Public Contract and Backend Behavior

Add the hardware-neutral API under `src/core/storage/files/`:

* `FileStorageBytes`: owned `std::vector<std::uint8_t>`;
* `FileStoragePath`: an owned logical relative path;
* `FileStorageState`: `Uninitialized`, `Ready`, `NotPresent`, or `MountError`;
* `FileReadStatus`: `Found`, `NotFound`, `InvalidPath`, `InvalidRequest`,
  `TooLarge`, `Unavailable`, or `BackendError`;
* `FileReadResult`: status plus owned bytes;
* `FileWriteStatus`: `Stored`, `InvalidPath`, `Unavailable`, `ReadOnly`,
  `CapacityExceeded`, or `BackendError`;
* `FileRemoveStatus`: `Removed`, `NotFound`, `InvalidPath`, `Unavailable`, or
  `BackendError`;
* `IFileStorageAdapter`: a non-owning, synchronous interface for refreshing
  media state and reading, replacing, and removing files;
* `FileStorage`: the validated facade composed with an injected
  `IFileStorageAdapter`.

Contract rules:

* callers use paths relative to a Cardputer Hub-owned root and never see a
  mount point;
* paths are non-empty, use `/` separators, and reject absolute paths,
  backslashes, embedded NUL, empty segments, `.` segments, and `..` segments;
* backend-specific filename and path limits are reported as `InvalidPath` by
  the adapter rather than exposed to core callers;
* callers must not create sibling paths that differ only by letter case,
  because matching rules may depend on the mounted filesystem;
* reads require a non-zero caller-provided maximum size; a larger file returns
  `TooLarge` and no bytes before its contents are allocated;
* successful reads return owned bytes, including a valid empty file; every
  unsuccessful read returns empty data;
* writes use replace semantics and permit empty files;
* implementations create required parent directories only below the owned
  root and never write elsewhere on the card;
* invalid paths and invalid read limits never invoke the adapter;
* `Unavailable` is distinct from `NotFound`: it represents absent or
  unmounted media rather than a missing file;
* calls are synchronous and single-threaded, and adapters must not retain
  references passed by the caller;
* `FileStorage` holds a non-owning adapter reference, so the adapter must
  outlive it;
* paths, contents, and file sizes are not logged;
* directory enumeration, arbitrary user-file browsing, append, rename,
  streaming, recursive removal, formatting, and automatic configuration
  import are not added.

Add `CardputerMicroSdFileStorageAdapter` under
`src/hardware/storage/microsd/`:

* use the Cardputer-Adv microSD interface supplied by the pinned framework and
  do not add a third-party filesystem dependency;
* keep SPI, mount, filesystem, and board-library types inside the adapter;
* place all managed content below `/cardputer-hub` on the mounted card;
* let `refresh()` probe and mount the card and return the resulting state;
* treat missing media as `NotPresent` rather than a boot failure;
* map media removal during an operation to `Unavailable` and update the
  adapter state;
* check file size against the caller's read limit before allocating the read
  result;
* flush successful replacements before reporting `Stored`;
* map read-only media and capacity exhaustion separately when the backend can
  identify them, and map other filesystem failures to `BackendError`;
* close every acquired file or directory handle on all paths;
* never format, repair, erase, or repartition a card automatically;
* never access files outside `/cardputer-hub`;
* do not construct the adapter in `main.cpp` or alter `SystemRuntime` until a
  real file consumer and capability owner are introduced.

## 3. Granular TDD Implementation Sequence

Follow RED-GREEN-REFACTOR for each hardware-neutral behavior and record the
observed RED failures in the plan's completion record.

1. Add `test/test_file_storage/test_main.cpp` with path-validation tests and
   confirm failure because the file-storage contract does not exist.
2. Implement file-storage types and validation for relative paths, nested
   paths, absolute paths, separator variants, empty segments, `.`, `..`, and
   embedded NUL.
3. Add media-state tests using a recording fake adapter:
   * the initial state is forwarded;
   * refresh results propagate;
   * `Ready`, `NotPresent`, and `MountError` remain distinguishable.
4. Implement state and refresh forwarding and rerun the focused suite.
5. Add bounded-read tests:
   * known files return owned binary data, including internal zero bytes;
   * empty files are successful;
   * missing, oversized, unavailable, and backend-error outcomes remain
     distinct;
   * invalid paths and zero read limits do not reach the adapter;
   * unsuccessful reads never expose partial or stale bytes.
6. Implement the minimum bounded-read behavior and rerun the focused suite.
7. Add replace tests:
   * valid binary and empty files are forwarded unchanged;
   * caller mutation after the call cannot change fake-backend contents;
   * repeated writes replace the same path;
   * unavailable, read-only, capacity, and backend errors propagate;
   * invalid paths do not reach the adapter.
8. Implement replace behavior and rerun the focused suite.
9. Add removal tests for removed, missing, unavailable, backend-error, and
   invalid-path outcomes, then implement removal and refactor shared test
   fakes while keeping the suite green.
10. Add the Cardputer microSD adapter. Treat vendor filesystem interaction as
    a thin-adapter TDD exception because it is excluded from native builds;
    verify the boundary through strict Cardputer-Adv compilation.
11. Update the persistence, capabilities, hardware-abstraction, development
    order, initial-scope, principles, and target-structure sections of
    `docs/ARCHITECTURE.md`.
12. Update `README.md` to record completion of all Phase 1 foundations without
    claiming that a file browser, configuration import/export, or automatic
    card mounting is available.
13. Audit `docs/manuals/device-guide.md`; retain its existing runtime
    limitations because the adapter is not yet composed into firmware.
14. Complete the plan record with RED failures, delivered behavior, adapter
    compilation corrections, verification results, and physical-card
    validation status.

## 4. Test and Verification Plan

Native file-storage scenarios must cover:

* valid single-segment and nested logical paths;
* empty, absolute, traversal, repeated-separator, backslash, and embedded-NUL
  path rejection;
* initial and refreshed media states;
* bounded reads, oversized-file rejection, empty files, and binary contents;
* owned read results and unchanged fake-backend write data;
* replace semantics for repeated writes;
* empty-file writes;
* removal of present and absent files;
* invalid input short-circuiting without adapter calls;
* unavailable, read-only, capacity, not-found, and backend-error outcomes.

Run and record:

```text
uv run --frozen pio test -e native -f test_file_storage
make format
make format-check
make lint
make test
make build
make check
```

Physical card writes are not required for completion because no runtime
consumer is introduced. If optional device validation is performed, use a
disposable card or disposable files below `/cardputer-hub/test`, record the
filesystem and card capacity, exercise absent-card and mounted-card states,
and remove all test files afterward. Never format a user's card as part of
validation.

## 5. Acceptance Criteria and Assumptions

The change is complete only when:

* future Services can depend on `FileStorage` without including Arduino, SPI,
  filesystem, or Cardputer headers;
* logical paths cannot escape the Cardputer Hub-owned root;
* reads are bounded before file contents are allocated;
* all facade result states have native behavioral coverage;
* invalid operations never reach the hardware adapter;
* absent or failed microSD media cannot block boot or affect NVS records;
* the adapter never formats media or accesses paths outside
  `/cardputer-hub`;
* Cardputer-Adv firmware compiles with the pinned framework and strict
  warnings;
* no product configuration, credentials, or user data are written;
* boot, display, keyboard, Action Bus, navigation, capabilities, and
  AppRegistry behavior remain unchanged;
* architecture documentation describes the NVS/microSD responsibility split;
* every applicable repository check passes.

Phase 1 is complete after this plan only when the repository contains verified
implementations for:

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

* plan 008 is merged and implementation starts from updated `main`;
* the existing C++17 and pinned toolchain configuration remain unchanged;
* the pinned Cardputer-Adv framework supplies the board's microSD interface;
* the physical card and its filesystem are optional runtime dependencies;
* the Cardputer Hub-owned root is `/cardputer-hub`;
* NVS remains the authoritative persistence backend for configuration needed
  during boot;
* concrete file consumers and `REMOVABLE_FILE_STORAGE` capability publication
  require later explicit integration;
* directory enumeration, streaming large files, hot-plug polling, backups,
  import/export, and secret handling require later designs driven by concrete
  use cases.

## 6. Completion Record

### TDD Evidence

The focused native suite recorded these expected RED failures before the
corresponding hardware-neutral behavior was implemented:

1. the initial path-validation test did not compile because
   `core/storage/files/file_storage.h` did not exist;
2. the media-state tests failed to link because `FileStorage::state()` and
   `FileStorage::refresh()` did not exist;
3. unsuccessful reads exposed backend-provided bytes, and a zero read limit
   was forwarded as a valid request instead of returning `InvalidRequest`;
4. replace scenarios failed to link because `FileStorage::replace()` did not
   exist;
5. removal scenarios failed to link because `FileStorage::remove()` did not
   exist.

Each failure was followed by the minimum implementation and a green focused
suite before the next behavior was added. Path validation is shared by read,
replace, and removal operations, while backend state and result policies remain
behind `IFileStorageAdapter`.

### Delivered Behavior

The completed change provides:

* a hardware-neutral `FileStorage` facade with explicit media, read, replace,
  and removal outcomes;
* logical path confinement that rejects absolute paths, traversal, malformed
  segments, backslashes, and embedded NUL before adapter calls;
* bounded reads with owned binary results, valid empty files, and no bytes on
  unsuccessful outcomes;
* replace semantics for binary and empty files plus exact propagation of
  unavailable, read-only, capacity, and backend failures;
* a Cardputer microSD adapter using the pinned Arduino SD and SPI interfaces,
  with all content confined below `/cardputer-hub`;
* parent creation below the managed root, pre-allocation size checks, short
  write capacity detection, explicit handle closure, and flushed successful
  replacements;
* adapter-side rejection of FatFs-invalid or normalized names, plus rejection
  of directory handles as replacement targets;
* documentation separating optional removable files from authoritative NVS
  configuration and recording completion of the Phase 1 foundations.

The adapter passed strict Cardputer-Adv compilation on its first build, so no
API compatibility correction was required. The pinned SD interface reports a
failed initial card or mount probe through one Boolean result; the adapter
therefore reports that initial failure as `NotPresent`, while a mounted card
whose managed root cannot be prepared reports `MountError`. Operation failures
still distinguish unavailable media, read-only media, capacity exhaustion, and
other backend errors where the framework exposes them.

`CardputerMicroSdFileStorageAdapter` is not constructed by `main.cpp` or
`SystemRuntime`. It performs no automatic mount, publishes no capability, and
writes no product configuration, credentials, or user data. Boot, display,
keyboard, Actions, navigation, capabilities, AppRegistry, and NVS behavior are
unchanged.

### Verification

The following checks passed:

```text
focused test_file_storage suite     10 cases passed
make format                          passed
make lint                            native and Cardputer-Adv passed
make build                           Cardputer-Adv firmware compiled
make check                           lock and format checks passed
                                     native and Cardputer-Adv lint passed
                                     17 Python tests passed
                                     67 native cases passed
                                     Cardputer-Adv firmware build passed
```

The device guide and installation manual were audited and remain accurate:
installing this firmware does not mount or use a microSD card, and no supported
control or procedure changes. Physical-card validation was not performed
because the adapter has no runtime consumer; no card was formatted, modified,
or erased.
