# Host Profile Metadata and Configuration Migration Plan

Status: **Implemented**

This plan describes the next isolated Phase 3 Core Services change after the
host/configuration slice delivered by plan 017.

Implementation should use:

```text
branch:   feat/018-host-profile-metadata
PR title: [018] Extend host profile metadata and configuration
```

The implementation branch must begin from updated `main` after plan 017 is
merged. Outstanding physical acceptance from plan 017 remains separately
tracked and must not be reported as completed by this configuration work.

## 1. Summary and Rationale

Extend the persisted `HostProfile` model with:

* an optional platform identifier;
* a bounded set of host-capability identifiers;
* an optional mapping-template identifier;
* explicit version-1 to version-2 configuration migration;
* host-level operations that update this metadata without disturbing the
  selected host or Bluetooth lifecycle;
* native behavioral tests for validation, serialization, migration, failure,
  and isolation.

This is required before later Device Manager and Host Control work can treat a
host's platform, abilities, and mapping choice as data. The current version-1
`HUBH` record contains only ID, name, bond, selection, next ID, and Bluetooth
On/Off. Its decoder rejects trailing bytes and unknown versions, so adding
fields without a versioned migration would either corrupt existing profiles or
silently introduce a second source of truth.

The scope is intentionally narrow. A mapping-template identifier is persisted,
but template definitions and Action-to-HID resolution remain Phase 7 work. This
plan does not add inert transport routing, infer behavior from a platform, or
claim that selecting a template makes any host command available.

## 2. Host Profile Metadata Contract

Extend the hardware-independent configuration model under
`src/services/configuration/` with:

* `HostPlatformId`: an optional stable textual identifier;
* `HostCapabilityId`: a stable textual identifier distinct from the
  Cardputer-side runtime `CapabilityRegistry`;
* `HostMappingTemplateId`: an optional stable textual identifier;
* the corresponding fields on `HostProfile`.

Identifier rules:

* identifiers are exact, case-sensitive ASCII data;
* a present identifier contains 1-32 bytes and matches
  `[a-z0-9][a-z0-9._-]*`;
* absence, rather than a magic value such as `unknown`, represents an
  unconfigured platform or mapping template;
* one host contains at most 16 unique capability identifiers;
* capability order is stable configuration data but does not imply priority;
* duplicate, empty, malformed, or over-length identifiers are invalid;
* profile names and opaque bond references retain their existing validation;
* the complete serialized record remains explicitly bounded and must fit well
  inside the existing `hub_config` partition.

Newly imported bonds and newly paired hosts start with no platform, no declared
capabilities, and no mapping-template selection. A platform must not imply a
capability or template automatically. In particular, values such as `macos` or
`windows` are profile data and must not create personal-device special cases in
`HostService`, Connectivity, or hardware adapters.

Host capabilities describe what a configured target is expected to support.
They are not Cardputer hardware availability and must not be registered in the
System Core `CapabilityRegistry`. Unknown but syntactically valid identifiers
are preserved so later extension-owned capabilities do not require a firmware
schema change.

The mapping-template field stores only a stable reference. Unknown but valid
references are preserved as configuration data; Phase 7 will define template
catalog ownership, resolution, and unsupported-template behavior before the
reference is consumed.

## 3. HostService Operations and Actions

Add explicit `HostService` operations and logical Actions for metadata updates:

* set or clear a host platform;
* add or remove one host capability;
* set or clear a host mapping-template reference.

Use Action IDs owned by `HostService` and parameters representable by the
existing `ActionValue` contract. The implementation plan should use these
shapes unless RED tests expose a contract problem:

```text
host.platform          id, platform        empty platform clears
host.capability        id, capability, enabled
host.mapping-template  id, template        empty template clears
```

All operations must:

* reject invalid parameters and unknown host IDs without writing storage;
* validate the complete candidate `HostConfiguration`, not only the changed
  field;
* persist successfully before publishing the changed in-memory value;
* preserve the previous value when storage fails;
* be idempotent, with an unchanged request succeeding without an NVS write;
* leave `activeHost`, `nextHostId`, profile identity, display name, and bond
  reference unchanged;
* make no Bluetooth adapter call and neither connect, disconnect, advertise,
  pair, nor release HID reports;
* avoid logging host metadata or configuration record contents.

This plan adds no UI. The full Device Manager may later emit these Actions, but
presentation, editing controls, and Launcher/Mini App integration remain in
Phases 5 and 6.

## 4. Version-2 Record and Migration Policy

Keep the authoritative host configuration at
`StorageAddress{"hosts", "configuration"}`. Add a version-2 `HUBH` encoding
rather than creating a second metadata record, so profile creation, deletion,
selection, and metadata stay one atomic configuration write.

Version 2 retains the existing global fields and, for every host, retains the
version-1 fields before adding bounded platform, capability-list, and
mapping-template fields. Lengths and counts must be encoded explicitly. The
decoder must consume the entire record and reject truncation, trailing bytes,
invalid booleans, invalid identifiers, duplicate capabilities, duplicate host
IDs or bonds, impossible selection, and every other existing invalid state.

Migration is a read migration:

1. A missing record still loads the existing empty, Bluetooth-Off defaults and
   performs no write.
2. A valid version-1 record loads into the version-2 in-memory model with all
   new metadata absent and performs no write.
3. The next successful configuration mutation writes the complete version-2
   record atomically through `Storage`.
4. A valid version-2 record loads without rewriting it.
5. Invalid, truncated, unreadable, or future-version records are preserved and
   return the existing explicit error; they are never replaced with defaults.
6. Failed version-2 writes preserve both the last published in-memory value and
   the previous stored record, allowing an explicit retry.

The lazy write avoids wearing NVS or making downgrade compatibility worse merely
by booting the new firmware. After any successful configuration mutation writes
version 2, older firmware that only understands version 1 will reject the record
without erasing it. The installation/operation documentation must state this
downgrade limitation before the implementation is considered complete.

## 5. Non-Goals

This plan does not:

* add platform/template editing to the current Hosts settings screen;
* implement the full Device Manager Mini App or Launcher integration;
* define a built-in mapping-template catalog;
* resolve logical Actions into shortcuts or HID reports;
* send keyboard, consumer-control, or other host input;
* change Bluetooth pairing, bond identity, host selection, reconnection, or
  On/Off behavior;
* reuse host capabilities as Cardputer runtime capabilities;
* add Wi-Fi, Mini App, Service, shortcut, indicator, or remote configuration;
* complete Phase 3, Phase 6, Phase 7, or plan 017 physical acceptance.

## 6. Granular TDD Implementation Sequence

Follow RED-GREEN-REFACTOR for each behavioral step and record the observed RED
failures in this plan's completion record.

1. Add failing profile-validation tests for absent metadata and valid boundary
   identifiers; implement only the new model defaults.
2. Add failing tests for malformed, over-length, duplicate, and over-count
   metadata; implement the shared identifier and collection validation.
3. Add a failing version-2 round-trip test covering platform, multiple
   capabilities, and mapping-template selection; implement the minimum bounded
   encoder and decoder.
4. Add truncation-at-every-byte, trailing-data, invalid-count, invalid-length,
   duplicate-capability, and future-version tests; harden decoding without
   publishing partial state or overwriting storage.
5. Preserve a fixed valid version-1 fixture and add a failing migration test
   proving it loads with empty metadata, retains IDs/bonds/selection/On-Off,
   and performs no write.
6. Add a failing lazy-upgrade test proving the next real save writes version 2,
   reloads exactly, and does not modify the original record when the write
   fails.
7. Add failing `HostService` tests for platform set/clear, capability
   add/remove, and template set/clear through logical Actions; implement the
   minimum operations.
8. Add unknown-host, invalid-parameter, duplicate/no-op, storage-failure, and
   action-result tests. Confirm rejected and unchanged requests make no writes.
9. Add integration regressions proving metadata edits preserve selection and a
   ready selected-host connection and make no Bluetooth calls.
10. Add import, pairing, rename, deletion, and reboot regressions proving new
    metadata defaults correctly and existing metadata follows the same stable
    profile ID without leaking to another host.
11. Refactor shared configuration parsing and metadata helpers while all
    focused suites remain green.
12. Update `docs/ARCHITECTURE.md`, README, and the device guide to describe only
    delivered data behavior, the v1/v2 migration policy, the absence of editing
    UI, and the downgrade limitation. Do not mark mapping execution or the full
    Device Manager as implemented.
13. Complete this plan with RED/GREEN evidence, final serialized bounds, check
    results, firmware-size change, and any physical observations.

## 7. Test and Verification Plan

Native configuration scenarios must cover:

* valid and absent metadata at every identifier boundary;
* invalid identifier characters, lengths, counts, and duplicates;
* version-2 round trips for zero, one, and maximum host/capability counts;
* every truncation of representative valid version-1 and version-2 records;
* trailing bytes and future schema versions;
* version-1 read migration with no write;
* first successful mutation upgrading the record to version 2;
* failed writes preserving stored and published state;
* metadata Action validation and idempotence;
* isolation from Bluetooth state and adapter calls;
* preservation through host rename, selection, deletion, pairing, import, and
  reload.

Run and record:

```text
uv run --frozen pio test -e native -f test_host_configuration
uv run --frozen pio test -e native -f test_host_service
make format
make format-check
make lint
make test
make build
make check
```

Physical host interaction is not required to establish metadata or migration
behavior because this plan deliberately has no radio or UI effect. Production
Cardputer-Adv compilation remains required. An optional device smoke test may
upgrade a disposable configuration and reboot it, but must not erase NVS,
delete bonds, or be reported as completing plan 017's acceptance matrix.

## 8. Acceptance Criteria and Assumptions

The change is complete only when:

* each Host Profile can represent optional platform, bounded host capabilities,
  and an optional mapping-template reference without hardware-specific types;
* valid version-1 records load losslessly with safe metadata defaults and no
  boot-time write;
* all successful configuration mutations serialize version 2 atomically;
* malformed and unknown-version records remain distinguishable from missing
  configuration and are never reset silently;
* failed writes and invalid Actions cannot replace published state;
* metadata changes never alter host selection or Bluetooth/HID lifecycle;
* personal host names, platforms, and mappings remain data rather than
  application-logic branches;
* template execution, Device Manager editing UI, and Host Control remain
  explicitly unimplemented;
* architecture, README, and manuals accurately describe the delivered scope
  and migration/downgrade behavior;
* every applicable repository check passes.

Assumptions and defaults:

* plan 017 is merged first;
* the existing C++17 toolchain, `Storage` address, dedicated `hub_config`
  partition, and Bluetooth bond-reference contract remain unchanged;
* at most 16 Host Profiles remain supported;
* v1 and v2 are the only accepted versions in this plan;
* migration does not erase, rename, split, or copy the authoritative record;
* no Wi-Fi credentials, Mini App settings, Service settings, global shortcuts,
  indicator settings, or remote settings are added;
* no mapping template is resolved or converted into an HID report;
* no manual recovery flow, export/import feature, or downgrade converter is
  introduced.

## 9. Completion Record

Implemented on 2026-09-11 from updated `main` on
`feat/018-host-profile-metadata`.

The RED steps were observed before implementation: the configuration suite
failed to compile because `HostProfile` had no metadata fields or bounds, and
the new HostService action scenarios were rejected because their operations and
dispatch branches did not exist. GREEN added the bounded v2 codec, lazy v1
migration, metadata operations/actions, and radio-independent failure handling.

The delivered maximum serialized record is 10,255 bytes: 15 global bytes plus
16 profiles of at most 640 bytes each. Native tests cover a maximally populated
round trip, syntactically valid unknown identifiers, invalid lengths/counts and
duplicates, every non-empty truncation of representative v1/v2 records, lazy
upgrade and failed writes, Action validation/idempotence, ready-connection
isolation, pre-start radio isolation, and preservation/defaults through import,
pairing, rename, deletion, and reload.

Physical interaction was not performed because the change has no UI, mapping,
or radio behavior. This does not complete any remaining plan 017 physical
acceptance item.

Verification results:

* focused `test_host_configuration`: 9/9 passed;
* focused `test_host_service`: 22/22 passed;
* `make format` and `make format-check`: passed;
* `make lint`: passed with zero high/medium findings and three pre-existing low
  style findings outside this change;
* `make test`: 44/44 Python and 226/226 native C++ tests passed;
* `make check`: passed, including lock verification and the repeated host and
  firmware checks;
* ESP-IDF 5.5.5 `make build`: passed for ESP32-S3; the application image is
  785,232 bytes with 77% of the smallest application partition free;
* an isolated build of updated `main` produced 780,800 bytes, so plan 018 adds
  4,432 bytes to the production image.
