# Implementation Plans and Status

Reviewed on **2026-09-11**. Plan numbers identify individual changes; they are
not phase numbers. The authoritative delivered/pending step checklist is in
[Architecture §47](../ARCHITECTURE.md#47-initial-development-order).

| Plan | Area | Current disposition |
| --- | --- | --- |
| [001](001-project-bootstrap.md) | Bootstrap | Complete |
| [002](002-system-core-foundation.md) | System Core | Complete |
| [003](003-action-bus-foundation.md) | Actions and ActionBus | Complete |
| [004](004-automatic-semantic-releases.md) | Release automation | Implemented; release-policy/workflow tests passed |
| [005](005-configuration-storage-foundation.md) | Configuration/storage boundaries | Complete |
| [006](006-navigation-foundation.md) | NavigationStack | Complete |
| [007](007-capability-registry-foundation.md) | Capability Registry | Complete |
| [008](008-app-registry-foundation.md) | Metadata AppRegistry | Complete; runtime Mini App integration is Phase 5 |
| [009](009-microsd-file-storage-foundation.md) | microSD/File Storage | Complete foundation; normal boot does not mount a card |
| [010](010-wifi-connectivity-foundation.md) | Wi-Fi foundation | Complete; runtime configuration/UI still pending |
| [011](011-bluetooth-lifecycle-foundation.md) | BLE lifecycle foundation | Complete foundation; later pairing/HID fixes in 014–017 |
| [012](012-supported-toolchain-and-nimble-migration.md) | ESP-IDF/NimBLE migration | Implemented; extended physical checks remain |
| [014](014-bluetooth-pairing-and-bond-management.md) | Pairing/bonds | Implemented; historical hardware acceptance has recorded equipment gaps |
| [015](015-ble-hid-transport.md) | BLE HID transport | Implemented; historical report checks, remaining final-image matrix in 017 |
| [016](016-native-usb-hid-transport.md) | USB HID | Cancelled and removed; not unfinished product work |
| [017](017-hid-transport-arbitration.md#0-current-closeout-status) | BLE-only closeout, hosts and system UI | Software complete; core device scenarios passed, extended acceptance partial |
| [019](019-startup-splash.md) | Startup splash | Implemented; automated checks and firmware build passed |

The work referred to historically as 013 is the approved
[UI requirements](../UI_REQUIREMENTS.md); there is no separate plan-013 file.
Phases 3/4/6 contain delivered slices from 017 and remain partial. Phase 5 and
Phases 7–11 are future implementation work, not missing numbered plan files.

Plan 017's current summary consolidates physical evidence, including the
operator-confirmed two-computer switching and last-host power-on restoration.
Its later sections are chronological records: superseded controls, temporary
firmware, old failures and old pending lists must not be treated as current
instructions. Follow the [manuals](../manuals/README.md) to operate the device.
