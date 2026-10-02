# 044 — NFC Reader Prototype (Transport Scope Superseded)

Status: **Superseded by plan 045; transport prototype removed, U216 foundation kept**

Suggested branch:

```text
feat/044-nfc-reader
```

Suggested PR:

```text
[044] Add NFC reader and transport card profiles
```

Suggested plan file:

```text
docs/plans/044-nfc-reader.md
```

## Current status

On 2026-10-02 the product direction changed to a personal inventory using
ordinary writable NFC tags. [Plan 045](045-nfc-inventory.md) owns the new
behavior. The transport profiles, generic MIFARE Classic sector inspector,
Classic authentication, details pages and serial card-data diagnostics
described below were removed from code, tests and architecture checks under
plan 045; they are a historical record, not requirements.

The hardware foundation is reusable: the Cardputer-Adv detected the U216
through Grove after the startup probe fix, the NFC capability became live, and
the NFC Mini App detected a real NFC-A card. Plan 045 will keep the verified
reader startup, pin arbitration, capability/lifecycle boundaries and relevant
hardware-neutral tests, while replacing the card-profile behavior. The
remaining sections record the original prototype design and observed results;
they are superseded where they conflict with plan 045.

The prototype was software implemented on branch `feat/044-nfc-reader`. The component checks
pass: 98 Python tests (one skipped), 738 native tests, clang-format, cppcheck,
the architecture check (182 files) and the ESP-IDF production build with its
effective-configuration check. `make check` passed with the installed
PlatformIO 6.2 used for `pio`; the default `uv` PlatformIO 6.1.19 still stops
in cppcheck package setup on this development machine.
Physical acceptance (section 14) is **pending**: no Unit NFC, EMT card
or Consorcio card was available during implementation, so the ST25R3916 adapter
compiles and links but had not driven real hardware. On 2026-10-02, a Unit NFC
U216 connected to the Cardputer-Adv Grove port before boot was not detected by
the initial firmware (`NfcService: reader not present`). The initial address
probe ran before the vendor library's 50 ms power-on wait and retries. Startup
probing now uses the same settle time and five bounded attempts. After flashing
that build to the CRUB `hub` partition, serial output reported
`NfcService: reader ready` and the NFC app's capability indicator turned green.
The user's EMT Málaga Súbete card was then identified as MIFARE Classic 1K
(NFC-A, 16 sectors). All 16 sectors remained protected with the only
configured factory-default key, so no card data or trip count was available
to decode. The card's data layout has not been verified;
transport decoding and the remaining physical acceptance are pending.

Delivered:

* **044-A foundation.** `INfcReader` and the normalized card, activation and
  operation-result types (`src/core/nfc`), `St25r3916Adapter`
  (`src/hardware/nfc`), the `NFC_READER` capability, `NfcService`, the profile
  registry and `GenericNfcProfile`, and the `NFC` Mini App (waiting screen,
  card summary, two details pages) registered through the normal
  AppRegistry/MiniAppRuntime path and the Launcher.
* **044-B MIFARE Classic inspector.** Classic geometry (Mini, 1K, 2K, 4K),
  explicit-credential sector authentication, the authenticated / readable /
  protected snapshot model, `GenericMifareClassicProfile`, and fixture-style
  snapshot tests that run without hardware. The readable-sector count requires
  a successfully read data block; an authenticated sector with no readable
  blocks is neither readable nor key-protected. A failed Classic selection is
  retried after an RF-field reset with the saved UID.
* **044-C / 044-D transport slots.** `EmtMalagaProfile` and
  `ConsorcioMalagaProfile` exist, are registered ahead of the generic profiles,
  and run on a shared rules engine (`NfcTransportRules`). **Verified support
  level today: none.** `emtMalagaVerifiedRules()` and
  `consorcioMalagaVerifiedRules()` return no rules, so both profiles are inert,
  never claim a card, and no balance, trip count or other field is shown. No
  layout was guessed. Rules are accepted only with a card family, a read map and
  at least one data marker that the read map actually covers, and they contain
  no UID, so a UID alone can never identify a card. Sections 7–9 (capture,
  diff, verify, decode, fixtures) remain to be done on the user's own cards.
  A recognition marker cannot refer to a sector trailer, which the reader never
  exposes to profiles.
* **Research mode.** Section 7's diagnostics are structured serial lines
  (`nfc.diag card|sector|block`) with the UID and raw readable blocks, switched
  on with `D` in the NFC app, off by default, never stored, and turned off when
  the app closes. Keys for protected sectors of the user's own cards are
  supplied in code (the credentials handed to `GenericMifareClassicProfile` in
  `main.cpp`, or a profile's verified rules); there is no runtime key entry or
  key storage in this plan.

Dependency evaluation (implementation step 1):

| Question | Result |
| --- | --- |
| Library | M5Stack `M5Unit-NFC` 0.1.2 (MIT) with `M5UnitUnified` 0.5.8, `M5HAL` 0.1.4 and `M5Utility` 0.3.1. None is on the ESP component registry (their manifests float `main` through Git), so all four are Git submodules under `components/` pinned to exact tags, each with an explicit-source CMake wrapper. |
| Alternatives | ST's RFAL (separate license, large) and a driver written from the datasheet (large and untestable here) were rejected. |
| Build | Compiles under ESP-IDF 5.5.5 beside M5Unified 0.2.21. Only the new I2C master, RMT and ADC drivers are linked (`nm` shows no `i2c_driver_install`, `rmt_driver_install` or legacy ADC symbols), matching M5GFX and `led_strip`, so there is no legacy/new driver conflict. |
| Size | Image 1,337,264 bytes against 1,236,288 before (+100,976); the CRUB `hub` partition holds 2 MiB. `libm5unitnfc` 38 KB, `libm5unitunified` 13 KB. |
| Logging | The libraries print identifiers (an NFC-B PUPI) at error level, so their logging is compiled out (`M5_LOG_LEVEL=0`); card data reaches the console only through the opt-in diagnostics. |
| Hardware | Unit NFC (SKU U216) is I2C address 0x50 on Grove port A; Grove has no IRQ line, so the library polls the interrupt registers. |

Hardware finding: Grove port A (SCL G1, SDA G2) is also Unit Puzzle's data pin
(GPIO 2), and the firmware drove that pin low at boot even without a Puzzle.
The two cannot share the port. `main.cpp` now starts `NfcService` before the
Puzzle: the adapter probes the Unit NFC address and releases the bus if nothing
answers; a reader that answered keeps the pins and
`PuzzleWs2812Adapter::inhibit()` leaves the Puzzle off for the session.
The address probe waits 50 ms after I2C startup and retries up to five times
at 20 ms intervals, as the vendor's chip-identity probe does, before deciding
that the Puzzle may claim the port.

Deviations from this plan:

* `INfcReader` and the normalized card types are in `src/core/nfc/`, not
  `src/hardware/nfc/`: Services may not include hardware headers, and adapter
  interfaces (audio, LED, battery) already live in Core.
* Profile order is structural. Profiles declare a tier (specific, MIFARE Classic
  family, generic fallback) and the registry orders candidates by it, so a
  specific profile precedes a generic one whatever the registration order.
  Recognition is two-stage: `matches()` is an identification-level candidate
  test, and `decode()` confirms or rejects from the data read; a rejected
  candidate hands over to the next with a fresh snapshot.
* Section 6.5's `Escape: details → summary` needed a shell change:
  `IMiniApp::handleBack()` (default false) is offered the plain Escape key
  through `MiniAppRuntime::handleBack()` before the shell closes the app.
* The generic Classic profile tries exactly one key per sector, the NXP
  transport-configuration Key A (`FFFFFFFFFFFF`), supplied explicitly by the
  composition root; a sector that does not open is protected and nothing else
  is tried.
* A reader that did not answer at start is never probed again, because its pins
  may belong to the Puzzle; one that answered is re-probed every two seconds
  after a loss. A unit connected after power-on is therefore not found until the
  next reset.
* The service performs one reader operation per `update()`, polls every
  150 ms only while the app wants scanning, and verifies presence every 300 ms.
  A result for an older activation is discarded (repeated ones reset the
  reader); one for a newer activation ends the session.
* NFC-B, NFC-F (FeliCa) and NFC-V (ISO 15693) are detected and identified by
  rotating polling (A, B, A, F, A, V) and a per-technology presence check. They
  are the least validated paths.
* Text is ASCII (`MALAGA`), and the waiting screen has no animation, as the UI
  requirements ask for event-driven motion only.
* `scripts/check_architecture.py` also enforces the NFC rules from section 12
  that a layer matrix cannot express (vendor library confinement, read-only
  reader surface, no write/emulation/key-search APIs, no profile or app reader
  access, no persistence).
* The vendor library still contains its own write and emulation code. The
  project never calls it and `INfcReader` cannot reach it; section 12's
  invariants apply to project code.

Definition of done (section 20):

| Item | State |
| --- | --- |
| ST25R3916 behind a project-owned abstraction | Done |
| `NFC_READER` through CapabilityRegistry | Done |
| NFC Mini App through AppRegistry / MiniAppRuntime | Done |
| Generic card detection on physical hardware | NFC-A / MIFARE Classic 1K observed; other technologies pending |
| Normalized card information shown | Done (host-tested) |
| MIFARE Classic inspected read-only | Done (host-tested; physical pending) |
| Specific profiles before generic | Done |
| EMT / Consorcio profiles with documented support level | Done: level **none verified** |
| Protected data fails safely; removal clears the session; stale reads discarded | Done |
| No write / clone / emulation / key-search paths | Done in project code |
| Native tests, static checks, firmware build | Pass |
| Physical ST25R3916 acceptance | **Pending** |
| Documentation | Done |

Physical acceptance to run, in addition to section 14: the Puzzle/NFC port
arbitration (NFC Unit attached: Puzzle stays dark; Puzzle attached and no NFC
Unit: Puzzle behaves as before); pulling the Unit out while NFC is open and
plugging it back in; the time to read a 1K and a 4K card; and NFC-B, FeliCa and
ISO 15693 detection with any tags available.

---

# 1. Goal

Add a production Mini App:

```text
NFC
```

for the optional M5Stack NFC Universal Unit based on:

```text
ST25R3916
```

The application provides one universal NFC reader instead of separate Mini Apps for individual cards.

Initial targets:

```text
generic NFC cards/tags
MIFARE Classic
EMT Málaga transport card
Consorcio de Transporte Metropolitano Área de Málaga card
```

The architecture must allow additional card types and application-specific decoders to be added later without changing the generic NFC UI or hardware layer.

Primary architecture:

```text
NfcApp
   ↓
NfcService
   ↓
Card identification
   ↓
Profile registry
   ├── Generic profile
   ├── EMT Málaga profile
   ├── Consorcio Málaga profile
   └── future profiles
   ↓
INfcReader
   ↓
ST25R3916 adapter
```

Plan 044 is intentionally **read-only**.

It must not introduce:

```text
card writing
card cloning
card emulation
transport-card modification
balance modification
key brute forcing
automatic attacks against protected sectors
```

---

# 2. Scope

## Included

### Hardware support

Add support for the external:

```text
M5Stack NFC Universal Unit
ST25R3916
```

through a project-owned NFC hardware abstraction.

The application must not depend directly on the M5Stack/ST25R3916 library.

Conceptually:

```text
M5Unit-NFC / ST25R3916 library
              ↓
      St25r3916Adapter
              ↓
          INfcReader
              ↓
          NfcService
```

---

### NFC capability

Introduce an NFC reader capability, for example:

```text
NFC_READER
```

The capability represents a functional NFC reader, not merely compiled NFC support.

When the NFC Unit is unavailable:

```text
Launcher
NFC             unavailable
```

When detected and initialized:

```text
Launcher
NFC             available
```

Loss of the NFC reader while NFC is active follows the existing Mini App capability-loss behavior and returns to Launcher.

---

### Generic NFC detection

The service must initially expose common identification information where available:

```text
technology
protocol
UID
ATQA
SAK
card family/type
memory/card-size metadata when known
```

Example:

```text
NFC-A

MIFARE CLASSIC 1K

UID
04 A8 12 93

ATQA   00 04
SAK    08
```

The exact fields depend on the detected technology.

Missing fields must remain absent rather than displaying fabricated or zero-filled data.

---

### Initial NFC families

The architecture must not be MIFARE-Classic-specific.

Plan 044 should support detection/identification for the protocols already exposed by the selected ST25R3916 library where practical, including:

```text
NFC-A
NFC-B
ISO15693
FeliCa
```

Detailed parsing is initially required only where implemented.

For unsupported higher-level formats:

```text
card detected
technology identified
raw identification shown
decoder unavailable
```

is valid behavior.

---

### MIFARE Classic

Provide explicit MIFARE Classic support through `NfcService`.

The service may expose:

```text
Classic card type
sector count
block count
authenticated/readable blocks
unavailable/protected blocks
```

Authentication must use only explicitly supplied/configured keys or keys belonging to a supported known profile.

Plan 044 must not implement automatic key discovery or brute forcing.

---

### Card profiles

Introduce a profile abstraction conceptually similar to:

```text
INfcCardProfile
```

Possible API shape:

```cpp
class INfcCardProfile {
public:
    virtual bool matches(const NfcCardInfo& card) const = 0;

    virtual ProfileReadRequest readRequest(
        const NfcCardInfo& card) const = 0;

    virtual DecodedCardData decode(
        const NfcCardSnapshot& snapshot) const = 0;
};
```

Exact names are implementation details.

Profiles own:

```text
recognition logic
required/readable data requests
application-specific decoding
presentation metadata
```

Profiles do not own:

```text
RF communication
hardware initialization
polling lifecycle
UI rendering
global storage
```

---

### Initial profiles

Implement:

```text
GenericNfcProfile
GenericMifareClassicProfile
```

and introduce dedicated profile slots for:

```text
EmtMalagaProfile
ConsorcioMalagaProfile
```

The Málaga profiles may initially remain partially decoded if the card layout has not yet been established.

A profile must be able to report:

```text
recognized
partially supported
supported
```

without pretending unknown fields are known.

---

# 3. Architecture

## 3.1 Dependency direction

Follow the existing project rule:

```text
Mini App
   ↓
Service
   ↓
Hardware abstraction
   ↓
Hardware adapter
```

For NFC:

```text
NfcApp
   ↓
NfcService
   ↓
INfcReader
   ↓
St25r3916Adapter
   ↓
M5Unit-NFC/ST25R3916 library
```

Card decoders are domain logic and belong above the adapter:

```text
                  NfcService
                      │
             NfcProfileRegistry
              /       |       \
             /        |        \
        Generic      EMT     Consorcio
```

They must never talk to ST25R3916 directly.

---

## 3.2 Suggested source layout

Conceptually:

```text
src/
├── apps/
│   └── nfc/
│       ├── nfc_app.h
│       └── nfc_app.cpp
│
├── services/
│   └── nfc/
│       ├── nfc_service.h
│       ├── nfc_service.cpp
│       ├── nfc_models.h
│       ├── nfc_profile.h
│       ├── nfc_profile_registry.h
│       ├── nfc_profile_registry.cpp
│       └── profiles/
│           ├── generic_nfc_profile.*
│           ├── mifare_classic_profile.*
│           ├── emt_malaga_profile.*
│           └── consorcio_malaga_profile.*
│
└── hardware/
    └── nfc/
        ├── nfc_reader.h
        ├── st25r3916_adapter.h
        └── st25r3916_adapter.cpp
```

Exact directory names may follow existing repository conventions discovered during implementation.

---

## 3.3 Reader abstraction

`INfcReader` owns hardware-facing operations such as:

```text
initialize
poll for card
identify card
perform supported read command
authenticate supported MIFARE Classic sector
read block
detect card removal
```

It must expose normalized project-owned data structures.

No M5Stack/ST25R3916 library types may escape the adapter boundary.

---

## 3.4 NfcService

`NfcService` owns the NFC scanning lifecycle.

Conceptual states:

```text
Unavailable
Initializing
Idle
CardDetected
Reading
Ready
Error
```

Typical lifecycle:

```text
Idle
 ↓
card enters field
 ↓
CardDetected
 ↓
identify
 ↓
select profile
 ↓
request required data
 ↓
Reading
 ↓
decode
 ↓
Ready
```

When the card leaves:

```text
Ready
 ↓
card removed
 ↓
Idle
```

---

## 3.5 Snapshot model

Separate physical card reading from decoding.

Conceptually:

```text
NfcCardInfo
```

contains identification:

```text
technology
UID
ATQA
SAK
card type
```

while:

```text
NfcCardSnapshot
```

contains data obtained during the current read:

```text
card info
read blocks/pages
access status
authentication status
read errors
```

Profiles decode a snapshot.

This allows captured test fixtures to be used without NFC hardware.

---

## 3.6 Profile registry

Profiles are evaluated in deterministic order.

Example:

```text
EMT Málaga
Consorcio Málaga
Generic MIFARE Classic
Generic NFC
```

Specific profiles therefore win over generic profiles.

The matcher must not identify EMT or Consorcio purely from UID.

Matching may consider:

```text
technology
card family
ATQA
SAK
memory geometry
known readable data markers
known application structures
```

UID may be part of diagnostics but must not be the sole profile discriminator.

---

# 4. Ownership & Boundaries

| Component | Owns | May call | Must not call |
| --- | --- | --- | --- |
| `ApplicationShell` / runtime | service lifecycle integration | `NfcService::update()` | ST25R3916 directly |
| `St25r3916Adapter` | physical reader communication | NFC hardware library | UI, profiles |
| `NfcService` | scanning, card session, normalized snapshot | `INfcReader`, profile registry | render UI |
| `NfcProfileRegistry` | deterministic profile selection | registered profiles | hardware |
| NFC profiles | recognition and decoding | snapshot/model helpers | reader hardware, UI |
| `NfcApp` | user interaction and presentation | `NfcService` read-only state | ST25R3916, direct authentication/read commands |

Important ownership rule:

```text
NfcApp never drives the ST25R3916 polling loop.
```

The Service owns NFC lifecycle.

---

# 5. State Model

## Reader/service state

```text
UNAVAILABLE
     │
     │ reader available
     ▼
INITIALIZING
     │
     ▼
    IDLE
     │
     │ card detected
     ▼
 DETECTED
     │
     ▼
  READING
   │     │
   │     └──── failure ────► ERROR
   ▼
  READY
   │
   │ card removed
   ▼
   IDLE
```

A new card must create a new card session.

---

## Card session

Each detected card receives an internal session identity:

```text
session #42
```

Async/stale results belonging to an earlier card session must not update the currently displayed card.

Example:

```text
Card A
  ↓
read starts

Card A removed

Card B presented
  ↓

late Card A result
  ↓
discard
```

---

# 6. User Experience

Launcher entry:

```text
NFC
```

Internal app id:

```text
nfc
```

---

## 6.1 Waiting screen

When opened without a card:

```text
┌────────────────────────────────────────┐
│ NFC                                    │
│                                        │
│                                        │
│          PRESENT NFC CARD              │
│                                        │
│             )))                        │
│                                        │
│                                        │
└────────────────────────────────────────┘
```

No continuous noisy redraw.

A subtle waiting animation is acceptable.

---

## 6.2 Generic card

Example:

```text
┌────────────────────────────────────────┐
│ NFC                                    │
│                                        │
│ MIFARE CLASSIC 1K                      │
│                                        │
│ UID     04 A8 12 93                    │
│ ATQA    00 04                          │
│ SAK     08                             │
│                                        │
│ ENTER  DETAILS                         │
└────────────────────────────────────────┘
```

---

## 6.3 Recognized EMT card

Target presentation once decoding becomes available:

```text
┌────────────────────────────────────────┐
│ NFC                                    │
│                                        │
│ EMT MÁLAGA                             │
│ MIFARE CLASSIC 1K                      │
│                                        │
│ BALANCE                    7.34 EUR    │
│ TRIPS                           8      │
│                                        │
│ ENTER  DETAILS                         │
└────────────────────────────────────────┘
```

Only fields actually decoded from verified card data are displayed.

If only recognition works:

```text
EMT MÁLAGA

CARD RECOGNIZED

BALANCE
NOT DECODED
```

is preferable to guessing.

---

## 6.4 Consorcio card

Target:

```text
┌────────────────────────────────────────┐
│ NFC                                    │
│                                        │
│ CONSORCIO MÁLAGA                       │
│ MIFARE CLASSIC                         │
│                                        │
│ BALANCE                   12.08 EUR    │
│                                        │
│ ENTER  DETAILS                         │
└────────────────────────────────────────┘
```

Again, unsupported fields remain absent.

---

## 6.5 Details

`Enter` opens a details page.

Possible first page:

```text
CARD INFO                       1/2

PROFILE
EMT MÁLAGA

TYPE
MIFARE CLASSIC 1K

UID
04 A8 12 93

ATQA 00 04
SAK  08
```

Second page may show read status:

```text
CARD DATA                       2/2

SECTORS          16
READABLE          4
PROTECTED        12

PROFILE
PARTIAL
```

Raw sector/block dump does not need to be part of the normal UI.

---

## Controls

```text
Enter
details / next details page

← / →
navigate details pages

Escape
details → card summary
summary/waiting → Launcher
```

Card removal while viewing details returns to:

```text
PRESENT NFC CARD
```

rather than keeping stale personal card data on screen indefinitely.

---

# 7. Transport Card Research Mode

The implementation needs a controlled way to establish the layout of the user's own EMT and Consorcio cards.

Do not mix this with the normal polished card screen.

Provide development diagnostics through one of:

```text
structured serial logs
test-only/debug build
explicit diagnostics screen
```

The diagnostics should be able to report:

```text
card technology
UID
ATQA
SAK
Classic geometry

sector/block number
readable
protected
authentication success/failure
raw bytes for explicitly readable blocks
```

Sensitive dumps must not be written permanently by default.

---

## Research workflow

For each real card:

```text
1. Detect card
2. Identify technology
3. Record geometry
4. Determine which areas are readable
5. Capture legitimate readable snapshots
6. Compare repeated reads
7. Perform real-world card usage/recharge
8. Capture another snapshot
9. Diff snapshots offline
10. Infer candidate fields
11. Verify inference using multiple observations
12. Implement decoder
13. Add fixture tests
```

Example investigation:

```text
EMT snapshot A
balance X

perform one legitimate trip

EMT snapshot B
balance Y

diff
 ↓
candidate blocks change
 ↓
determine counters / balance representation
```

A field becomes part of the production decoder only after its interpretation is repeatable.

---

# 8. EMT Málaga Profile

Create:

```text
EmtMalagaProfile
```

but separate implementation into stages.

## Stage A — Recognition

Determine reliable characteristics of the EMT card.

Success means:

```text
card identified as EMT Málaga
```

without depending solely on UID.

---

## Stage B — Read map

Document:

```text
sector layout
readable sectors
required authenticated sectors
known fields
unknown fields
```

Store this as code/tests/documentation rather than relying on developer memory.

---

## Stage C — Decoder

Candidate values to investigate:

```text
balance
trip/pass count
card/product type
expiry date
last transaction metadata
recharge metadata
```

Only verified fields are implemented.

No assumption that all of these actually exist on the card.

---

# 9. Consorcio Málaga Profile

Create:

```text
ConsorcioMalagaProfile
```

using the same stages:

```text
Recognition
 ↓
Read map
 ↓
Snapshot comparison
 ↓
Verified decoder
```

Candidate fields:

```text
balance
transport-zone/product information
card type
expiry/validity
transaction counters
```

Again, these are research targets, not promised fields.

---

# 10. Generic MIFARE Classic Inspector

Even before EMT/Consorcio decoding is complete, Plan 044 must remain useful.

For a MIFARE Classic card the generic profile should expose:

```text
MIFARE Classic variant
sector count
block count
known/readable sectors
protected sectors
```

This allows the NFC app itself to be used while developing future profiles.

---

# 11. BDD Scenarios

## Scenario: Start NFC with reader attached

Given:

- ST25R3916 is connected
- reader initialization succeeds
- no card is present

When:

- user opens NFC

Then:

- NFC Mini App activates
- waiting screen is shown
- NFC service remains in Idle
- no card data is displayed

---

## Scenario: Generic NFC card detected

Given:

- NFC app is active
- reader is Idle

When:

- an unrecognized supported NFC card enters the field

Then:

- card is detected exactly once
- identification information is read
- generic profile is selected
- card summary is shown
- no application-specific fields are invented

---

## Scenario: EMT card detected

Given:

- NFC app is active
- an EMT profile can positively recognize the presented card

When:

- the EMT card enters the field

Then:

- `EmtMalagaProfile` wins over generic MIFARE profile
- only required readable data is requested
- verified decoded fields are shown
- unknown fields remain unavailable

---

## Scenario: Consorcio card detected

Given:

- NFC app is active
- a Consorcio profile positively recognizes the card

When:

- the card enters the field

Then:

- `ConsorcioMalagaProfile` is selected
- verified fields are decoded
- generic identification remains available in Details

---

## Scenario: Protected MIFARE sector

Given:

- a MIFARE Classic card is present
- requested sector cannot be legitimately authenticated

When:

- the service attempts the profile read

Then:

- the sector is marked protected/unavailable
- scanning continues
- the app remains usable
- no key-search process begins
- available fields are still shown

---

## Scenario: Card removed

Given:

- a card summary is visible

When:

- the card leaves the NFC field

Then:

- current card session is cleared
- decoded card details disappear
- UI returns to `PRESENT NFC CARD`

---

## Scenario: Card replaced quickly

Given:

- card A is being read

When:

- card A is removed
- card B is immediately presented
- a delayed result from card A completes

Then:

- delayed card A result is discarded
- only card B may update current state

---

## Scenario: NFC Unit disappears

Given:

- NFC Mini App is active

When:

- `NFC_READER` capability disappears

Then:

- active NFC operation stops
- current card snapshot is cleared
- Mini App is deactivated
- Launcher becomes visible
- existing capability-loss behavior is preserved

---

## Scenario: Reader initialization fails

Given:

- NFC hardware is connected
- initialization fails

When:

- runtime starts or retries initialization

Then:

- service exposes unavailable/error status
- firmware remains operational
- other Mini Apps remain unaffected

---

## Scenario: Repeated card polling

Given:

- one card remains stationary over the reader

When:

- many reader polling cycles occur

Then:

- the same card is not repeatedly treated as a newly presented card
- profile decoding is not restarted continuously
- UI remains stable

---

# 12. Architecture Invariants

- NFC is one Mini App, not one Mini App per card system.
- Transport systems are NFC profiles/decoders.
- `NfcApp` never accesses ST25R3916 directly.
- NFC library types never escape `St25r3916Adapter`.
- `NfcService` owns reader polling and card-session lifecycle.
- Profiles operate on normalized card models/snapshots.
- Profiles never perform hardware I/O directly.
- Specific card profiles are evaluated before generic profiles.
- UID alone never identifies EMT or Consorcio.
- Unknown data is never presented as decoded data.
- Plan 044 performs no card writes.
- Plan 044 performs no cloning or card emulation.
- Plan 044 performs no key brute forcing.
- Protected sectors remain protected when valid credentials are unavailable.
- Card data is not persistently stored by default.
- Removal of a card clears the active card session.
- Stale read results cannot modify a newer card session.
- Absence/failure of the optional NFC Unit cannot affect unrelated firmware functionality.

---

# 13. Tests Mapped to Scenarios

| Scenario | Test |
| --- | --- |
| Reader available | `test_nfc_service_becomes_idle_after_reader_init` |
| Generic card | `test_unknown_card_uses_generic_profile` |
| EMT recognition | `test_emt_profile_precedes_generic_mifare_profile` |
| Consorcio recognition | `test_consorcio_profile_precedes_generic_mifare_profile` |
| Protected sector | `test_protected_sector_remains_unavailable_without_key_search` |
| Authenticated sector with no readable blocks | `test_authenticated_sector_with_no_readable_blocks_is_not_counted_readable` |
| Unreadable trailer marker | `test_explicit_sector_trailer_cannot_be_a_recognition_marker` |
| Card removal | `test_card_removal_clears_snapshot` |
| Rapid replacement | `test_stale_card_result_is_discarded` |
| Capability loss | `test_nfc_capability_loss_returns_to_launcher` |
| Init failure | `test_reader_failure_does_not_break_runtime` |
| Stationary card | `test_same_card_is_not_redecoded_each_poll` |
| Profile order | `test_specific_profiles_have_priority_over_generic` |
| Hardware boundary | architecture/static dependency test |
| No UI → adapter dependency | architecture/static dependency test |

---

## Profile fixture tests

Transport decoders must primarily use captured sanitized fixtures.

Example:

```text
test/fixtures/nfc/
├── mifare-classic/
├── emt-malaga/
└── consorcio-malaga/
```

Fixtures should contain only the bytes necessary for decoding tests.

Tests should allow:

```text
snapshot fixture
      ↓
profile matcher
      ↓
decoder
      ↓
expected normalized fields
```

without physical NFC hardware.

---

# 14. Physical Acceptance

## Hardware detection

On real Cardputer-Adv:

1. boot with NFC Unit connected;
2. confirm NFC is available;
3. open NFC;
4. remove/disconnect reader where safely supported;
5. confirm failure does not crash firmware.

---

## Generic card

Test with at least one ordinary supported NFC tag/card.

Verify:

```text
detection
UID
technology
card removal
repeated presentation
details navigation
```

---

## MIFARE Classic

Test with a known non-critical MIFARE Classic card/tag.

Verify:

```text
card type
geometry
readable/protected distinction
stable repeated reads
```

---

## EMT Málaga

Using the user's own valid card:

```text
present card
identify profile
capture diagnostic snapshot
remove card
present again
confirm stable identification
```

If balance decoding is implemented:

```text
compare displayed balance with an authoritative known balance
```

Repeat using multiple balances before considering the field verified.

---

## Consorcio Málaga

Perform the equivalent test using the user's own Consorcio card.

Do not mark application-specific decoding complete until values match independent known values across multiple card states.

---

# 15. Documentation

Update:

```text
README.md
docs/ARCHITECTURE.md
docs/plans/README.md
docs/manuals/
```

Document:

```text
supported NFC Unit
connection requirements
supported card technologies
supported profiles
read-only limitation
controls
known profile limitations
diagnostic procedure
```

Architecture documentation should describe NFC generically rather than encoding Málaga-specific knowledge into the system architecture.

---

# 16. Implementation Order

Recommended sequence:

```text
1. ST25R3916 dependency evaluation
2. INfcReader
3. St25r3916Adapter
4. NFC_READER capability
5. normalized NFC models
6. NfcService state machine
7. GenericNfcProfile
8. GenericMifareClassicProfile
9. NfcProfileRegistry
10. NFC Mini App
11. generic physical NFC acceptance
12. EMT diagnostic capture
13. EmtMalagaProfile recognition
14. EMT decoder research + fixtures
15. Consorcio diagnostic capture
16. Consorcio profile recognition
17. Consorcio decoder research + fixtures
18. physical transport-card verification
19. documentation and closeout
```

Important:

```text
generic NFC foundation must work
before Málaga-specific decoding is required
```

This prevents transport-card reverse engineering from blocking the whole feature.

---

# 17. Delivery Milestones

Plan 044 can be developed in clear internal milestones.

## 044-A — NFC Foundation

Deliver:

```text
ST25R3916 adapter
NfcService
NFC_READER capability
card detection
generic NFC info
NFC Mini App
```

At this point the app is already useful.

---

## 044-B — MIFARE Classic Inspector

Deliver:

```text
Classic identification
geometry
authenticated/readable/protected model
generic Classic details
fixture infrastructure
```

---

## 044-C — EMT Málaga

Deliver as knowledge permits:

```text
profile recognition
verified read map
verified decoded fields
fixture tests
physical verification
```

---

## 044-D — Consorcio Málaga

Deliver:

```text
profile recognition
verified read map
verified decoded fields
fixture tests
physical verification
```

These are milestones inside one architectural feature, not separate Mini Apps.

---

# 18. Out of Scope

Explicitly excluded from Plan 044:

```text
writing arbitrary NFC tags
editing transport cards
changing balances
recharging transport cards
MIFARE key brute forcing
dictionary attacks
DarkSide/Nested/HardNested-style attacks
card cloning
UID cloning
transport-card emulation
payment-card transactions
credential/access-card cloning
background NFC scanning
automatic NFC actions while another Mini App is active
saving full card dumps by default
sharing card dumps over Companion
```

NDEF writing can be considered independently later as a benign extension of the generic NFC subsystem.

---

# 19. Future Extensions

The architecture should make later work possible without requiring it now:

```text
NDEF reader
NDEF writer
NTAG tools
ISO15693 inspector
FeliCa inspector
saved user-owned tag names
microSD export of explicitly requested diagnostics
NFC history
Companion-side NFC inspector
additional public transport profiles
```

Potential future architecture:

```text
NFC
├── SCAN
├── DETAILS
├── NDEF
├── TRANSPORT
│   ├── EMT Málaga
│   └── Consorcio Málaga
└── DIAGNOSTICS
```

but Plan 044 should start with one straightforward scan flow rather than turning NFC into a large toolbox immediately.

---

# 20. Definition of Done

Plan 044 is complete when:

```text
[ ] ST25R3916 is behind a project-owned hardware abstraction
[ ] NFC reader availability is represented through CapabilityRegistry
[ ] NFC Mini App is registered through normal AppRegistry/MiniAppRuntime flow
[ ] generic card detection works on physical hardware
[ ] normalized card information is shown
[ ] MIFARE Classic cards can be inspected read-only
[ ] profile registry selects specific profiles before generic profiles
[ ] EMT Málaga profile exists and has documented verified support level
[ ] Consorcio Málaga profile exists and has documented verified support level
[ ] protected data fails safely
[ ] card removal clears current session
[ ] stale reads cannot replace current-card state
[ ] no write/clone/emulation/key-search paths exist
[ ] native tests pass
[ ] architecture/static checks pass
[ ] firmware build passes
[ ] physical ST25R3916 acceptance passes
[ ] relevant documentation is updated
```

The EMT and Consorcio profiles do **not** need to expose every conceivable card field to complete the plan.

Their completion criterion is:

```text
supported behavior is verified,
unsupported behavior is explicitly identified,
and no guessed data is presented as fact.
```
