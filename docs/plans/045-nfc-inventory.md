# 045 — NFC Inventory for Boxes and Bags

Status: **Software implemented; automated gates pass; physical acceptance pending**

## Current status

Plan 044 proved that Cardputer-Adv can detect the M5Stack Unit NFC U216 on
Grove and identify a physical NFC-A card. Its transport-card direction was
withdrawn on 2026-10-02 and replaced by this personal inventory: a tag
identifies a container, Cardputer Hub stores its contents on microSD, and the
existing macOS Companion edits those contents.

Implemented on branch `feat/045-nfc-inventory` (2026-10-02), in the plan's order:

1. **Cleanup.** The EMT and Consorcio profiles, transport rules and registry,
   the generic MIFARE Classic inspector, Classic authentication and block reads
   (`INfcReader::authenticateClassic` / `readClassicBlock`), the profile
   registry, the details pages and the `nfc.diag` serial diagnostics are gone
   from code, tests and architecture checks. The verified U216 startup probe,
   Grove/Puzzle arbitration, `NFC_READER` lifecycle, scan-on-demand, one
   operation per update and stale-activation rejection remain, with their tests.
2. **Codecs and files.** `services/nfc/nfc_ndef` (Type 2 window inspection, NDEF
   TLV area, NDEF Text record), `services/inventory/inventory_id` (`CHINV1:` +
   32 lowercase hex), `inventory_record` (strict JSON and limits),
   `core/text/utf8`, and `FileStorage::list`, `rename`, `replaceRecoverable`
   and `readRecoverable` with the microSD adapter's `opendir` listing and
   no-replace rename.
3. **NTAG and services.** `INfcReader::readPages` / `writePage` (user-area pages
   only) in `St25r3916Adapter`; `NfcService` reads NTAG213/215/216 data areas
   from page 2 until the content is decided, classifies the tag, and writes a
   message to a provably blank writable tag (empty TLV and Terminator first,
   real first page last) with read-back verification; `InventoryStore`,
   `InventoryService` (enrollment, lookup, missing-record recovery, revisions)
   and `RemovableStorageService` (mount on first use, `REMOVABLE_FILE_STORAGE`).
4. **Cardputer UI.** The repurposed NFC Mini App (state screens, ASCII name
   editor, paginated descriptions, erase confirmation) and UTF-8 text: `core/display/display_glyphs`,
   code-point layout and wrapping, and the Font0-derived system font with
   original Cyrillic glyphs in `hardware/cardputer/assets/system_font_extension.h`.
5. **Companion.** Protocol operations `INVENTORY_LIST/GET/PUT/DELETE` (9–12) and statuses
   `CONFLICT`, `REJECTED`, `STORAGE_ERROR` (5–7) in the generator, fixtures, C++
   and Swift codecs; Mac-to-Cardputer requests in `CompanionService`
   (`takeInboundRequest`, `respond`, `sessionEpoch`) and `CompanionSession`
   (`sendRequest`, timeout, reset); `InventoryCompanionEndpoint`;
   `InventoryClient`, `InventoryEditorModel` and the SwiftUI Inventory window
   opened from the menu.
6. **Documentation.** Architecture, UI requirements, device guide, protocol and
   Companion READMEs, plan index.

Verification: the `make host-check` component (lock, architecture, format,
cppcheck, Python and native tests), `make firmware-check` (ESP-IDF build and
configuration check), and `make companion-check` pass after the review fixes;
the production image is 1,413,536 bytes, inside the
2 MiB CRUB `hub` partition. **Physical acceptance (section 9) is pending**: no
NTAG stickers, second Mac or power-cut rig were used, so tag writing on the
U216, FAT recovery on real media, the Cyrillic glyphs on the display and BLE
throughput are unverified.

Revised on 2026-10-02 after the first implementation, at the user's request:

* **Record content.** Schema 2 replaces the item list with one free-text
  `description` of up to 900 code points with line breaks; the name stays a
  32-character title entered on the Cardputer and editable in the Companion.
  The record bound is 4 KiB (900 four-byte code points fit). No schema 1
  records existed, so there is no migration; the protocol fingerprint changed.
* **Registration without holding the tag.** The name is typed first (from
  `TAP A TAG` or `BLANK TAG`); `TAP TAG TO WRITE` then waits for any blank,
  writable tag until Escape. Other tags are refused untouched. A record for a
  `NO RECORD FOR THIS TAG` ID is also created without the tag present.
* **Erasing.** Fn+Del on an inventory tag asks first, empties the tag
  (`03 00 FE 00`, verified), then deletes its record for good; nothing is
  deleted when the tag cannot be erased. The Companion deletes records
  (`INVENTORY_DELETE`, revision-checked, 0 for a damaged file) but cannot erase
  tags.
* **Review follow-up.** The erase tests use the confirmation API and distinguish
  foreign writable NDEF from reserved or locked tags. If readback fails after
  the blank page may have been written, Inventory keeps every unresolved target
  while the app runs and deletes each record only after reading its UID as blank;
  otherwise it leaves the record for recovery through the Companion. The Mac
  editor hides a failed or pending listing and disables the old form during a
  fresh GET, preserving an unsaved draft until a successful refresh. A storage
  read error during listing fails the list instead of labeling a record damaged.

Deviations from the plan text:

* NDEF parsing and encoding are project code above `INfcReader`, not the
  vendor NDEF layer: the adapter exposes only Type 2 page reads and single
  user-area page writes, so the never-overwrite rule, the write order and the
  read-back are host-tested. The vendor library's own `safety` user-area check
  still applies to every write.
* Pages 2–17 decide a factory-formatted tag (`03 00 FE`). Blank otherwise
  needs the whole declared data area read: while the bytes decide nothing
  (NULL bytes to their end, or a message continuing past them) four more pages
  are read per update. Lock or Memory Control TLVs without a message, data after
  an empty NDEF TLV, and a capability container larger than the user area are
  "other data" and never written. The first write is `03 00 FE 00`, so a torn
  registration still reads as blank. A capability container that denies
  writes, or any static lock bit, makes a tag read-only for this firmware; a
  dynamically locked page fails the write.
* After every new Companion session the Mac re-reads the selected record; a
  record missing from the new listing is dropped, and an unsaved draft whose
  record advanced becomes a conflict (review finding, 2026-10-02).
* Recoverable replacement keeps the previous file as `<id>.json.bak` and
  commits by renaming `<id>.json.new`; reads fall back to the backup and never
  write; the next write finishes or rolls back an interrupted one. FAT rename
  cannot replace a name, which the in-memory test card models.
* Text layout counts one glyph per code point. Russian Cyrillic, Ё/ё, «», №, °
  and … have glyphs; typographic dashes, quotes and spaces fall back to ASCII
  shapes and other characters show a box.
* At most 256 records (`INVENTORY IS FULL`).
* The Inventory window uses SwiftUI without macros because the command-line
  toolchain lacks the SwiftUI macro plugin.

Tests mapped to the scenarios (section 8): `test_nfc_service` (inspection, write
order, verification, interruption, stale results, reader loss),
`test_inventory_service` (`test_inventory_enrollment_writes_verifies_and_persists_once`,
`test_inventory_lookup_displays_utf8_description_without_companion`,
`test_inventory_storage_loss_preserves_record_and_reader`,
`test_inventory_discards_stale_tag_result`,
`test_inventory_tag_written_save_failed_can_recover`,
`test_inventory_never_overwrites_foreign_ndef_or_classic`,
`test_inventory_put_revision_round_trip`,
`test_erase_readback_failure_reconciles_the_blank_tag`,
`test_uncertain_erase_waits_for_the_same_blank_tag`,
`test_two_uncertain_erases_keep_both_records_until_each_tag_is_blank`, the name-first
registration, erase and delete tests), `test_inventory_codec`
(`test_inventory_rejects_invalid_utf8_json_and_bounds`, glyphs, wrapping),
`test_inventory_companion` (`test_inventory_transfer_cancel_and_revision_conflict`,
`test_list_reports_record_read_error_instead_of_damage`, chunked get/put),
`test_file_storage` (power loss at every step),
`test_nfc_app` (screens), `test_companion_protocol` (fixtures), the
`CompanionCoreCheck` inventory checks (fixtures, limits, session requests,
client, editor conflict/disconnect without replay) and
`test_python/test_nfc_architecture.py`.

This plan's later sections are the original design and remain the contract
where they do not conflict with the deviations above.

## 1. Goal

A user can attach an inexpensive writable NFC sticker to a box, bag or
suitcase; register it with a short name on Cardputer; add or edit a Cyrillic
item list in Cardputer Companion on a Mac; and later tap the sticker to see
that name and list on Cardputer, without Internet or a Mac nearby.

The tag carries only an opaque inventory ID. The microSD card in Cardputer is
the authoritative store for names and item lists. The two Mac installations
edit that same store through whichever Mac currently has a compatible Companion
session; no cloud account or replicated Mac database is required.

## 2. Scope

### Hardware and tag contract

* Retain the verified U216 startup probe, reader-absence behavior, Grove/Unit
  Puzzle pin arbitration, `NFC_READER` capability, scan-on-demand lifecycle and
  stale-card/session rejection from 044.
* The supported enrollment target is a writable, unlocked NFC Forum Type 2
  tag from the NTAG213/215/216 family. Begin with NTAG213 physical acceptance.
  Other technologies may be detected only to show `UNSUPPORTED TAG`; this plan
  does not promise arbitrary card or tag read/write support.
* On enrollment, generate a random 128-bit inventory ID and write a versioned
  NDEF Text record containing `CHINV1:` followed by its 32 lowercase hex
  digits. Read the record back and compare before reporting a successful bind.
  A scan reads this record, validates the version and ID, and uses the ID as a
  file key. A tag containing other data is never overwritten automatically.
* Inventory IDs are locators, not authentication. Copying one to another tag
  makes both point to the same container record. The UID is diagnostic only and
  is not the persistent database key.
* A tag with a valid inventory ID but no local record is recoverable: show the
  missing-record state and offer to create a record for that same ID. A failed
  enrollment must never claim that a box was saved.

### Cardputer behavior

* One repurposed NFC Mini App owns waiting, enrollment, known-container,
  missing-record, unsupported-tag, SD-unavailable and error screens. Keep the
  existing app registration and capability lifecycle rather than adding a
  second NFC app.
* Registration requires a ready microSD card. The Cardputer keyboard enters a
  short ASCII name initially; the Companion may later edit the name and items
  as UTF-8 Cyrillic. Cardputer shows those strings with a Cyrillic-capable
  embedded font, Unicode-aware wrapping and bounded scrolling/pagination on
  its 240×135 display. Input limits and available space are visible before
  saving.
* A known tag loads only its matching record. Card removal clears the visible
  list; another tag replaces the session. Repeated polling of a held tag never
  duplicates a record or write.
* The inventory remains usable without Bluetooth, Wi-Fi or Companion.

### Authoritative file data

* Use one UTF-8 JSON file per inventory ID below the existing Cardputer-owned
  microSD root, for example `inventory/records/<id>.json`. Filenames contain
  only the normalized ASCII ID, never user text. A record has a schema version,
  ID, name, array of item strings and monotonic revision. The Companion presents
  one item per editable row; comma-separated paste may be a UI convenience,
  not the storage format.
* Extend the existing `FileStorage` boundary for bounded directory listing and
  a recoverable replacement operation. The current adapter's `replace()`
  opens the destination with `wb` and can leave it truncated after power or
  media failure; inventory saves must preserve the previous valid record until
  the new one is committed. Define and test the temp/backup/recovery policy
  against the actual FAT behavior before using it for user data. Never format
  or repair the card automatically.
* The inventory service owns JSON parsing, validation, record limits, lookup,
  revision checks and write ordering. For the first implementation, bound each
  record to 8 KiB of encoded JSON and at most 100 item strings; enforce both
  limits in firmware and Companion. Validate UTF-8 and reject malformed or
  duplicate IDs without changing files. Revisit limits only with measured
  memory, transfer and UI behavior.
* No SD means no enrollment and no contents view. It does not break Hub boot,
  Launcher or other Mini Apps. A failed save is surfaced without losing the
  previous record or inventing success.

### macOS Companion

* Add an Inventory window to the existing menu-bar `Cardputer Companion.app`.
  It lists records, shows the Cardputer-entered name, and edits name and item
  rows with normal macOS Unicode input. It does not create a second Companion
  process or a separate Mac-side source of truth.
* The existing selected-host, authenticated BLE Companion session carries
  domain operations: paged list, get record, and revision-checked put record.
  The current 256-byte message limit requires bounded application-level pages
  or chunks for longer UTF-8 JSON. A partial upload is never visible as a
  record; commit happens only after complete validation. Session loss cancels
  in-flight transfers. Reconnect requires a fresh list/get and never silently
  replays an old edit.
* For this plan editing requires a live Companion session and a ready Cardputer
  microSD card. The UI clearly distinguishes disconnected, SD unavailable,
  loading, unsaved edit, conflict and saved states. A second Mac sees the same
  records after connecting to the Cardputer. If a record revision changed since
  it was loaded, reject the stale put and ask the user to reload or resolve it.

### Cleanup and documentation

* Delete the EMT and Consorcio profiles, transport rules/registry, MIFARE
  Classic sector inspector, explicit Classic key/credential paths and raw
  serial card-data diagnostics from project code. Remove their tests and
  architecture checks, replacing them with inventory behavior and invariants.
  Keep only code and tests needed for generic tag detection and the verified
  U216 foundation. No card balance, trip count, card cloning or emulation
  remains in the Mini App.
* Update the owning sections of `docs/ARCHITECTURE.md`, `docs/UI_REQUIREMENTS.md`,
  the device manual, Companion protocol documentation and fixtures, and the
  plan index as behavior lands. Manuals describe only delivered firmware.

## 3. Architecture

```text
NFC Mini App ── UI/input ──▶ InventoryService ◀── domain requests ── CompanionService
                               │                                  ▲
                               ├── NfcService ── INfcReader ── U216 │ BLE
                               └── FileStorage ── microSD adapter   │
                                                   Cardputer Companion.app
                                                   Inventory window
```

`NfcService` owns reader presence, RF operations and one active tag session.
The hardware adapter alone uses M5Unit-NFC NDEF APIs. `InventoryService` owns
the mapping from versioned tag ID to validated JSON record and coordinates
enrollment/save/recovery. The Mini App observes service state and handles
keyboard/navigation only. The Companion protocol carries inventory records,
not filesystem paths or arbitrary file operations. The Mac UI owns edit state;
Cardputer validates and commits every write to the authoritative SD card.

The existing `FileStorage` facade is currently compiled but not constructed
in normal boot. Compose it only when this consumer is added; publish live
removable-storage state independently of the NFC reader. File reads remain
bounded. NFC and SD failures do not propagate into unrelated services.

Implementation order:

1. Clean the 044 worktree of withdrawn transport behavior and keep the verified
   U216 foundation with focused tests and a compiling baseline.
2. Add host-testable inventory ID, NDEF payload and JSON record codecs, plus
   bounded/recoverable file operations.
3. Add NTAG NDEF read/write/verify behind `INfcReader` and the enrollment and
   lookup service state machine.
4. Compose runtime microSD; implement the Cardputer screen, ASCII name entry
   and Cyrillic display/font work.
5. Extend the lockstep BLE Companion protocol and its C++/Swift fixtures,
   implement transfer/revision handling and the macOS Inventory window.
6. Update owning documentation, run focused tests through implementation,
   then the required final firmware and Companion gates and physical acceptance.

## 4. Ownership & Boundaries

| Component | Owns | May call | Must not call |
| --- | --- | --- | --- |
| `app_main` composition | Reader/SD/Inventory/Companion lifecycle | Service updates once per loop | NFC or inventory UI rendering |
| NFC Mini App | Screens, input, pagination | `InventoryService` commands/status | Vendor NFC APIs, SD adapter, Companion transport |
| `InventoryService` | Enrollment, record validation, lookup, revisions and commit | `NfcService`, `FileStorage` | Display, board library, macOS APIs |
| `NfcService` | Reader/card session, NDEF operation results | `INfcReader` | JSON, SD files, Companion UI |
| `St25r3916Adapter` | U216 I2C/RF/NDEF mechanics | M5Unit-NFC | Inventory policy or file paths |
| `FileStorage` and microSD adapter | Safe bounded files and media status | Board SD/FAT only in adapter | Inventory JSON semantics |
| `CompanionService` and protocol | Session, framed inventory operations and correlation | Selected host transport, `InventoryService` | Raw SD path API or UI |
| macOS Inventory window | Text editing and presentation | Companion session domain operations | Direct Cardputer SD writes, independent authoritative cache |

## 5. State Model

* Reader: `Unavailable → Ready → Scanning → TagPresent`; loss clears the tag
  session and withdraws `NFC_READER`, following existing capability behavior.
* Enrollment: `BlankTag → NameEntry → WritingID → VerifyingID → SavingRecord
  → Registered`. Any failed step enters a specific recoverable error. A valid
  ID left on a tag after a failed SD save leads to `MissingRecord`, not a false
  `Registered` state.
* Lookup: `ValidID → Loading → Known | MissingRecord | SDUnavailable |
  InvalidRecord`; removing/replacing the tag invalidates in-flight results.
* Mac edit: `Disconnected | Loading | Clean | Dirty | Saving | Conflict |
  Error`. A `put` includes the revision observed by the editor and creates
  exactly one new revision on a successful commit.
* Card and storage states are separate. SD removal never makes the reader
  appear absent; reader loss never erases SD data.

## 6. BDD Scenarios

### Scenario: Register a blank NTAG213

Given:
- U216 is ready, a blank writable NTAG213 is present, and SD is mounted.

When:
- The user taps the tag, enters a name and confirms.

Then:
- The app writes one versioned random ID and reads it back.
- It saves one JSON record and reports success only after tag and SD
  verification succeed.

### Scenario: Read a registered box offline

Given:
- A valid inventory tag has an SD record with Cyrillic items.
- Mac, Wi-Fi and Internet are unavailable.

When:
- The user taps and holds the tag.

Then:
- The app displays the saved name and paginated items in Cyrillic.
- Holding the tag creates no writes.

### Scenario: Edit from a Mac and see the result on Cardputer

Given:
- Companion is ready and SD is mounted.
- The editor loaded record revision `n`.

When:
- The user edits the name and items and saves.

Then:
- Cardputer validates the complete transfer and commits revision `n+1`.
- The Mac sees saved status and the next tap shows the new text.
- A second Mac connected later retrieves that revision from Cardputer.

### Scenario: SD is absent or disappears

Given:
- U216 has detected a tag.

When:
- SD is absent or disappears during loading or save.

Then:
- The app shows storage unavailable and never claims registration or save.
- It does not erase an existing record; NFC remains available.

### Scenario: Tag leaves during enrollment or lookup

Given:
- An NFC or SD operation is in flight.

When:
- The tag leaves or a different tag arrives.

Then:
- Stale results are discarded and the old contents do not appear for the
  new tag.
- Repeated taps of one tag do not duplicate enrollment.

### Scenario: Enrollment writes a tag but cannot save its record

Given:
- ID readback succeeded but SD commit failed.

When:
- The tag is tapped later with working SD.

Then:
- The app reports a missing record and offers to create one for the
  existing ID without silently generating another ID.

### Scenario: Existing or unsupported tag

Given:
- A tag has unrelated NDEF data, is a locked NTAG, is MIFARE Classic, or
  has another unsupported type.

When:
- The user taps it.

Then:
- The app does not overwrite or authenticate it.
- It shows a specific unsupported/existing-data outcome and can continue
  scanning afterward.

### Scenario: Companion disconnects or a stale edit is saved

Given:
- A record transfer is in flight or the editor holds revision `n`.

When:
- BLE disconnects or the Cardputer record has advanced to `n+1`.

Then:
- Partial transfer state is discarded or the put returns conflict.
- Reconnect does not replay the edit; the Mac can reload the authoritative
  record.

### Scenario: Corrupt or oversized record

Given:
- A record has malformed JSON, invalid UTF-8, a mismatched ID or a size
  above the bound.

When:
- Cardputer or Mac reads it.

Then:
- The record is rejected with a visible error.
- No data is truncated, rewritten or replaced automatically.

## 7. Architecture Invariants

* The tag contains only a versioned opaque ID; name and items live on SD.
* No Mini App or Mac UI calls the vendor NFC library or the microSD adapter.
* Only the U216 adapter includes M5Unit-NFC headers; only the microSD adapter
  includes board filesystem APIs.
* One Cardputer inventory service validates and commits all records, including
  edits received from Companion.
* Inventory transfer is domain-specific and bounded; no arbitrary BLE file
  browser or unchecked remote path write is introduced.
* A tag/record revision is never reported saved before verified persistence.
* Missing SD, reader loss and Companion loss are isolated; offline Cardputer
  lookup needs neither Mac nor network.
* Neither UID nor NDEF ID authorizes security-sensitive actions.
* No transport-card profile, Classic sector authentication, key search, raw
  card-data diagnostic, card emulation or cloning remains in product code.

## 8. Tests Mapped to Scenarios

Each scenario starts with one focused failing host test before its production
change. The planned mapping is:

| Scenario | Focused test / verification |
| --- | --- |
| Register blank tag | `test_inventory_enrollment_writes_verifies_and_persists_once` with fake reader/storage |
| Offline known box | `test_inventory_lookup_displays_utf8_items_without_companion` and UI snapshot/geometry checks |
| Mac edit and second Mac | `test_inventory_put_revision_round_trip` in C++/Swift protocol fixtures and CompanionCore checks |
| SD absent/removal | `test_inventory_storage_loss_preserves_record_and_reader` plus storage-adapter physical check |
| Tag removal/replacement | `test_inventory_discards_stale_tag_result` |
| Missing record recovery | `test_inventory_tag_written_save_failed_can_recover` |
| Unsupported/existing tag | `test_inventory_never_overwrites_foreign_ndef_or_classic` |
| Disconnect/conflict | `test_inventory_transfer_cancel_and_revision_conflict` in C++/Swift |
| Corrupt/oversized data | `test_inventory_rejects_invalid_utf8_json_and_bounds` |
| Ownership/invariants | `test_python/test_nfc_architecture.py`, architecture check, build-link boundary |

The exact test filenames may be split by module during implementation; keep
the scenario-to-test mapping current. Run narrow tests during TDD. After the
last material change, run `make check` and `make companion-check` once, plus
focused workflow checks only if build/toolchain configuration changes.

## 9. Physical Acceptance

* On the Cardputer-Adv with U216, register two distinct NTAG213 stickers and
  confirm each resolves to its own record after reboot, with Mac disconnected.
* Use the Companion to enter a multi-line Cyrillic list and a Cyrillic name;
  verify glyphs, wrapping, scrolling and removal/replacement on the physical
  240×135 display. Verify editing from one Mac and reading from a second
  selected Mac against the same SD record.
* Remove SD before enrollment, during a save, and before a lookup; confirm
  truthful error states and preservation of prior data. Reinsert and recover
  without formatting. Power-cycle during a disposable record update and check
  the documented temp/backup recovery policy.
* Remove a tag during NDEF write/readback; retry without duplicate records.
  Present an already programmed unrelated NDEF tag and a protected Classic
  card; neither is modified.
* Unplug U216 while scanning and check capability loss and recovery. Confirm
  the existing Unit Puzzle/Grove arbitration remains correct.
* Validate actual BLE throughput and cancellation with a near-limit record,
  disconnect during transfer, and reconnect before reporting completion.

Physical checks that require tags or a second Mac remain explicitly pending
until the equipment is available; automated gates do not imply they passed.

## 10. Out of Scope

* EMT Málaga/Consorcio decoding, trip counts, balances, protected MIFARE
  sectors, key discovery, raw dumps, cloning and card emulation.
* Arbitrary NDEF editing, NFC-B/F/V inventory tags, 125 kHz RFID and phone
  companion apps. Recognition of a card family does not imply inventory
  compatibility.
* Cloud sync, a Mac-local authoritative database, offline Mac edit queues and
  simultaneous multi-host writes. The first Mac editor requires a live
  selected-host Companion session.
* Cyrillic text entry on Cardputer, full-text search, nested containers,
  photos and arbitrary attachments. Cyrillic display is required.
* Exposing microSD as USB mass storage or directing the user to edit JSON
  files manually; these can be separate future workflows.
