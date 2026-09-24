# Implementation Plans and Status

Reviewed on **2026-09-18**. Plan numbers identify individual changes; they are
not phase numbers. The authoritative delivered/pending step checklist is in
[Architecture §47](../ARCHITECTURE.md#47-initial-development-order).

Plans 001–031 keep their original structure. Starting with plan 032, new plans
follow the template in [Engineering §38](../ENGINEERING.md#38-implementation-plans):
architecture in prose, BDD scenarios for behavior and failure, and an explicit
ownership matrix with invariants. Do not rewrite older plans into that format.

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
| [018](018-host-profile-metadata.md) | Host Profile metadata and configuration migration | Implemented; subsumed by the current version-4 system schema |
| [019](019-startup-splash.md) | Startup splash | Implemented; automated checks and firmware build passed |
| [020](020-sync-uv-workflow-pin.md) | Workflow toolchain consistency | Implemented; host checks and firmware build passed |
| [021](021-key-feedback-and-volume.md) | Synthesized key feedback and persistent volume | Software implemented; physical audio acceptance pending |
| [022](022-ui-update-and-render-scheduling.md) | UI scheduling and incremental rendering | Complete; automated and physical validation passed |
| [023](023-enforce-architecture-boundaries.md) | Enforced source dependency boundaries | Implemented; host checks and firmware build passed |
| [024](024-host-service-status-model.md) | HostService domain status snapshot | Implemented; automated checks and firmware build passed |
| [025](025-persisted-wifi-configuration-and-runtime-composition.md) | Persisted Wi-Fi and runtime composition | Implemented; automated checks and firmware build passed |
| [026](026-firmware-size-observability-and-safe-optimization.md#0-current-closeout-status) | Firmware size observability and safe optimization | Complete; automated checks, firmware build, and physical Wi-Fi/BLE smoke test passed |
| [027](027-wifi-system-ui-and-home-network-status.md) | Wi-Fi System UI and Home network status | Complete |
| [028](028-mini-app-runtime-foundation.md#0-current-closeout-status) | Mini App runtime foundation | Software complete; physical Home/Settings smoke pending; Launcher and production Mini Apps remain deferred |
| [029](029-appregistry-driven-launcher-integration.md#0-planning-status) | AppRegistry-driven Launcher and SYSTEM Mini App | Software implemented; physical Home → Launcher → SYSTEM validation pending |
| [030](030-macos-companion-foundation.md) | macOS Companion foundation | Software implemented; physical BLE/GATT lifecycle validation pending |
| [031](031-mac-control-app-grid.md) | MAC CONTROL app grid and Telegram activation | Software implemented; physical Telegram focus/launch/disconnect validation pending |
| [032](032-display-power-and-wake.md) | Display idle dimming, off and wake consumption | Software implemented; physical Cardputer-Adv dim/off/wake acceptance pending |
| [033](033-pomodoro-timer-and-optional-led-progress.md#0-current-status) | Pomodoro timer, optional LED progress, phase-transition display wake | Software implemented; physical Cardputer-Adv LED and display-wake acceptance pending |
| [034](034-led-gallery.md) | LED Gallery with ten procedural matrix effects, including Kaleidoscope | Software implemented; physical Unit Puzzle acceptance and tuning pending |
| [034/2](034-2-led-gallery-expansion.md) | Twenty LED Gallery effects and contextual interaction | Software implemented; physical Unit Puzzle acceptance and tuning pending |

The work referred to historically as 013 is the approved
[UI requirements](../UI_REQUIREMENTS.md); there is no separate plan-013 file.
Phases 3/4/5/6 contain delivered slices from 017 and 028 and remain partial.
Phases 7–12 are future implementation work, not missing numbered plan files.

Plan 017's current summary consolidates physical evidence, including the
operator-confirmed two-computer switching and last-host power-on restoration.
Its later sections are chronological records: superseded controls, temporary
firmware, old failures and old pending lists must not be treated as current
instructions. Follow the [manuals](../manuals/README.md) to operate the device.
