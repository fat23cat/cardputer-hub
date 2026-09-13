# Synthesized Key Feedback and Persistent Volume Plan

Status: **Software implemented; physical audio acceptance pending**

This plan owns the sound work layered after plan 018 and pull request #24.

```text
branch:   feat/021-key-feedback-volume
PR title: [021] Add key feedback and persistent volume
```

## 1. Dependency and Scope

Pull request #24 (`feat/018-host-profile-metadata`) must merge first. The sound
branch is then rebased onto updated `main`, retaining plan 018's complete
metadata and version-2 migration tests.

This change adds:

* a hardware-independent audio adapter contract;
* an AudioService for cue selection and persistent volume;
* bounded, precomputed synthesized key and directional-step clips;
* asynchronous Cardputer-Adv playback through M5Unified;
* a Settings row below Bluetooth with 0-100% volume in 10% steps;
* a version-3 configuration record that preserves plan 018 metadata and adds
  sound volume;
* lazy version-1 and version-2 migration to version 3 on the next successful
  configuration mutation.

Boot/status cues, display-power behavior, arbitrary sound themes, recorded
samples, and host-side audio are not part of this plan.

## 2. Ownership and Isolation

ApplicationShell recognizes UI input and dispatches the logical
`audio.volume.step` Action. AudioService owns validation, clamping, persistence,
cue selection, and retry behavior. CardputerAudioAdapter alone owns M5Unified
speaker configuration and playback.

`SystemConfiguration` composes a host-only `HostConfiguration` and system sound
volume. HostService exposes only the host slice; AudioService cannot control
Bluetooth, and host consumers cannot observe or mutate sound settings through
HostService.

Every volume delta is computed only after the authoritative configuration has
loaded. A Settings volume Action retries speaker startup after a transient boot
failure without allowing an unread or corrupt record to be overwritten.
Configuration saves are rejected until the shared Service has loaded, which
also prevents a candidate copied from pre-load defaults from replacing a valid
existing record.

## 3. Behavior

Each debounced semantic key press after the splash handoff requests one short
key click. Eight deterministic variants rotate without runtime synthesis. A new
interface cue replaces the previous one instead of building a queue. Every key
clip fades to digital silence before its fixed buffer ends so the speaker does
not receive an abrupt end-of-buffer transition.

Plain Tab is the only Settings shortcut. Fn+Tab is inactive, and normal G0 has
no application action; G0 plus reset retains its hardware download behavior.

Settings places Sound volume immediately below Bluetooth. Up/Down changes row
focus; Left/Right changes volume by 10%. Values clamp at 0 and 100, zero mutes
playback, and raising zero to ten uses the new audible level.

## 4. Persistence Compatibility

The binary record keeps the `HUBH` prefix and existing storage address:

* version 1 loads host fields with absent metadata and 60% volume;
* version 2 loads all plan 018 metadata and 60% volume;
* version 3 stores the same metadata plus sound volume;
* loading legacy data performs no write;
* the next successful mutation writes version 3;
* invalid, truncated, unreadable, and future-version records remain preserved;
* failed writes do not publish the candidate value.

Firmware predating version 3 cannot read a record after it has been upgraded.
Installation documentation must retain this downgrade warning.

## 5. TDD and Verification

The observed RED regression reproduced a transient startup read failure with a
stored 90% volume: the old Action path computed Right from the uninitialized
60% default and wrote 70%. GREEN loads first, retries the adapter, and writes
100%. A second RED regression showed that `save()` could load a valid record
and then write a candidate copied from defaults before that load; GREEN now
rejects every save attempted before a successful explicit load.

Automated coverage includes:

* clip bounds, deterministic variants, mute, directional cues, and playback
  replacement requests;
* Action validation, clamping, persistence failure, and startup recovery;
* Settings focus, partial redraw, 0-100% stepping, and one click per key event;
* v1/v2/v3 round trips, truncation, malformed metadata, lazy migration, and
  failed-write preservation;
* HostService metadata idempotence, failure handling, capacity, connection
  isolation, and preservation of sound volume.

Required before completion:

```text
make format-check
make lint
make test
make build
make check
```

Physical Cardputer-Adv acceptance must confirm perceived cue timing and level,
rapid-key replacement, mute/unmute, and that playback does not disturb BLE,
input, or animation. Until then this plan must not claim physical audio
acceptance.
