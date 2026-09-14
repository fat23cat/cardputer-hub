# 024 — Host Service Status Model

Status: **Implemented — automated verification and firmware build passed**

## 1. Goal

Prevent UI code from depending directly on Bluetooth and HID implementation state.

`HostService` should expose a domain-level representation of host connectivity and pairing state suitable for:

* Home;
* Bluetooth settings;
* future Launcher/Mini Apps;
* future macOS companion;
* future Web UI or remote control.

The UI should not need to understand the internal Bluetooth state machine.

---

## 2. Current Problem

`HostService` currently exposes connectivity-level state directly:

```cpp
BluetoothPairingState pairingState();
BluetoothPairingChallenge pairingChallenge();
BluetoothState bluetoothState();
HidTransportState hidState();
```

Consequently the Apps layer contains logic such as:

```text
Bluetooth Disabled → OFF
Bluetooth Error → ERROR
HID Ready → READY
Bluetooth Connected → SECURING
pairing → PAIRING
otherwise → CONNECTING
```

This mapping already exists in more than one place.

Home and Host Settings can therefore disagree about the same underlying state as the implementation evolves.

It also couples UI behavior to Connectivity implementation details.

---

## 3. Scope

Included:

* domain-level host status;
* immutable/read-only HostService snapshot;
* unified mapping from Bluetooth/HID state to host-facing state;
* migration of Home and Host Settings to the snapshot;
* tests for status mapping.

Not included:

* BluetoothService redesign;
* pairing state-machine refactor;
* changes to pairing security;
* Web UI implementation;
* companion implementation;
* transport-independent host abstraction beyond what is currently required.

---

## 4. Domain-Level Status

Introduce a HostService-owned status enum.

Example:

```cpp
enum class HostConnectionStatus : std::uint8_t {
    Off,
    Connecting,
    Securing,
    Ready,
    Pairing,
    Error,
};
```

Names may change slightly, but they should describe user-visible/domain state rather than Bluetooth internals.

The mapping should exist in exactly one place.

---

## 5. Host Snapshot

Introduce an immutable snapshot returned by HostService.

Example:

```cpp
struct HostStatusSnapshot {
    HostConnectionStatus status = HostConnectionStatus::Off;

    std::optional<std::uint32_t> activeHostId;
    std::string activeHostName;

    HostResult lastResult = HostResult::Success;

    bool pairing = false;
    std::optional<HostPairingPrompt> pairingPrompt;
};
```

Avoid copying the entire HostConfiguration if it is unnecessary.

The exact model may instead expose:

```cpp
const HostConfiguration& settings() const;
HostStatusSnapshot status() const;
```

This keeps profile configuration and runtime status conceptually separate.

---

## 6. Pairing Prompt

UI currently needs pairing-specific information, but it should not require `BluetoothPairingChallenge` directly.

Introduce a host-facing model:

```cpp
enum class HostPairingPromptType : std::uint8_t {
    None,
    DisplayPasskey,
    EnterPasskey,
    ConfirmComparison,
};

struct HostPairingPrompt {
    std::uint32_t generation = 0;
    HostPairingPromptType type = HostPairingPromptType::None;
    std::optional<std::uint32_t> value;
};
```

`HostService` translates from Connectivity types.

UI should depend only on these host-level types.

---

## 7. Status Mapping

Centralize the existing UI mapping inside `HostService`.

Recommended precedence:

```text
fatal service/runtime failure
    → Error

Bluetooth disabled
    → Off

pairing in progress
    → Pairing

HID ready
    → Ready

Bluetooth connected but HID not ready
    → Securing

enabled / advertising / reconnecting
    → Connecting
```

The exact precedence should preserve current visible behavior.

Document it with a table.

Example:

| Condition                    | Host status  |
| ---------------------------- | ------------ |
| `HostResult::StorageError`   | `Error`      |
| `HostResult::BluetoothError` | `Error`      |
| `HostResult::MissingBond`    | `Error`      |
| Bluetooth error              | `Error`      |
| Bluetooth disabled           | `Off`        |
| pairing active               | `Pairing`    |
| HID ready                    | `Ready`      |
| Bluetooth connected          | `Securing`   |
| otherwise enabled            | `Connecting` |

---

## 8. UI Migration

Update Home to use:

```cpp
const auto status = hosts_.status();
```

instead of reading:

```cpp
hosts_.bluetoothState();
hosts_.hidState();
hosts_.pairing();
hosts_.lastResult();
```

Home should translate only:

```text
HostConnectionStatus
    ↓
text + palette
```

Example:

```cpp
switch (status.connection) {
case HostConnectionStatus::Off:
    ...
case HostConnectionStatus::Ready:
    ...
}
```

Host Settings should use the same snapshot.

Delete the second independent status computation currently living in Host Settings.

---

## 9. Keep Connectivity Internal

After migration, remove public HostService methods whose only purpose is exposing Connectivity internals, where possible:

```cpp
bluetoothState()
hidState()
pairingState()
pairingChallenge()
```

If some are temporarily required by tests, migrate the tests to domain-level behavior before removing them.

The objective is:

```text
Apps
    ↓
HostService domain model

NOT

Apps
    ↓
HostService
    ↓
Bluetooth implementation state leaking back upward
```

---

## 10. Actions

Existing ActionBus commands may remain unchanged:

```text
host.select
host.bluetooth
host.rename
host.pair
host.cancel-pairing
host.pair-response
host.delete
```

This plan does not require redesigning the Action Bus.

Pairing response Actions should continue to carry the prompt generation number so stale responses remain rejectable.

---

## 11. Revision Tracking

Consider adding a lightweight revision number to HostService:

```cpp
std::uint32_t revision() const noexcept;
```

Increment it when externally observable host state changes.

This can later support efficient UI dirty checking:

```cpp
if (hosts.revision() != previousRevision)
    refreshHostView();
```

Do not make revision tracking mandatory if the typed snapshot from plan 022 already solves the rendering problem cleanly.

---

## 12. Testing

Add tests for every status transition.

At minimum:

```text
disabled
    → Off

enabled + advertising
    → Connecting

connected + not HID ready
    → Securing

HID ready
    → Ready

pairing active
    → Pairing

Bluetooth failure
    → Error

storage failure
    → Error

missing bond
    → Error
```

Also test pairing prompt translation:

```text
DisplayPasskey
EnterPasskey
ConfirmComparison
generation preservation
value preservation
prompt cleared after response
```

Add UI tests proving Home and Host Settings display the same semantic status from the same HostService state.

---

## 13. API Design Constraint

Do not turn `HostStatusSnapshot` into a dumping ground for arbitrary Bluetooth properties.

Before adding a field, ask:

```text
Is this information part of the host/application domain,
or is it merely an implementation detail of BLE?
```

Examples that should normally remain inside Connectivity:

```text
advertising lifecycle ID
peer handle
bond operation state
retry kind
GATT subscription details
protocol mode
raw security flags
```

---

## 14. Future Transport Support

The domain-level status should not contain `Ble` in its names.

Future host communication may include:

```text
BLE HID
USB
companion protocol
other transport
```

A status such as:

```cpp
HostConnectionStatus::Ready
```

remains valid regardless of transport.

This does not require implementing transport arbitration now.

---

## 15. Acceptance Criteria

The plan is complete when:

* Home does not read `BluetoothState` directly;
* Host Settings does not read `BluetoothState` or `HidTransportState` directly;
* host-facing connection status is computed in one place;
* pairing UI consumes host-level pairing prompt types;
* duplicate `OFF / CONNECTING / SECURING / READY / ERROR` mapping is removed;
* HostService tests cover all status mappings;
* existing pairing behavior remains unchanged;
* existing native tests pass;
* firmware builds successfully;
* physical-device Bluetooth behavior remains unchanged.

---

## 16. Follow-Up

After this plan, consider a separate Action Bus cleanup:

```text
typed/static action IDs
fail-fast registration
HostActionHandler adapter
```

Do not include that refactor here.

The purpose of this plan is specifically to establish a clean boundary:

```text
Apps → Host domain API → Connectivity
```

---

## 17. Completion Record

Implemented on 2026-09-14.

Delivered:

* `HostConnectionStatus` and a read-only `HostStatusSnapshot` owned by
  `HostService`;
* one status mapping with failure, Off, Pairing, Ready, Securing, and
  Connecting precedence;
* host-facing pairing prompt types, generation/value preservation, and a small
  domain pairing phase used by the existing securing guidance;
* active-host identity/name, current connectivity enablement, pairing state,
  prompt, and last operation result in the snapshot;
* Home and Host Settings migration to the same snapshot;
* removal of the public `bluetoothState`, `hidState`, `pairingState`,
  `pairingChallenge`, `pairing`, and `lastResult` accessors that exposed or
  fragmented runtime state;
* architecture and UI contract updates.

TDD evidence:

* the new HostService status tests were first observed failing to compile
  because the domain types and `HostService::status()` did not exist;
* after implementation and follow-up hardening, the focused HostService and
  Host Settings suites passed together: 52 tests;
* the UI suite includes an explicit check that Home and Bluetooth Settings show
  `PAIRING` from the same HostService state.
* the follow-up regression test proves that an empty rename of the selected host
  returns `InvalidInput` and preserves its published name, snapshot identity,
  and stored configuration; therefore Home's empty-name check is safe under the
  persisted configuration contract.

Final verification:

* architecture dependency check: passed (87 files);
* formatting check: passed;
* native static analysis: passed with no high- or medium-severity findings;
* Python tests: 66 passed;
* native tests: 258 passed;
* ESP-IDF 5.5.5 production firmware build: passed;
* resulting application image: `0xcddf0` bytes with 75% of the smallest app
  partition free.

No BluetoothService state-machine, pairing-security, or hardware-adapter logic
changed; existing Bluetooth and pairing regression suites passed. No additional
physical-device behavior was introduced by this boundary migration.
