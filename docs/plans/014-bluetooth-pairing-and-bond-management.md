# Bluetooth Pairing and Bond Management Plan

Status: **Implemented — physical validation pending**

This plan describes the fourth granular Phase 2 change. It adds authenticated
BLE pairing, persistent opaque bond references, explicit bond management, and
selected-bond reconnection without adding pairing UI, Host Profiles, or HID
reports.

Implementation should use:

```text
branch:   feat/014-bluetooth-pairing-and-bonds
PR title: [014] Add authenticated Bluetooth pairing and bond management
```

The implementation branch must begin from updated `main` after plan 012 is
merged and its mandatory physical validation is complete. The UI requirements
change numbered 013 must also be merged first.

## 1. Summary

Add:

* an explicit, time-bounded pairing mode on `BluetoothService`;
* LE Secure Connections pairing with bonding and MITM protection;
* negotiated passkey display, passkey entry, and numeric comparison;
* stable opaque references for persisted Bluetooth bonds;
* bond enumeration, selected-bond reconnection, remove-one, and remove-all;
* native behavioral tests and ESP-NimBLE security integration.

Bluetooth remains inactive during normal boot. A temporary, unshipped hardware
validation harness may drive pairing, but Device Manager owns the eventual UI
and `HostService` owns the eventual mapping from a bond reference to a
`HostProfile`.

## 2. Public Contract and Behavior

Extend the hardware-independent Bluetooth API with:

* `BluetoothBondReference`: an opaque, stable 128-bit value with equality and
  ordering only;
* `BluetoothPairingState`: `Closed`, `Preparing`, `Advertising`,
  `AwaitingResponse`, `Completing`, `Succeeded`, or `Error`;
* `BluetoothPairingChallengeType`: `DisplayPasskey`, `EnterPasskey`, or
  `ConfirmComparison`;
* `BluetoothPairingChallenge`: a non-identifying generation, challenge type,
  and optional six-digit value for display or comparison;
* explicit pairing-open, cancellation, response, bond-query, target-selection,
  bond-removal, and remove-all result types;
* `BluetoothService::openPairing`, `cancelPairing`, `respondToPairing`,
  `pairingState`, `pairingChallenge`, `completedPairing`, `bonds`,
  `selectBond`, `removeBond`, and `removeAllBonds`;
* adapter events and operations for security challenges, security completion,
  bond enumeration, stable reference calculation, and bond deletion.

Contract rules:

* `openPairing` requires an enabled lifecycle and opens a 120-second window;
* the timeout begins only after pairing advertising starts successfully;
* opening pairing while connected requests peer disconnection without deleting
  its bond, then advertises for pairing after disconnection completes;
* repeated open requests are idempotent only for the same active window;
* only one unbonded peer and one challenge may be pending;
* bonded reconnections and additional unbonded peers are rejected during an Add
  Device window so they cannot consume or replace that window;
* pairing requires LE Secure Connections, bonding, encryption, and MITM
  authentication with `KeyboardDisplay` I/O capability;
* Just Works, legacy-only, OOB, unauthenticated, unencrypted, and non-bonded
  completion are rejected;
* the Service supports the authenticated method negotiated by the peer:
  passkey display, six-digit passkey entry, or numeric comparison;
* passkey entry accepts exactly six decimal digits, preserving leading zeroes;
* every challenge response carries the issuing challenge generation; stale,
  repeated, malformed, or wrong-kind responses are rejected without reaching
  the adapter;
* cancellation or timeout rejects an incomplete peer and resumes normal
  selected-bond advertising only after that peer disconnects;
* successful pairing closes the window, exposes exactly one new bond reference,
  and retains the newly bonded peer as the current connection;
* no pairing code, comparison value, peer identity, bond reference, key, or
  address may be logged;
* the Service supports at most 16 bonds and never evicts an existing bond;
* a full registry rejects new pairing with an explicit capacity result;
* outside pairing, unbonded peers retain the plan-011 rejection behavior;
* when a bond is selected, any other bonded peer is also rejected without
  changing the selected target;
* selecting a different bond disconnects the old peer, retains both bonds, and
  resumes advertising for the new target after disconnection;
* selecting no bond clears targeted reconnection without deleting bonds;
* removing an active bond disconnects it before deleting it;
* remove-all processes every persisted bond, leaves no selected target, and
  reports partial adapter failure instead of claiming complete success;
* pairing, deletion, and target replacement remain asynchronous and progress
  only through `update(elapsed)`; no callback mutates Service state directly.

## 3. Persistence and ESP-NimBLE Adapter

Extend `Esp32BluetoothAdapter` without introducing a second Bluetooth owner:

* configure `ble_hs_cfg` for Secure Connections, bonding, MITM, identity-key
  distribution, and `KeyboardDisplay` I/O capability;
* initiate security only for the one peer admitted by the Service policy;
* copy bounded passkey-action and security-completion events into the existing
  lifecycle event queue;
* route application responses back through checked NimBLE security APIs;
* initialize NimBLE's persistent store non-destructively and never erase NVS as
  automatic recovery;
* enumerate and delete bonds using the checked ESP-NimBLE store APIs;
* keep raw public, random, and identity addresses inside the adapter;
* derive `BluetoothBondReference` as the first 128 bits of HMAC-SHA-256 over
  the adapter-private identity address using a random, device-local 256-bit
  reference key;
* generate that key with the ESP-IDF cryptographic random source, persist it in
  internal authoritative storage before admitting new pairing, and never log or
  export it;
* on an existing installation with bonds but no reference key, create the key
  before exposing references; no `HostProfile` migration is needed because
  Phase 3 has not yet shipped;
* if reference-key persistence or new-bond finalization fails, delete the new
  bond and disconnect the peer so no unreachable bond remains;
* detect an improbable reference collision as an adapter error rather than
  returning an ambiguous reference;
* retain failed disconnect, bond deletion, host shutdown, or persistence
  cleanup as owned work that a later explicit operation can retry;
* preserve lifecycle generations, the single peer slot, bounded callback
  storage, explicit advertising ownership, and definitive shutdown behavior.

The stable-reference key is Bluetooth-internal metadata, not a user-visible
configuration record. Future `ConfigurationService` may migrate its storage
mechanics without changing the public bond-reference contract.

## 4. Granular TDD Implementation Sequence

1. Add failing tests proving that closed pairing continues to reject an
   unbonded peer and has no challenge or completed result.
2. Add enable-state, open, repeated-open, cancel, and 120-second boundary tests;
   implement only the pairing-window lifecycle.
3. Add connected-peer tests and implement disconnect-before-pairing without
   deleting or replacing the existing bond.
4. Add one failing test for each authenticated challenge type, then implement
   bounded challenge publication through `update`.
5. Add malformed passkey, stale generation, repeated response, wrong-kind
   response, explicit rejection, and adapter-response failure tests.
6. Add Just Works, legacy, unauthenticated, unencrypted, non-bonded, cancelled,
   and timed-out completion tests; implement strict completion validation.
7. Add successful-pairing tests proving exactly one stable opaque reference is
   returned and the peer becomes the current connection.
8. Add reboot, disable/re-enable, missing-key upgrade, key-persistence failure,
   finalization failure, and reference-collision tests.
9. Add bond enumeration, capacity, remove-one, remove-active, remove-all,
   not-found, and partial-failure tests.
10. Add selected-bond connection, wrong-bond rejection, target replacement,
    target removal, and reconnect tests.
11. Refactor pairing, rejection, and cleanup state helpers while the focused
    suite remains green.
12. Add the thin ESP-NimBLE security and bond-store integration. Framework
    callback glue is a thin-adapter TDD exception; all observable policy remains
    covered through fake-adapter tests.
13. Audit controller, NimBLE, SMP, GAP, ATT, and storage logging and add
    identity/privacy regressions for project-owned logs.
14. Update the Bluetooth, persistence, failure-isolation, and Phase 2 sections
    of `docs/ARCHITECTURE.md`; update README status without claiming pairing is
    available in normal firmware.
15. Complete this plan with RED/GREEN evidence, exact check results, firmware
    size, and physical validation findings.

## 5. Test and Verification Plan

Native scenarios must cover every behavior in the TDD sequence plus isolation
from Wi-Fi and System Core. Run and record:

```text
uv run --frozen pio test -e native -f test_bluetooth_service
make format
make format-check
make lint
make test
make build
make check
```

Mandatory Cardputer-Adv validation must:

* exercise numeric comparison and passkey entry with controlled peers;
* confirm cancellation and timeout reject the incomplete peer;
* pair, reboot, reconnect, enumerate, remove one bond, and remove all bonds;
* fill the supported registry and confirm the seventeenth bond is rejected
  without eviction;
* confirm an unselected bonded peer cannot replace the selected peer;
* confirm Bluetooth failures leave Wi-Fi, display, keyboard, storage, and the
  boot loop responsive;
* audit the complete log for peer identities, bond references, keys, and
  pairing values.

The validation harness must not ship and must remove every test bond when the
validation record is complete.

## 6. Acceptance Criteria and Assumptions

The change is complete only when:

* higher layers can open authenticated pairing and manage bonds without ESP32
  or NimBLE types;
* all admitted new bonds are Secure Connections, encrypted, authenticated, and
  persistently referenceable;
* bond references remain stable across reboot and reveal no peer address;
* all destructive bond operations are explicit and report partial failure;
* no user-specific name, platform, or `HostProfile` logic enters Connectivity;
* pairing remains inactive during normal boot;
* all automated and mandatory physical checks pass.

Assumptions and defaults:

* plans 012 and the UI requirements change numbered 013 merge first;
* authenticated interoperability takes priority over Just Works compatibility;
* one live connection and up to 16 persisted bonds are supported;
* Device Manager pairing UI and `HostProfile` creation remain later-phase work;
* BLE HID reports are introduced only by plan 015.

## 7. Implementation Record

Implemented on 2026-09-06. The required physical Cardputer-Adv validation is
still pending, so this plan is not marked complete.

TDD and delivered behavior:

* RED: the first pairing-state test failed because `BluetoothService` had no
  pairing state, challenge, or completion API. The hardware-neutral contract
  and closed-window behavior made that slice pass.
* Pairing-window timing, disconnect-before-pairing, all three authenticated
  challenges, response generation and input validation, cancellation, strict
  security completion, capacity, selected-target enforcement, and asynchronous
  bond deletion were then covered with fake-adapter behavioral tests. The
  focused suite contains 48 passing cases, including all retained plan-011
  lifecycle regressions.
* `Esp32BluetoothAdapter` configures bonding, MITM, `KeyboardDisplay`, identity
  key distribution, and Secure Connections-only security. Its callback glue
  remains the documented thin-adapter TDD exception; callbacks only copy bounded
  challenge, identity-resolution, and completion data into the existing queue.
* A random 256-bit reference key is created non-destructively in `hub_config`
  before pairing can be admitted. The adapter derives 128-bit references with
  HMAC-SHA-256, rejects collisions, keeps addresses private, and exposes checked
  ESP-NimBLE bond enumeration and deletion.
* Normal runtime composition is unchanged. There is no pairing UI or HID
  transport yet, so `docs/manuals/` requires no supported-behavior change.
* Review follow-up added a regression proving disable forgets the previous
  challenge response generation and aligned the runtime security-level value
  with ESP-IDF's verified `CONFIG_BT_NIMBLE_SM_LVL=3` encoding.
* A compile-time-only `validation-014` image and physical runbook now exercise
  pairing through the Cardputer display and keyboard, privacy-safe bond
  management, reboot reference comparison, capacity, microSD, Wi-Fi, and
  validation-only Bluetooth failure injection. It is mutually exclusive with
  the retained plan-012 harness and excluded from production firmware.

Automated verification:

```text
make format                                      PASS
make format-check                                PASS
make lint                                        PASS (no findings)
uv run --frozen pio test -e native \
  -f test_bluetooth_service                      PASS (48 tests)
make test                                        PASS (38 Python + 153 native tests)
make build                                       PASS
make check                                       PASS
validation-014 firmware build                    PASS
retained validation-012 firmware build           PASS
```

A clean ESP-IDF 5.5.5 build using only `sdkconfig.defaults` produced a
436,432-byte application image, leaving 87% of the smallest OTA partition free.
Compile-time configuration guards reject legacy pairing, debug keys, a bond
capacity other than 16, or missing persistent Secure Connections support.
The opt-in validation-014 image is 1,244,240 bytes and leaves 63% free.

No hardware pairing was attempted and no user bonds were created or removed.
The mandatory interoperability, reboot, capacity, failure-isolation, and full
log privacy checks in section 5 remain required before changing this plan's
status to complete.
