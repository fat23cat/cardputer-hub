# 049 — SERVICES HEALTH: public status pages

## Current status

Implemented on **2026-10-06**:
- `SERVICE_STATUS` (Companion operation 14);
- `IHttpClient` and `Esp32HttpClient`;
- the Statuspage parser, in firmware and in Swift;
- `ServiceStatusService`, the SERVICES HEALTH Mini App and the
  `WIFI_OR_COMPANION` capability;
- the `Fault` audio cue;
- the Companion's `URLSessionStatusPageFetcher`.

`make check`, `make companion-check` and the firmware build pass. The parser
read the four live pages correctly on the host.

Physical acceptance (section 9) has not been run. It covers TLS next to BLE on
the device, heap, the Companion route, and the Fault cue on the speaker.

### Requirement changes during the work

These are settled decisions; the sections below describe only the result.

| Change | Reason |
| --- | --- |
| kinopub.online removed | It is a Cronitor page with no JSON API; the user dropped it. |
| Cursor added | Requested; it is a Statuspage source like the others. |
| App renamed from SERVICES to **SERVICES HEALTH** | Requested. |
| The app opens only with Wi-Fi or Companion | Requested; `WIFI_OR_COMPANION` capability (section 3). |
| The route (Wi-Fi / Mac) is not shown | Not useful to the user; still kept in the snapshot. |
| The page summary (`All Systems Operational`) is not shown | It only repeats the level. |
| No row selection | Without descriptions there is nothing to select. |
| Detail screen on Enter dropped | Same reason. |
| Wait shown as `N SEC AGO` in the header, not a segment bar | A bar at the bottom was too distracting. |

## 1. Goal

Show at a glance whether GitHub, Anthropic (Claude), OpenAI or Cursor is
having an incident, in the spirit of fkhadra/esp-notify.

A page that gets worse plays a sound. Everything else stays inside the app:
there is no LED signal and no `/notify` endpoint.

## 2. Scope

- Four fixed Atlassian Statuspage sources. Each is read from
  `/api/v2/status.json`, about 250 bytes:
  - `www.githubstatus.com`
  - `status.claude.com`
  - `status.openai.com` (incident.io, with a Statuspage-compatible API)
  - `status.cursor.com`
- The app opens only while Wi-Fi is connected or a Companion session is ready.
  If both stay lost for 20 s while it is open, it closes.
- Checks run only while the app is open: at once on opening, then every 60 s
  after the previous round ends. R starts a round now.
- Route for each page:
  - direct HTTPS while Wi-Fi is connected;
  - otherwise the Mac Companion fetches the page;
  - the Companion also takes over when the direct fetch fails.
- A fault cue plays when a page gets worse, to MINOR or above.

## 3. Architecture

```text
ServiceStatusApp (apps/service_status)               status board, R → Action
   └─ ServiceStatusService (services/service_status)  round, route, levels, cue,
        │                                             WIFI_OR_COMPANION
        ├─ connectivity::IHttpClient ← hardware::Esp32HttpClient (own task, cert bundle)
        ├─ connectivity::WiFiService::state()   (Connected → direct route)
        ├─ CompanionService::requestServiceStatus(url) → Mac URLSessionStatusPageFetcher
        ├─ core::CapabilityRegistry              (WIFI_OR_COMPANION)
        └─ AudioService::play(AudioCue::Fault)
```

**Status pages.** `parseStatusPage()` reads `status.indicator` and
`status.description`, the latter cut to 48 bytes at a character boundary.
`StatusPageReport.parse` mirrors it in Swift. The description travels over the
wire and stays in the snapshot, but the screen does not show it.

**Launch gate.** The launcher can only require every listed capability at
once, so "Wi-Fi or Companion" cannot be expressed as two capabilities. Instead,
`ServiceStatusService::update()` publishes one capability, `WIFI_OR_COMPANION`,
on every loop pass whether or not the app is open. It is published while
`WiFiService` is `Connected` or `CompanionService` has a live session. The app
registers with that capability, so:
- without it, the launcher shows `REQUIRES WIFI_OR_COMPANION`;
- when it is removed, MiniAppRuntime closes the app.

It is withdrawn only after both routes have been gone for 20 s
(`linkLossGrace`), so a Wi-Fi retry or a Companion re-handshake does not close
an open app; a new route publishes it at once.

**HTTPS.** `Esp32HttpClient` runs `esp_http_client` with the ESP-IDF
certificate bundle on its own task:
- one GET at a time, on a task pinned to CPU1 at priority 1 so TLS crypto never
  preempts the UI loop on CPU0;
- 4 s per network operation and no operation started after 10 s (ESP-IDF's own
  timeout covers single operations only), so a request ends about 14 s after it
  connects; DNS inside the connect is bounded only by lwIP's retries;
- at most three redirects, followed only to https;
- a body of at most 2 KiB;
- `start()` reports `Started`, `Busy` or `Failed`, so a client that cannot
  start is never taken for an abandoned request.

The service gives up on a direct fetch after 15 s (`directFetchTimeout`) and
treats it as a Wi-Fi failure. While the client is still held by an abandoned
request (`Busy`), the page goes to the Companion, or fails after 15 s without
one. A Companion submit the protocol rejects (`Invalid`) fails the page
instead of being retried. A fault cue refused by a busy speaker is retried for
two seconds.

`sdkconfig.defaults` pins `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`, and
`check_esp_idf_config.py` requires it.

### Screen

```text
SERVICES HEALTH            10 SEC AGO
──────────────────────────────────────
01 GITHUB                        ■ OK
02 ANTHROPIC                  ■ MINOR
03 OPENAI                  ■ CRITICAL
04 CURSOR                     ■ MAINT

GITHUB UNREACHABLE        (only on a problem)
```

**Header.** `SERVICES HEALTH` sits on the left. The right slot uses the quiet
Ordinal colour:
- `CHECKING` during a round;
- otherwise the time since the last round, in ten-second steps: `0 SEC AGO`
  to `50 SEC AGO`.

**Rows.** Four fixed rows with a right-aligned level and a 7×7 mark:

| Level | Label | Mark |
| --- | --- | --- |
| Operational | `OK` | Leaf |
| Maintenance | `MAINT` | Blue |
| Minor | `MINOR` | Vermilion |
| Major | `MAJOR` | Vermilion |
| Critical | `CRITICAL` | Vermilion |
| Not checked yet, or no route | `--` | none |
| Fetch or parse failed | `ERROR` | none |

There is no selection plate.

**Problem line.** It sits below the rows and is empty when all is well.
Otherwise it shows one of:
- `NO WI-FI OR COMPANION`;
- `<NAME> UNREACHABLE`;
- `<NAME> NOT READABLE`;
- `N PAGES FAILED`.

**Repaint.** Only changed regions repaint. While waiting, the header slot
changes once every ten seconds. There is no animation and no key hint.

**Keys.**
- R checks now.
- Escape returns to Apps.
- Up/Down do nothing.

## 4. Ownership & Boundaries

| Component | Owns | May call | Must not call |
| --- | --- | --- | --- |
| `app_main` | service lifecycle | `serviceStatus.update()` | SERVICES HEALTH rendering |
| `ServiceStatusService` | round, route, snapshot, last known levels, fault cue, `WIFI_OR_COMPANION` | `IHttpClient`, `WiFiService::state()`, `CompanionService`, `CapabilityRegistry`, `AudioService` | Mini App methods |
| `ServiceStatusApp` | frame diffing | `setActive`, `snapshot()`, ActionBus | `IHttpClient`, `CompanionService`, `AudioService`, `update()` |
| `MiniAppRuntime` | opening and closing on the capability | `CapabilityRegistry` | the service |
| `Esp32HttpClient` | worker task, one GET, body ≤ 2 KiB | esp_http_client | status formats |
| Mac `CompanionSession` | answering SERVICE_STATUS for its own session | `StatusPageFetching` | anything but the named https URL |

## 5. State Model

**Service.** The service is either inactive or active. An active service is
in a round or waiting between rounds.

- In a round, page `i` is fetched over Wi-Fi or through the Companion, or it
  is marked NoConnection. Then `i` advances.
- After the last page, the service waits; `sinceRound` counts up from 0.
- At 60 s the next round starts. `sinceRound` is 0 during a round.

**Entry.** Each page's entry holds:
- level (Unknown, Operational, Maintenance, Minor, Major, Critical);
- description;
- route (None, WiFi, Companion);
- problem (None, NoConnection, Unreachable, Unreadable);
- whether it has been checked;
- the time since that check.

**Failure.** A failed check clears the shown level. `lastKnown` keeps the
latest successful level of this boot; the fault cue compares against it.

**Capability.** `WIFI_OR_COMPANION` follows the Wi-Fi state and the Companion
session on every update, independently of the app.

## 6. BDD Scenarios

### Scenario: Happy path over Wi-Fi
Given Wi-Fi is Connected.
When SERVICES HEALTH opens.
Then the service fetches the four pages one at a time and shows each level,
and the header reads `CHECKING`, then `0 SEC AGO`.

### Scenario: Launch gate
Given neither Wi-Fi nor a Companion session is available.
When the user selects SERVICES HEALTH in Apps.
Then it does not open and the launcher shows `REQUIRES WIFI_OR_COMPANION`.
If both stay gone for 20 s while it is open, it closes and Apps returns; a
shorter blip keeps it open.

### Scenario: Unavailable dependency
Given there is no Wi-Fi and Companion is Ready.
When SERVICES HEALTH opens.
Then each page is requested with SERVICE_STATUS. The screen looks the same as
over Wi-Fi.

### Scenario: In-flight failure
Given Wi-Fi and Companion are Ready.
When the direct fetch fails or returns a non-2xx status.
Then the same page is requested from the Companion. If that fails too, the
row reads `ERROR` and the problem line names the page.

### Scenario: Reconnect
Given a SERVICE_STATUS request is pending.
When the Companion session ends.
Then the page reads Unreachable and the request is not replayed.

### Scenario: Repeated input
Given a round is running.
When R is pressed again.
Then no second round starts. While the app is closed, the refresh Action is
rejected.

### Scenario: Stale completion
Given the app closed with a request in flight.
When the answer arrives.
Then it is dropped. An abandoned HTTP request holds the client until it ends,
and the next page waits for it.

### Scenario: Reopening with a pending Companion request
Given SERVICE_STATUS is still pending from before the app was closed.
When SERVICES HEALTH is reopened, even several times.
Then no second SERVICE_STATUS is sent until the pending one answers or times
out. The Companion keeps free slots for its heartbeat and stays connected.

### Scenario: HTTP client cannot start or runs too long
Given Wi-Fi is Connected.
When the HTTP client fails to start, or a direct fetch runs past 15 s.
Then the page goes to the Companion, or is marked Unreachable without one,
and the round continues; it never retries the dead client forever.

### Scenario: Waiting for the next round
Given a round has ended.
When time passes.
Then the header shows `0 SEC AGO`, `10 SEC AGO` … `50 SEC AGO`. The display
repaints only when the label changes. At 60 s the header reads `CHECKING`
again.

### Scenario: Fault cue
Given a page's last known level is OK.
When it reads MINOR or worse.
Then the Fault cue plays at most once in that round. The first answer,
recovery and maintenance are silent.

## 7. Architecture Invariants

- `app_main` alone calls `ServiceStatusService::update()`.
- `ServiceStatusApp` uses only `setActive`, `snapshot()` and the ActionBus.
- `ServiceStatusService` is the only consumer of SERVICE_STATUS completions and
  the only publisher of `WIFI_OR_COMPANION`.
- `IHttpClient` knows nothing about status formats. The parser knows nothing
  about networks.
- Nothing is fetched while SERVICES HEALTH is closed.
- At most one SERVICE_STATUS is outstanding at any time, across reopenings.
- No page waits on the HTTP client for more than 15 s: a running fetch is
  abandoned and a `Busy` client is routed around or failed.
- The Mac fetches only an `https://` URL the Cardputer names. It returns only a
  level and at most 48 bytes of text.

## 8. Tests mapped to scenarios

| Scenario | Test |
| --- | --- |
| Happy path over Wi-Fi | `test_opening_checks_every_page_over_wifi_one_at_a_time` |
| Poll interval | `test_pages_are_checked_again_every_minute_while_open` |
| Launch gate | `test_the_link_capability_follows_wifi_and_the_companion`; MiniAppRuntime's capability tests |
| Closed app | `test_nothing_is_fetched_while_closed`, `test_closing_stops_the_checks` |
| Unavailable dependency | `test_without_wifi_the_companion_fetches_the_page`, `test_pages_read_through_the_mac_look_the_same`, `test_without_any_connection_every_page_reports_no_connection` |
| In-flight failure | `test_a_failed_wifi_fetch_falls_back_to_the_companion`, `test_wifi_failure_without_a_companion_marks_the_page_unreachable`, `test_a_failed_page_is_named_below_the_list` |
| Reconnect | `test_companion_loss_during_a_request_fails_only_that_page` |
| Stale completion | `test_an_answer_after_closing_is_ignored`, `test_an_abandoned_wifi_request_delays_the_next_page`; Swift "an answer for an ended session is dropped" |
| Repeated input | `test_refresh_starts_a_round_once_and_only_while_open`, `test_r_checks_every_page_again` |
| Reopening with a pending Companion request | `test_reopening_waits_for_the_pending_companion_request` |
| Brief route loss | `test_a_brief_route_loss_keeps_the_link_published` |
| Client held by an abandoned request | `test_a_busy_http_client_routes_the_page_through_the_companion`, `test_a_busy_http_client_without_a_companion_fails_after_the_fetch_timeout` |
| Busy speaker | `test_a_fault_cue_waits_for_a_busy_speaker` |
| HTTP client cannot start or runs too long | `test_an_http_start_failure_falls_back_to_the_companion`, `test_an_http_start_failure_without_a_companion_ends_the_round`, `test_a_wifi_fetch_that_runs_too_long_is_abandoned` |
| Waiting for the next round | `test_the_header_counts_since_the_last_check_in_ten_second_steps`, `test_an_unchanged_screen_is_not_repainted` |
| Fault cue | `test_a_worse_incident_plays_one_fault_cue_per_round`, `test_fault_cue_rotates_four_keys_and_releases_to_digital_silence` |
| Screen | `test_levels_show_labels_and_marks_without_the_page_summary`, `test_there_is_no_selection_plate`, `test_without_any_connection_the_caption_explains_it`; captures `service-status-checking`, `-list`, `-list-waiting`, `-offline` |
| Wire format | `test_service_status_round_trip_and_rejects_bad_urls`; Swift `statusPageChecks` |
| Parser | `test_status_page/*`; Swift parser checks |
| Ownership invariants | `scripts/check_architecture.py` and review |

## 9. Physical Acceptance

1. With Wi-Fi connected and Companion closed:
   - open SERVICES HEALTH;
   - the four rows fill within a few seconds;
   - the header goes from `CHECKING` to `0 SEC AGO` and steps every 10 s.

   The serial log shows each GET with the free heap. Record the lowest value
   seen while BLE is connected.
2. With Wi-Fi turned off in Settings and Companion connected: the rows still
   fill.
3. With Wi-Fi off and Companion closed:
   - Apps shows SERVICES HEALTH as unavailable (`REQUIRES WIFI_OR_COMPANION`);
   - turning both off while it is open closes it after about 20 s;
   - toggling Wi-Fi off and on quickly keeps it open.
6. Watch the UI during a round: key presses and rendering stay smooth while
   TLS handshakes run.
4. Close SERVICES HEALTH during a round: no further GET appears in the log.
5. Check that the Fault cue sounds like the reference error gesture on the
   speaker.

If TLS does not fit next to NimBLE, agree on a configuration change before
making it, for example `CONFIG_MBEDTLS_DYNAMIC_BUFFER` or a smaller
`SSL_IN_CONTENT_LEN`.

## 10. Out of Scope

- Cronitor or other non-Statuspage sources.
- Editing the list on the device.
- An LED indicator.
- Polling while the app is closed.
- A `/notify` endpoint.
- Showing component-level detail, the page summary or the route.
