# 043 — Lockstep Companion protocol with build identity

## Current status

Software implemented on branch `claude/043-companion-lockstep-protocol`
(from plan 042). `make check` and `make companion-check` pass; physical
acceptance (section 9) is pending.

Delivered as planned: one wire format with marker `0xC7`, HELLO / HELLO_ACK
with fingerprint and build ID, `Incompatible` firmware state with
`UPDATE COMPANION` / `UPDATE FIRMWARE` / `REBUILD BOTH` on Home, the
COMPANION-only capability, build identity on both sides (SYSTEM row `BUILD`,
Companion menu **Builds**), one fixture set and rewritten documentation.

Deviations:

* The fingerprint hashes the generator source only. The fixtures are derived
  from that source and `hello.bin` contains the fingerprint, so hashing them
  would be circular; the consistency test is
  `test_python/test_companion_fixtures.py`.
* The build ID has no `·`: `2026-09-29 abc1234`, as in section 3.
* No answer to HELLO does not stop the Mac: it keeps the existing reconnect
  retries and keeps the no-answer notice until a HELLO_ACK arrives, so a
  firmware update connects without pressing Reconnect. A mismatch does stop
  retries (handled in `AppDelegate`'s handshake watchdog, not
  `CompanionCentral`).
* Beyond dropping schema bytes, the CPU details group lost its load-average
  field and the network group its reserved VPN bit, which plan 042's polish
  had already removed from the screens.
* SYSTEM row 07 is `BUILD` (the build ID) instead of `VERSION`.

---

## 1. Goal

The Cardputer firmware and the macOS Companion are always updated together,
by hand, from the same repository. Compatibility with older peers is not
needed, so the Companion protocol stops negotiating versions:

* one wire format, with no per-version branches, schemas or capability lists;
* one compatibility check when a session starts: both sides must be built from
  the same protocol definition (a **protocol fingerprint**);
* a mismatch is reported clearly on both sides and nothing else is exchanged;
* both sides show a human build identity, `YYYY-MM-DD · <commit>`, so builds
  can be told apart at a glance.

Today the codecs carry protocol versions 1–6, AI_USAGE schemas 1–3,
SYSTEM_METRICS schemas 1–2, version-gated capabilities, per-version fixtures,
and a HELLO that can list only four versions. Plan 042 found one real defect
in that machinery (AI_USAGE schema drift in v6). None of it is used, because
the owner always updates both sides.

## 2. Scope

In scope:

* Firmware and Companion codecs: remove version negotiation, the envelope
  version field as a version, CAPABILITIES, and every payload schema byte.
* A generated protocol fingerprint shared by both codebases, with a check that
  the committed value matches the fixtures.
* HELLO / HELLO_ACK carrying the fingerprint and both build identities; a
  mismatch outcome.
* Build identity (date and commit) for firmware and Companion, refreshed on
  every build.
* Presentation: Cardputer SYSTEM screen, Home connected-device context and the
  Companion menu.
* Mini App gating: MAC CONTROL, MAC STATUS and AI USAGE depend on a ready
  Companion session only.
* One fixture set, tests and documentation rewritten for the single format.

Plans 030–042 are historical records and are not rewritten.

## 3. Architecture

```text
protocol/companion/generate_fixtures.py
        ├─ fixtures/*.bin                      (one set, no -vN names)
        ├─ protocol/companion/companion_fixtures.h
        ├─ src/connectivity/companion/companion_fingerprint.h      (generated)
        └─ companion/macos/Sources/CompanionCore/ProtocolFingerprint.swift (generated)

Mac HELLO { marker, fingerprint[8], companion build id }
        │
        ▼
CompanionService (firmware)
    fingerprint equal ─▶ HELLO_ACK Accepted { session, firmware build id } ─▶ Ready
    fingerprint differs ─▶ HELLO_ACK Mismatch { firmware build id } ─▶ Incompatible
    old v1–v6 HELLO    ─▶ Incompatible (peer = "older Companion"), no reply
```

### Protocol fingerprint

* `generate_fixtures.py` computes SHA-256 over the generator source and the
  fixture bytes (sorted by name) and writes the first 8 bytes into a C++
  header and a Swift file, both committed like `companion_fixtures.h`.
* A Python test regenerates in memory and fails if the committed fingerprint,
  header, Swift file or fixtures differ. Any wire change therefore changes the
  fingerprint without anyone remembering to bump a number.
* Documentation-only edits to `protocol/companion/README.md` do not change it.

### Envelope

The 8-byte envelope stays. Byte 0 becomes a fixed format marker `0xC7`
instead of a version. A frame with byte 0 in `1..6` is recognised as an old
v1–v6 peer. It is never parsed as a current message.

### HELLO / HELLO_ACK

| Message | Payload |
| --- | --- |
| HELLO (Mac → Cardputer) | fingerprint (8), build id length (1, 1–24), build id (ASCII) |
| HELLO_ACK Accepted | fingerprint (8), build id length, firmware build id |
| HELLO_ACK Mismatch (status `UNSUPPORTED`) | fingerprint (8), build id length, firmware build id |

A build id is `YYYY-MM-DD <short commit>`, for example `2026-09-29 2db2c40`,
with a `+` suffix for a dirty working tree.

### Operations after this plan

`PING`, `APP_ACTIVE`, `APP_ACTIVATE`, `APP_ACTIVE_CHANGED`, `SYSTEM_METRICS`,
`SYSTEM_DETAILS`, `AI_USAGE`. `CAPABILITIES` is removed; operation numbers
keep their current values so traces stay readable. Payloads lose their schema
byte: SYSTEM_METRICS is today's schema 2 body (27 → 26 bytes), AI_USAGE is
today's schema 3 body, SYSTEM_DETAILS keeps its group echo. Validity bits stay:
they describe data the Mac cannot read (no battery, Intel CPU), not peer
features.

### Mini App gating

`CompanionService` registers only `COMPANION` when a session is Ready. MAC
CONTROL, MAC STATUS and AI USAGE require `COMPANION`; the `APP_*`,
`SYSTEM_METRICS`, `SYSTEM_DETAILS` and `AI_USAGE` capability IDs and
`CompanionCapabilitySummary` are deleted. `supportsSystemDetails()` and
similar checks become `hasLiveCompanion()`.

### Build identity

* Firmware: `BuildInfo` gains `buildDate`. `main/CMakeLists.txt` currently
  reads the commit with `execute_process` at configure time only, so it goes
  stale after later commits. Date, commit and dirty flag move to a small
  generated source that is refreshed on every build (a custom command that
  always runs and rewrites the file only when its content changes). An
  environment override (`CARDPUTER_HUB_BUILD_DATE`) keeps release builds
  reproducible.
* Companion: `scripts/package_macos_companion.sh` writes `CFBundleVersion` =
  build id and `CFBundleShortVersionString` = date. `CompanionCore` reads its
  build id from the bundle, with a `dev` fallback for `swift run`.

## 4. Ownership & Boundaries

| Component | Owns | May call | Must not call |
| --- | --- | --- | --- |
| `generate_fixtures.py` | fixtures, fingerprint, generated header and Swift file | — | runtime code |
| connectivity `companion_protocol` | envelope marker, HELLO/ACK codec, old-peer detection | fingerprint constant | services, build info |
| `CompanionService` | fingerprint comparison, `Incompatible` state, peer build id | transport, `BuildInfo` | UI |
| `core::BuildInfo` | firmware version, date, commit, dirty flag | — | services |
| SYSTEM app, Home context | presenting firmware build and a Companion mismatch | `CompanionService` status, `BuildInfo` | protocol codec |
| `CompanionSession` (Mac) | HELLO with fingerprint, ACK outcome, peer build id | `CompanionCodec` | CoreBluetooth |
| `CompanionCentral` (Mac) | not retrying or liveness-reconnecting while Incompatible | session state | codec internals |
| `CompanionStatusStore` / menu | presenting both build ids and the mismatch | status snapshot | session internals |

## 5. State Model

Firmware `CompanionServiceState`:

```text
Unavailable ─transport ready─▶ Attaching ─HELLO match─▶ Handshaking ─▶ Ready
                                   │
                                   ├─HELLO fingerprint differs─▶ Incompatible(peer build id)
                                   └─old v1–v6 frame──────────▶ Incompatible(older Companion)
Incompatible ─new matching HELLO─▶ Handshaking
any ─transport lost─▶ Unavailable (peer build id cleared)
```

While Incompatible no capability is published, no PING is sent and every
non-HELLO message is ignored rather than treated as a protocol error.

Mac `CompanionSession`:

```text
idle ─startHandshake─▶ awaitingAck ─ACK Accepted─▶ ready
                                   ├─ACK Mismatch─▶ incompatible(firmware build id)
                                   └─no answer 5 s─▶ noAnswer ("firmware older than this Companion?")
incompatible / noAnswer ─Reconnect─▶ awaitingAck
```

The liveness reconnect from plan 042 runs only in `ready`. `incompatible`
does not retry on its own, so a mismatch does not reconnect every few seconds.

Suggested advice uses the build dates. The older side is named (`UPDATE
COMPANION` or `UPDATE FIRMWARE`). With equal dates and different fingerprints
the advice is `REBUILD BOTH`.

## 6. BDD Scenarios

### Scenario: Matching builds connect (happy path)

Given:
- firmware and Companion built from the same commit

When:
- the Companion attaches

Then:
- HELLO carries the fingerprint and the Companion build id
- HELLO_ACK is Accepted with the firmware build id
- `COMPANION` is published; MAC CONTROL, MAC STATUS and AI USAGE appear
- the Companion menu shows both build ids; SYSTEM shows the firmware build id

### Scenario: Companion older than firmware (mismatch)

Given:
- the firmware fingerprint differs from the Companion's
- the Companion build date is earlier

When:
- the Companion sends HELLO

Then:
- the firmware answers HELLO_ACK Mismatch and enters Incompatible
- Home shows `UPDATE COMPANION`; no Companion app is listed
- the Companion menu shows `Cardputer <firmware build id> — update this Companion`
- the Companion does not retry until Reconnect or relaunch

### Scenario: Firmware older than Companion (mismatch)

Given:
- differing fingerprints; the firmware build date is earlier

Then:
- Home shows `UPDATE FIRMWARE`; the Companion menu says to update the firmware

### Scenario: A v1–v6 Companion attaches to new firmware (unavailable dependency)

Given:
- a Companion from before this plan sends a version-list HELLO

When:
- the firmware receives it

Then:
- the frame is recognised by its version byte and the firmware enters
  Incompatible with `UPDATE COMPANION`; no protocol error loop

### Scenario: New Companion attaches to v1–v6 firmware (in-flight failure)

Given:
- firmware from before this plan

When:
- the Companion sends the new HELLO

Then:
- the old firmware cannot decode it and does not answer
- after 5 seconds the Companion shows `No answer — Cardputer firmware may be
  older than this Companion` and stops retrying

### Scenario: Update one side and reconnect (reconnect)

Given:
- Incompatible after a mismatch

When:
- the user installs the matching Companion, or flashes matching firmware, and
  the Companion attaches again

Then:
- the new HELLO matches and the session becomes Ready without a Cardputer
  reboot

### Scenario: Two Macs with different builds (host switch)

Given:
- Mac A matches the firmware, Mac B does not

When:
- Cardputer switches from Mac A to Mac B

Then:
- Mac A's session and apps are cleared as today
- Mac B leads to Incompatible with its own advice; switching back to Mac A
  restores a Ready session

Repeated input and stale completion are unaffected: they concern requests
after a Ready session and keep the existing coverage.

## 7. Architecture Invariants

- The only compatibility decision is a fingerprint equality check in HELLO;
  no code outside the HELLO path reads a version or schema.
- The fingerprint is generated, never edited by hand, and a test proves the
  committed value matches the fixtures.
- An Incompatible session exchanges no messages except HELLO / HELLO_ACK and
  publishes no capability.
- A frame whose first byte is not `0xC7` is never decoded as a current message.
- Build ids are ASCII, at most 24 bytes, and contain no user or host names.
- The Mac never auto-reconnects while Incompatible.
- Firmware build date and commit are refreshed on every build, not only on
  CMake configure.

## 8. Tests mapped to scenarios

| Scenario | Test |
| --- | --- |
| Fingerprint matches fixtures | `test_companion_fingerprint.py`: regenerate in memory and compare with the committed header, Swift file and fixtures |
| Envelope marker, old-frame detection | `test_old_version_frames_are_recognised_not_decoded` (test_companion_protocol) |
| HELLO / ACK codec | `test_hello_and_ack_carry_fingerprint_and_build_id` |
| Matching builds connect | `test_matching_fingerprint_reaches_ready_and_publishes_companion` (test_companion_service) |
| Mismatch in both directions | `test_mismatch_enters_incompatible_with_peer_build_id` |
| Old Companion HELLO | `test_old_hello_enters_incompatible_without_protocol_error` |
| Incompatible ignores traffic | `test_incompatible_ignores_requests_and_sends_no_ping` |
| Update one side and reconnect | `test_matching_hello_recovers_from_incompatible` |
| Host switch | `test_host_switch_clears_peer_build_id` |
| Apps gated by COMPANION only | `test_mac_status_requires_live_metric_capability` and the AI USAGE / MAC CONTROL equivalents, updated |
| Advice from build dates | `test_mismatch_advice_names_the_older_side` (Home / SYSTEM presentation) |
| Mac session outcomes | CompanionCoreCheck: accepted, mismatch, and no-answer outcomes; no liveness reconnect while incompatible |
| Build id format | CompanionCoreCheck and a firmware `BuildInfo` test: date, short commit, `+` when dirty, ≤ 24 ASCII bytes |
| Build info refresh | `test_esp_idf_build_config.py`: the generated build-info source is regenerated on every build |

## 9. Physical Acceptance

- Flash firmware and install the Companion from the same commit: connection,
  MAC CONTROL, MAC STATUS and AI USAGE work; the menu and SYSTEM show the same
  commit.
- Rebuild only the Companion after a protocol edit: Cardputer shows `UPDATE
  FIRMWARE`, the menu names the firmware build, nothing reconnects in a loop.
- Flash new firmware with a pre-043 Companion running: `UPDATE COMPANION`
  on Cardputer; installing the new Companion connects without a reboot.
- Two Macs with different builds: switching hosts shows the right advice for
  each.

## Implementation order

1. Build identity on both sides (usable on its own).
2. Fingerprint generation, committed outputs and the consistency test.
3. Single-format codecs and fixtures on both sides (firmware and Swift
   together, one commit).
4. `CompanionService` Incompatible state and COMPANION-only gating.
5. Mac session outcomes, retry and liveness rules, menu presentation.
6. Cardputer SYSTEM and Home presentation.
7. Documentation: `protocol/companion/README.md` rewritten as one format,
   `docs/ARCHITECTURE.md` §35, `docs/UI_REQUIREMENTS.md`,
   `docs/manuals/device-guide.md` (update both sides together, what the
   mismatch messages mean), `companion/macos/README.md`, `docs/ENGINEERING.md`
   §21 (build date), this plan and `docs/plans/README.md`.

Final gate: `make check` and `make companion-check`.

## 10. Out of Scope

- Automatic updates or update prompts beyond the mismatch message.
- Changes to the CRUB manager, release packaging or firmware file names.
- Renumbering operations or redesigning payloads beyond removing schema bytes.
- Keeping any compatibility with v1–v6 peers beyond recognising them.
- Rewriting plans 030–042.
