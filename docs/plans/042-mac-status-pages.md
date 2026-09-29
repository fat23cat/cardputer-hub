# 042 — MAC STATUS pages, history and detailed macOS telemetry

## Current status

Software implemented on branch `claude/042-mac-status-pages`; automated gates
passed. Physical Cardputer-Adv acceptance (section 9) is in progress.

First device run, 2026-09-28, failed: sparklines were isolated dots, detail
pages and then the overview fell to `--`, and MAC STATUS closed until
Reconnect was pressed in the Companion. Causes and fixes:

* The Mac wrote 17-byte chunks, one ATT write-with-response each. v6 traffic
  (a 3-chunk overview every second, up to 7-chunk details every two seconds,
  pings) outran the link: answers passed the 2-second timeout, the Cardputer's
  16-chunk receive ring overflowed, and the Cardputer dropped the Companion
  transport. The Mac now writes MTU-sized chunks (a whole response in one
  write at the usual MTU).
* After such a drop the Mac had no liveness check once the handshake was done,
  so only a manual Reconnect recovered. It now reconnects by itself after 12
  seconds without a valid request.
* History marked every second without an answer as a gap, so late answers
  turned the line into dots. A gap now means a failed or unsent poll only.

A related report from another Mac (AI USAGE open for about two hours, then
closed; Companion Reconnect did not help, only restarting the Companion did)
traced to the same receive path, independent of plan 042: a receive-ring
overflow or a failed notify cleared the firmware's Companion subscription flag,
and only a new CCCD write restores it. An in-process Reconnect does not
produce one while the HID link stays up; quitting the Companion does. Firmware
now keeps the subscription on overflow and notify failure, and the ring is a
1536-byte variable-length FIFO (two full AI_USAGE messages in 17-byte chunks,
or five MTU-sized chunks) instead of 16 fixed 259-byte slots.

Step 0 spike, 2026-09-28, MacBook with Apple M4 (4 P + 6 E cores), macOS 27.0,
as an ordinary user without new permissions. Every field in section 3 was
readable, so none was removed:

| Source | Result |
| --- | --- |
| P/E clusters | `hw.perflevel*` gives only counts. The CPU-to-cluster map comes from IORegistry `IOPlatformDevice` `cluster-type` (`E`/`P`) keyed by `logical-cpu-id`: CPUs 0–5 E, 6–9 P. |
| GPU | `Device Utilization %` readable (29% during the spike). |
| Power draw | `PowerTelemetryData.SystemLoad` in mW, present on AC and on battery; `SystemPowerIn` is adapter input, so `SystemLoad` is used. |
| Health | `AppleRawMaxCapacity` is absent on this Mac; `BatteryData.NominalChargeCapacity / DesignCapacity` works (capped at 100%). Cycles and adapter watts readable. |
| Peripheral | `AppleDeviceManagementHIDEventService` exists; no battery peripheral was paired, so this field is unverified. |
| Wi-Fi | RSSI and transmit rate readable; `ssid()` is nil without Location, as expected. |
| ICMP | Unprivileged `SOCK_DGRAM` echo to `1.1.1.1` works (about 14 ms). The home router drops ICMP but accepts TCP 80/443, so router time falls back to a TCP connection. |
| Processes | 383 of 555 processes readable; the rest belong to other users. `proc_pid_rusage` times need `mach_timebase_info` (125/3 here). |
| Memory, swap, SSD, disk I/O | All readable. |

Deviations from the draft below, applied in the code and in this plan: SSD
space is decimal GB (as Finder shows it), an invalid detail group is an
invalid request rather than a `MALFORMED` answer, and a malformed
telemetry response (SYSTEM_METRICS, AI_USAGE or SYSTEM_DETAILS) fails only that
request.

Code review on 2026-09-28 added: `APPS IDLE` instead of a blank app list; a
compact cores line (`P61% E18% GPU27%`) and a label-fitting rule so no row
touches its value; a shared `isPageLeft`/`isPageRight` input helper used by MAC
CONTROL and MAC STATUS; a per-group cache refreshed on a background queue on
the Mac (the first request after a page opens is `NOT_AVAILABLE`), with
executable paths cached by pid and start time and only the needed IORegistry
keys read; router latency cached per router address.

---

## 1. Goal

MAC STATUS answers "what is my Mac doing right now" at a glance, and on one
key press answers "why": which apps load the CPU, whether the battery is
charging, whether the network or the router is slow, and where memory went.

The single dashboard becomes the first of five pages:

```text
1 OVERVIEW        CPU sparkline · RAM · SSD · BAT + charge/time · ↓/↑ · PRESS · THERM
2 CPU · TOP       CPU % + 60 s sparkline · P/E cores · GPU · LOAD · top 4 apps
3 POWER           battery % · charging · time to full/empty · draw W · adapter W
                  · health % · cycles · lowest Apple peripheral battery
4 NETWORK         ↓/↑ with 60 s sparklines · router ping · internet ping
                  · Wi-Fi RSSI and link rate · VPN
5 MEMORY · DISK   RAM split app/wired/compressed · swap · SSD free/total
                  · disk read/write rates
```

The resting overview keeps its current geometry. A Companion that does not
support the new detail operation still gets a working, slightly richer
overview.

Wireframes were agreed in the design discussion on 2026-09-28. Two changes
relative to those drafts: the overview keeps `SSD NN%` (free space in GB is on
page 5), and the NETWORK `TODAY` traffic row is replaced by `ROUTER` latency.
A daily total would need continuous background counting on the Mac, and its
accuracy would depend on Companion uptime.

## 2. Scope

In scope:

* Firmware-only: a 60-sample history of CPU, download and upload in
  `MacStatusService`, and a CPU sparkline on the overview. Works with any
  Companion that already supports protocol v2.
* Companion protocol v6:
  * `SYSTEM_METRICS` schema 2 adds power source and battery time to the
    overview payload;
  * a new `SYSTEM_DETAILS` operation and capability return one detail group
    per request.
* macOS `SystemDetailsCollector` for the four detail groups.
* A page model in the MAC STATUS Mini App with Left/Right navigation and a
  page indicator.
* Protocol README, fixtures, architecture, UI requirements, the device manual
  and the Companion README.

The ON AIR camera/microphone alert from the same discussion is **not** part of
this plan; see section 10.

## 3. Architecture

```text
Mac                                              Cardputer
───────────────────────────────                  ─────────────────────────────────────
MacSystemMetricsCollector ─┐                     MacStatusApp (page index, rendering)
  (+ power source, minutes)│                         │ setDetailPage / snapshot /
                           ├─ CompanionSession ─BLE─ │ details / history
SystemDetailsCollector ────┘   SYSTEM_METRICS        ▼
  cpu | power | network |      SYSTEM_DETAILS    MacStatusService
  memory groups                                    ├─ 1 s SYSTEM_METRICS poll (existing)
  (IOKit, libproc, CoreWLAN,                       ├─ 2 s SYSTEM_DETAILS poll, visible group only
   SCDynamicStore, ICMP)                           ├─ MacStatusHistory (60 samples)
                                                   └─ freshness: 3 s overview, 6 s details
                                                 CompanionService (operation-filtered completions)
```

### Protocol v6

* HELLO offers `[6, 5, 4, 3]`; it still carries at most four versions. A v2
  firmware (plan 037, before AI USAGE) no longer shares a version with the new
  Companion, the same trade-off plan 041 made for v1.
* Capability `SYSTEM_DETAILS = 6` is advertised only in v6 sessions.
* Operation `SYSTEM_DETAILS = 8`, request/response only, no events.
* v2–v5 sessions keep `SYSTEM_METRICS` schema 1 (24 bytes) byte for byte. A v6
  session uses schema 2 only; firmware rejects a schema that does not match
  the session version.

#### SYSTEM_METRICS schema 2 (v6)

Schema 1 layout with byte 0 = `2`, followed by:

| Offset | Size | Field |
| --- | --- | --- |
| 24 | 1 | power source: 1 battery, 2 AC charging, 3 AC not charging |
| 25 | 2 | battery minutes: to full while charging, to empty on battery |

Validity bit 7 is the power source and bit 8 is battery minutes. Total size is
27 bytes. Battery minutes are invalid while macOS is still calculating
(`-1` from IOPS) and on AC while not charging.

#### SYSTEM_DETAILS request

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | group: 1 CPU, 2 power, 3 network, 4 memory/disk |

Any other group value makes the request undecodable; it is not answered and
times out on the Cardputer. A group the Mac cannot collect at all is
`NOT_AVAILABLE` with an empty payload.

#### SYSTEM_DETAILS response header

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | details schema = 1 |
| 1 | 1 | group, echoed from the request |
| 2 | 2 | validity flags for this group |

The group body follows in field order. A field whose validity bit is clear
is still encoded, as zero, so every group has a fixed size except for the
string tails.

| Group | Fields in order (bit = validity bit) |
| --- | --- |
| 1 CPU | bit0 P-cluster % u8 · bit1 E-cluster % u8 · bit2 GPU % u8 · bit3 load ×100 u16 · bit4 process count u8 (0–4), then per process: CPU % of whole machine u8, name length u8 (1–20), ASCII name |
| 2 power | bit0 system draw deciwatts u16 · bit1 adapter watts u8 · bit2 health % u8 · bit3 cycle count u16 · bit4 peripheral battery % u8, name length u8 (1–16), ASCII name |
| 3 network | bit0 internet RTT ms u16 · bit1 router RTT ms u16 · bit2 Wi-Fi RSSI dBm i8 · bit3 Wi-Fi link Mbps u16 · bit4 VPN active u8 (0/1) |
| 4 memory/disk | bit0 app MiB u32, wired MiB u32, compressed MiB u32 · bit1 swap used MiB u32 · bit2 SSD free GB u16, SSD total GB u16 (decimal) · bit3 disk read KiB/s u32, disk write KiB/s u32 |

The largest body is CPU: 4 + 6 + 4 × 22 = 98 bytes, well within the
248-byte payload limit. Exact offsets are fixed in
`protocol/companion/README.md` together with the fixtures.

Names are printable ASCII (0x20–0x7E). The Mac transliterates to Latin, strips
diacritics, drops any other character and truncates to the byte limit. A name
that becomes empty is replaced with `APP`.

### macOS sources

Step 0 confirms each source on current macOS on Apple Silicon and on one Intel
or desktop Mac if available.

| Field | Source | Notes |
| --- | --- | --- |
| P/E cluster % | `host_processor_info` per CPU; cluster membership from IORegistry `IOPlatformDevice` `cluster-type` by `logical-cpu-id` | Intel or an incomplete map: bits clear. |
| GPU % | IORegistry `IOAccelerator` `PerformanceStatistics["Device Utilization %"]` | No entitlement needed. |
| Load | `getloadavg` (1 minute) | — |
| Top apps | `proc_listallpids`, `proc_pid_rusage` CPU-time deltas (converted with `mach_timebase_info`), `proc_pidpath` | Processes of other users return `EPERM` and are skipped. Helpers inside `X.app/` roll up into the outermost bundle, so Chrome helpers count as `GOOGLE CHROME`. |
| Power source, minutes | `IOPSCopyPowerSourcesInfo` (`IsCharging`, `TimeToEmpty`, `TimeToFullCharge`, power source state) | Same source as the current battery percent. |
| System draw | `AppleSmartBattery` `PowerTelemetryData.SystemLoad` if present, else `Amperage × Voltage` on battery | On AC without telemetry the bit is clear. |
| Adapter | `IOPSCopyExternalPowerAdapterDetails` watts | — |
| Health, cycles | `AppleSmartBattery` `BatteryData.NominalChargeCapacity / DesignCapacity` (else `AppleRawMaxCapacity`), `CycleCount` | Capped at 100%. |
| Peripheral | IORegistry `AppleDeviceManagementHIDEventService` `BatteryPercent` + `Product` | The lowest one only. AirPods are out of scope. |
| Internet / router RTT | Unprivileged ICMP (`SOCK_DGRAM`, `IPPROTO_ICMP`) to `1.1.1.1` and to the IPv4 router from `SCDynamicStore State:/Network/Global/IPv4` | 1 s timeout per probe; a router that drops ICMP is timed with a TCP connection to port 80, then 443 (a refused connection still counts). The bit is clear on timeout. |
| RSSI, link rate | CoreWLAN `rssiValue`, `transmitRate` | Works without Location permission; SSID is never read. |
| VPN | `SCDynamicStore` primary interface is `utun*`, `ipsec*` or `ppp*` | Full-tunnel VPN only; split tunnels show `OFF`. Private Relay must not count. |
| Memory split | `host_statistics64` (internal − purgeable, wired, compressor pages) | Same call as the overview. |
| Swap | `vm.swapusage` | — |
| SSD free/total | `/` `volumeAvailableCapacityForImportantUsage`, `volumeTotalCapacity` | Matches Finder. |
| Disk rates | IORegistry `IOBlockStorageDriver` `Statistics` byte counters, summed | First sample has no rate. |

### Mac-side cadence

`SystemDetailsCollector` samples a group only when that group is requested.
Delta-based fields (process CPU, disk rates) keep the previous counters per
group. If more than 10 seconds have passed since the last request of that
group, the next response has those bits clear, exactly like the network
bootstrap in plan 037. ICMP probes run at most every 5 seconds and only while
the network group has been requested within the last 10 seconds. The
request thread never waits for a probe; it returns the last completed result.

## 4. Ownership & Boundaries

| Component | Owns | May call | Must not call |
| --- | --- | --- | --- |
| `SystemDetailsCollector` (App target) | macOS API access, per-group counters, probe schedule | IOKit, libproc, CoreWLAN, SCDynamicStore, sockets | `CompanionSession`, encoding |
| `SystemDetails` (CompanionCore) | group models, validation, v6 encoding/decoding | — | macOS APIs |
| `CompanionSession` | version filter, answering SYSTEM_METRICS/SYSTEM_DETAILS | `SystemMetricsCollecting`, `SystemDetailsCollecting` | collector internals |
| connectivity `companion_protocol` | wire decode/encode, schema-by-version rules | — | services |
| `CompanionService` | request correlation, capability `SYSTEM_DETAILS`, operation-filtered completions | transport | Mac Status state |
| `MacStatusService` | overview and detail polling, history, freshness, group echo check | `CompanionService` | display, input |
| `MacStatusHistory` | 60-sample ring buffers and gap markers | — | `CompanionService` |
| `MacStatusApp` | page index, page availability, rendering diff | `MacStatusService` public API, display | `CompanionService`, protocol decode |

Page navigation is local view state, like LED Gallery effect selection. It
dispatches no ActionBus Actions because nothing is sent to the host.

## 5. State Model

Pages:

```text
available pages = [OVERVIEW]                              no SYSTEM_DETAILS capability
available pages = [OVERVIEW, CPU, POWER, NETWORK, MEMORY]  SYSTEM_DETAILS live

Right (`/` with or without Fn): next page, wrapping
Left  (`,` with or without Fn): previous page, wrapping
One page only: Left/Right do nothing, and no page indicator is drawn
Activation: always OVERVIEW
Capability SYSTEM_DETAILS lost while on a detail page: OVERVIEW
```

Detail polling in `MacStatusService`:

```text
Idle ──setDetailPage(g)──▶ Due(g) ──submit──▶ InFlight(g)
  ▲                          ▲                   │ completion: group == g → accept, Due after 2 s
  │                          └── 2 s ────────────┤ completion: group != g → discard, Due now
  └── setDetailPage(none) / stopMonitoring ──────┘ timeout/failure → Due after 2 s
```

* At most one SYSTEM_DETAILS request is outstanding. A page change while a
  request is in flight waits for that completion (or its 2-second timeout)
  instead of overlapping requests.
* On a page change the previous group's details are dropped immediately; the
  new page shows `--` until its first accepted response.
* Details are fresh for 6 seconds after acceptance. Older values show `--`.
* The overview SYSTEM_METRICS poll continues at 1 s on every page, because it
  feeds the history and the header values on pages 2 and 4.

History:

```text
append on every accepted fresh overview sample: cpu, download, upload
append a gap when a 1 s poll slot passes without a fresh sample
clear on startMonitoring, stopMonitoring and Companion session change
```

A sparkline draws only complete segments between adjacent non-gap samples. It
fills from the right edge, so an opened app shows a short line that grows over
60 seconds.

## 6. BDD Scenarios

### Scenario: Overview grows a CPU sparkline (happy path, any v2+ Companion)

Given:
- a v5 Companion session with SYSTEM_METRICS
- MAC STATUS was opened 10 seconds ago and received 10 fresh samples

When:
- the overview renders

Then:
- the CPU block shows `CPU NN%` and a sparkline covering the rightmost 10 of
  60 slots instead of the bar
- RAM, SSD and BAT keep their bars; no page indicator is drawn

### Scenario: Charging battery on a v6 Companion

Given:
- a v6 session; schema 2 reports battery 78%, AC charging, 102 minutes

When:
- the overview renders

Then:
- the BAT block shows a charging mark and `BAT 78% 1:42`
- with minutes invalid it shows `BAT 78%` and the charging mark only
- on battery it shows the time to empty without the mark

### Scenario: Browse the detail pages (happy path, repeated input)

Given:
- a v6 session with SYSTEM_DETAILS live
- MAC STATUS is on OVERVIEW

When:
- user presses Right five times

Then:
- pages go CPU → POWER → NETWORK → MEMORY → OVERVIEW
- each detail page sends one SYSTEM_DETAILS request for its own group on
  entry and then one every 2 seconds
- OVERVIEW sends no SYSTEM_DETAILS requests
- each detail header shows `N/5` at the right

### Scenario: Older Companion (unavailable dependency)

Given:
- a v5 session: SYSTEM_METRICS schema 1, no SYSTEM_DETAILS

When:
- MAC STATUS opens and user presses Left and Right

Then:
- only OVERVIEW exists; the keys do nothing and no page indicator is drawn
- the CPU sparkline works; BAT shows no charging mark or time
- no SYSTEM_DETAILS request is ever submitted

### Scenario: Partial details (desktop or Intel Mac)

Given:
- a v6 session; the power group reports no battery, the CPU group has no P/E
  bits

When:
- user opens POWER and CPU

Then:
- POWER shows `NO BATTERY` in place of the large value, and `--` in the
  battery-dependent rows; the adapter row shows its value if valid
- CPU shows `--` for `P` and `E`; the other fields render normally

### Scenario: Late completion after a page change (stale completion)

Given:
- CPU page with a CPU-group request in flight

When:
- user presses Right to POWER, then the CPU-group response arrives

Then:
- the CPU response is discarded by the group echo check and never shown on
  POWER
- the POWER request is submitted right after that completion

### Scenario: Detail request fails (in-flight failure)

Given:
- NETWORK page with fresh details

When:
- two consecutive SYSTEM_DETAILS requests time out

Then:
- network rows show `--` 6 seconds after the last accepted response
- the overview-derived header (`↓`/`↑` values and sparklines) keeps updating
- polling continues every 2 seconds with no burst of retries

### Scenario: First process sample (bootstrap)

Given:
- the CPU group was not requested in the last 10 seconds

When:
- user opens CPU

Then:
- the first response has the process bit clear and the rows show `--`
- the next response, 2 seconds later, lists up to four apps

### Scenario: Companion loss and reconnect

Given:
- MAC STATUS is on MEMORY with history

When:
- the Companion session disappears, then a Companion becomes Ready again

Then:
- MAC STATUS closes through MiniAppRuntime, as today; history and details
  are cleared
- reconnect does not reopen it; the next open starts on OVERVIEW with an
  empty sparkline

### Scenario: Old firmware and new Companion (compatibility)

Given:
- the new Companion offers `[6, 5, 4, 3]`

When:
- a v5 firmware connects

Then:
- the session selects v5; SYSTEM_METRICS uses schema 1 and SYSTEM_DETAILS is
  not advertised
- a v2-only firmware selects no shared version; this is the documented
  consequence of the four-version HELLO limit

### Scenario: Top apps respect macOS process boundaries (Companion)

Given:
- Chrome runs a main process and several helper processes; WindowServer runs
  as another user

When:
- the CPU group is collected twice

Then:
- Chrome helpers are summed into one `GOOGLE CHROME` entry
- processes owned by other users are skipped without error
- names are ASCII, at most 20 bytes, and are not written to logs or
  diagnostics

Reconnect with an open detail page is covered by the loss scenario, because
Companion loss closes the app. Repeated input is covered by page wrapping.

## 7. Architecture Invariants

- `MacStatusService` is the only consumer of SYSTEM_METRICS and
  SYSTEM_DETAILS completions.
- `MacStatusApp` never calls `CompanionService` and never decodes protocol
  payloads.
- At most one outstanding request per operation. SYSTEM_DETAILS is requested
  only while monitoring, only for the visible page, and never on OVERVIEW.
- Nothing is polled while MAC STATUS is closed. The Mac stops ICMP probes
  within 10 seconds after the last network-group request.
- v2–v5 sessions never carry schema 2 or SYSTEM_DETAILS; a v6 session never
  carries schema 1.
- History and details are never persisted and clear on every session change.
- Process and peripheral names never leave the Mac except as bounded ASCII
  in SYSTEM_DETAILS, and are never logged.
- macOS API access lives only in the App target's collectors; encoding and
  validation live in CompanionCore.

## 8. Tests mapped to scenarios

Firmware tests are Unity cases; Companion checks are named `expect` labels in
`CompanionCoreCheck`.

| Scenario | Test |
| --- | --- |
| History, gaps and clearing on session change | `test_history_appends_samples_gaps_and_clears_on_session_change` (test_mac_status) |
| Overview sparkline | `test_overview_draws_cpu_sparkline_from_history` |
| Charging battery on a v6 Companion | `test_v6_overview_reports_power_source_and_minutes`, `test_overview_battery_shows_charge_state_and_time` |
| Browse the detail pages | `test_detail_pages_wrap_and_request_only_visible_group`, `test_detail_page_content_matches_details`, `test_detail_polling_requests_only_the_selected_group` |
| Older Companion | `test_single_page_without_system_details_ignores_left_right` |
| Partial details | `test_detail_pages_render_placeholders_for_invalid_fields` |
| Late completion after a page change | `test_mismatched_group_completion_is_discarded` |
| Detail request fails | `test_detail_values_expire_after_six_seconds_without_retry_burst` |
| Companion loss and reconnect | `test_companion_loss_resets_pages_and_history`, existing `test_mac_status_requires_live_metric_capability` |
| Schema by version, AI_USAGE stays schema 3 | `test_system_metrics_schema_matches_session_version` (test_companion_protocol) |
| SYSTEM_DETAILS round trip, bounds, ASCII | `test_system_details_groups_round_trip_and_reject_bad_names` |
| Capability only in v6; malformed details fail one request | `test_system_details_capability_requires_v6` (test_companion_service) |
| Fixtures present | `test_companion_fixtures.py`, v6 fixtures added |
| HELLO offers 6,5,4,3 | "v6 hello offers fallback", "v6 hello advertises fallbacks" |
| Swift encode/decode and version filter | "v6 … details fixture", "v6 … details round trip", "v5 rejects SYSTEM_DETAILS", "metrics schema follows the session version" |
| Session answers the requested group | "v6 session answers the requested group", "uncollectable group is NOT_AVAILABLE" |
| Name sanitising | "Cyrillic is transliterated", "diacritics are stripped", "names must be printable ASCII" |
| Top apps respect process boundaries | "top apps roll up helpers and skip processes without a baseline", "helpers roll up into the outermost app" |
| First process sample (bootstrap) | "first process sample is only a baseline", "process baseline expires after ten seconds", "disk baseline expires" |
| Probe schedule | "probes run at most every five seconds", "probes stop ten seconds after the last network request" |
| Ownership invariants | review: `MacStatusApp` holds only `MacStatusService&` and the display, and its sources name no `CompanionService` or `connectivity::` symbol (an include rule cannot apply, because `mac_status_service.h` includes `companion_service.h`) |

## 9. Physical Acceptance

- Overview: the sparkline follows a CPU load (`yes > /dev/null` in one terminal
  tab), and its gaps appear while the Mac sleeps; readable at 10% backlight.
- Charging mark and minutes match the macOS battery menu within a few minutes
  when plugging and unplugging the adapter.
- CPU page: top apps roughly match Activity Monitor, filtered to the user's
  processes; P/E split and GPU % respond to a video export or a game.
- POWER page on a MacBook: draw W is plausible (compare with
  `sudo powermetrics` once); health and cycles match System Information; a
  Magic Mouse or Keyboard battery appears and turns `LOW` at 20% or less.
- NETWORK page: router RTT stays low while internet RTT rises under an upload;
  RSSI changes when moving away from the access point; no Location prompt
  appears; VPN toggles with a full-tunnel VPN and stays `OFF` with iCloud
  Private Relay.
- MEMORY page: app/wired/compressed values are close to Activity Monitor;
  disk rates react to copying a large file.
- A 98-byte CPU response arrives within the 2-second request timeout at the
  negotiated MTU; if not, record the time and raise the timeout for
  SYSTEM_DETAILS only.
- A v5 Cardputer firmware with the new Companion still shows the old overview.
- `make firmware-size`: the `hub` image stays within the 2 MiB CRUB partition.

## Implementation order

0. **macOS spike.** A throwaway command-line check for every row in the sources
   table. Record the results under "Current status" and remove unreadable
   fields from the layouts.
1. **Firmware history and sparkline** (no protocol change): `MacStatusHistory`,
   overview CPU sparkline. It can ship on its own.
2. **Protocol v6**: README, fixtures, both codecs, HELLO offer, capability.
3. **macOS collectors**: power fields for schema 2, then `SystemDetailsCollector`
   group by group behind injectable sources.
4. **Firmware detail polling**: `setDetailPage`, group echo, freshness.
5. **Pages UI**: page model, headers, the four detail layouts, page dots on
   the overview.
6. **Documentation**: `protocol/companion/README.md` (v1–v6),
   `docs/ARCHITECTURE.md` §35, `docs/UI_REQUIREMENTS.md` MAC STATUS paragraph,
   `docs/manuals/device-guide.md`, `companion/macos/README.md`, this plan's
   status and `docs/plans/README.md`.

Final gate: `make check` and `make companion-check`.

## 10. Out of Scope

- **ON AIR** camera/microphone indicator. It must work while MAC STATUS is
  closed, so it needs a Companion push event, a cross-app annunciator or
  takeover, and a Puzzle priority rule. Candidate plan 043.
- CPU/GPU temperatures and fan speed (private SMC keys, fragile across macOS
  releases); the thermal state stays the only thermal signal.
- Wi-Fi network name (requires Location permission).
- AirPods and third-party peripheral batteries.
- Mac-side history, daily traffic totals, and history that survives closing
  MAC STATUS.
- A configurable ping target, per-process memory ranking, and actions on
  processes (quit, force quit).
- A Unit Puzzle gauge for MAC STATUS.
