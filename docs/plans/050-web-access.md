# 050 — WEB ACCESS: Wi-Fi access point and local web UI

## Current status

Implemented on **2026-10-11**:
- `IWifiAccessPointAdapter` and AP+STA in `Esp32WifiAdapter`;
- `IHttpServer` and `Esp32HttpServer`;
- `WebAccessService` with credentials, sign-in, sessions and routing;
- `InventoryWebEndpoint`;
- the page `web/index.html` and its generated asset;
- the WEB ACCESS Mini App, its Launcher icon and the Home `WEB` group;
- the large password and the full-screen QR join code (Q), with a QR Code
  encoder in `core/display/qr_code`.

Host tests, the architecture rules and the firmware build pass. The page was
exercised in a browser, at desktop and phone widths, against a local stand-in
for the API, including the review findings below. The QR encoder's output, and
the join code as the app draws it, were read back by a reference QR decoder.
Physical acceptance (section 9) has not been run.

Mockups: the "Hub Wi-Fi Inventory" design canvas (Cardputer, phone and
desktop screens).

### Requirement changes during the work

| Change | Reason |
| --- | --- |
| The web UI is a shell for several apps, with a start screen of tiles and header tabs | Requested; Inventory is the first app. |
| The server runs in the background | Requested; it lives in a Service, the Mini App only shows it. |
| The app is opened from APPS as `WEB ACCESS` | Decided with the mockups. |
| No `Starting` state | Starting is synchronous; a failure goes straight to `Error`. |
| Password shown at 2× in groups of four | The first device test could not read it reliably at the system scale. |
| Bluetooth pauses while WEB ACCESS runs | On the device an iPhone could not join ("Failed to join", "Unable to join") until Bluetooth was turned off; the user chose a pause over tuning BLE timing. `HostService::setRadioHold()` keeps the saved setting. |
| Q shows a full-screen Wi-Fi QR code | Requested for the iPhone; iOS cannot join Wi-Fi from an NFC tag, so NFC is not used. |
| Home-screen icon and manifest | Requested, to add the page to the iPhone home screen; the icons are drawn by the generator. |
| No flashing between pages | Requested; fixed in the page without a framework: cached rendering, in-place updates, delayed loading notice. |
| Third review fixes | Credentials that storage could not keep stay the same until restart; a background list refresh keeps focus in the search field; Bluetooth shows `PAUSED` while held. A per-client sign-in lockout was not added: the page password is the Wi-Fi password, so every client already holds it. |
| Second review fixes | A late Reload reply never replaces a newer selection or draft; work finishing after a page change does not draw; typing during a save keeps it in flight (one PUT); the Bluetooth toggle flips the saved setting while WEB ACCESS holds the radio; every receive, headers included, stops when the server stops. |
| Review fixes | Edits typed during a save are kept; the automatic first selection updates the unsaved-edits check; signing out clears the inventory cache; Reload after an expired session stays on sign-in; a damaged record's delete shows conflicts and errors; `stop()` no longer waits for a stalled upload. |

## 1. Goal

A phone or a computer joins a Wi-Fi network that the Cardputer itself
provides, signs in, and works with Hub data in a browser. The first web app is
Inventory: read, edit and delete the NFC container records that the macOS
Companion edits today.

The web UI is a shell for several apps, not an Inventory page. The server keeps
running in the background while the user does something else on the
Cardputer.

## 2. Scope

- **WEB ACCESS Mini App** in APPS, after NFC. Enter starts the service; while it
  runs, the screen shows the network name, the password (2×, in groups of four),
  the address and the login; Q shows a full-screen QR code that joins the
  network from a phone camera; Esc leaves it running in the background; Enter
  stops it.
- **Home** shows a `WEB` status group with a dot while the service is not Off.
- **Access point:** WPA2-PSK, SSID `HUB-XXXX` (four random uppercase hex
  digits), a random twelve-character password from an unambiguous lowercase
  alphabet, address `192.168.4.1`, at most four clients. Generated once,
  kept in NVS, reused on every start. It runs beside the station connection
  (AP+STA) when Wi-Fi is enabled; the station is not changed.
- **HTTP server** on port 80 of the access-point interface only.
- **Sign-in:** login `admin`, the access-point password. A session cookie
  (128-bit random token, `HttpOnly; SameSite=Strict`), at most four sessions,
  30 minutes idle; every session ends when the service stops.
- **Web shell:** one page served from firmware, phone and desktop layouts.
  Start screen with app tiles and a DEVICE block, header tabs for quick
  switching, sign out.
- **Inventory web app:** paged list with search over names and descriptions,
  editor with the same limits and revision check as the Companion, delete with
  confirmation, damaged-record handling, conflict handling.
- Off at every boot. Turning it on is always a user action.

## 3. Architecture

```text
Browser ──Wi-Fi AP──► Esp32HttpServer (httpd task)
                         │ one request at a time, copied
                         ▼
                 IHttpServer::poll() / respond()     connectivity/http
                         │
                         ▼   main loop
                  WebAccessService                   services/web_access
                  ├─ lifecycle: AP + server
                  ├─ credentials (NVS "web/access")
                  ├─ sessions, sign-in throttle, CSRF header
                  ├─ static page, /api/session, /api/hub
                  └─ /api/<app>/… ──► IWebApp
                                         └─ InventoryWebEndpoint   services/inventory
                                               └─ InventoryService (all record writes)
WebAccessApp (Mini App) ──ActionBus "web.set-enabled"──► WebAccessService
ApplicationShell (Home) ──status()──► WebAccessService
```

### Connectivity

- `connectivity/wifi/wifi_access_point.h`: `WifiAccessPointConfig` (SSID,
  passphrase, channel-free), `validWifiAccessPointConfig()` (SSID 1–32 bytes,
  WPA2 passphrase 8–63 printable ASCII), and `IWifiAccessPointAdapter`
  (`startAccessPoint`, `stopAccessPoint`, `accessPointClientCount`). The
  station contract of `IWifiAdapter` and `WiFiService` does not change.
- `connectivity/http/http_server.h`: `HttpRequest` (id, method, path, query,
  cookie, content type, request-marker header, body ≤ 5 KiB), `HttpResponse`
  (status, content type, extra headers, owned body or static body), and
  `IHttpServer` (`start`, `stop`, `running`, `poll`, `respond`). It knows
  nothing about routes or sessions.

### Hardware

- `Esp32WifiAdapter` implements both Wi-Fi interfaces over one driver. It
  tracks which interfaces are active and switches between `STA`, `AP` and
  `APSTA`; initializing the station after the access point (or the reverse)
  no longer fails. Station state accepts `APSTA`.
- `Esp32HttpServer` wraps `esp_http_server`: one server task, four sockets,
  LRU purge, a one-second receive and two-second send timeout, three seconds
  for a body; stop() abandons a body still arriving. A URI wildcard handler rejects a
  request whose local address is not the access-point address, copies the
  request into the single slot, and waits at most three seconds for the main
  loop's response (then 503). Bodies over the limit are refused with 413
  before they are read.

### Services

- `WebAccessService` owns the lifecycle (`Off`, `Running`, `Error`),
  credentials, sessions, routing and the app list. It handles the
  `web.set-enabled` Action (`enabled` bool). `update()` runs once per main loop
  and answers at most one request per call.
- `IWebApp` (`services/web_access/web_app.h`): `id()`, `title()`,
  `summaryJson()` for the start tile, and `handle(request, subpath)`.
- `InventoryWebEndpoint` implements `IWebApp` for `inventory` on top of
  `InventoryService::listRecords/getRecord/putRecord/deleteRecord`. Records
  travel as their canonical JSON; uploads are decoded with
  `decodeInventoryRecord()`, exactly as the Companion does.
- The page is `web/index.html` (HTML, CSS and JavaScript in one file), gzipped
  by `scripts/generate_web_ui.py` into the checked-in
  `src/services/web_access/web_ui_asset.h`. A Python test keeps them in step.

### HTTP API

| Method and path | Auth | Result |
| --- | --- | --- |
| `GET /` | no | The page (gzip). Any other non-API GET is 404. |
| `POST /api/session` `{"login","password"}` | no | 204 + cookie; 401; 429 while throttled |
| `DELETE /api/session` | yes | 204, cookie cleared |
| `GET /api/hub` | yes | device block and the app list with summaries |
| `GET /api/inventory/records?after=<id>` | yes | `{"records":[…],"next":<id or null>,"total":N,"limit":256}` |
| `GET /api/inventory/records/<id>` | yes | canonical record JSON; 404; 422 damaged |
| `PUT /api/inventory/records/<id>` | yes | body = record JSON at the expected revision; 200 `{"revision":N}`; 409 `{"revision":current}`; 400; 404; 503 |
| `DELETE /api/inventory/records/<id>?revision=N` | yes | 204; 409; 404; 503 (`revision=0` removes a damaged file) |

A list page holds at most 16 records and stops before 6 KiB of JSON. A
damaged record appears as `{"id":"…","damaged":true}`. Every request other
than `GET` must carry `X-Hub-Request: 1`. Errors are
`{"error":"<code>"}`.

## 4. Ownership & Boundaries

| Component | Owns | May call | Must not call |
| --- | --- | --- | --- |
| `app_main` | object lifetime, `webAccess.update()` once per loop | every layer | route or session logic |
| `Esp32HttpServer` | httpd task, sockets, the request slot | `esp_http_server`, lwIP socket queries | any Service, `InventoryService` |
| `Esp32WifiAdapter` | the Wi-Fi driver, STA and AP netifs, mode | `esp_wifi`, `esp_netif` | Services |
| `WebAccessService` | lifecycle, credentials, sessions, routing | `IWifiAccessPointAdapter`, `IHttpServer`, `core::Storage`, `IRandomSource`, `IWebApp` | display, Mini Apps, `esp_*` |
| `InventoryWebEndpoint` | the inventory HTTP mapping | `InventoryService` domain operations | `InventoryStore`, file storage, `IHttpServer` |
| `WebAccessApp` | WEB ACCESS screen state | `status()`, ActionBus | `WebAccessService` mutators, connectivity |
| `ApplicationShell` | the Home `WEB` group | `WebAccessService::status()` | lifecycle changes |
| `WebAccessBluetoothPause` | holding Bluetooth while Running | `WebAccessService::status()`, `HostService::setRadioHold()` | saved host settings, `BluetoothService` |

## 5. State Model

```text
Off ──web.set-enabled(true)──► Running
 ▲  (credentials, AP, server)     │ the server stops by itself
 │       │ a step fails           ▼
 │       └──────────────────────► Error ──web.set-enabled(true)──► Running
 └──── web.set-enabled(false), from any state
```

Enabling loads or creates the credentials, starts the access point, then the
server, synchronously. A failure at any step rolls back what started and enters
`Error`. Leaving `Running` clears every session, resets every app, and ends a
request still waiting in the server.

## 6. BDD Scenarios

### Scenario: Start from the Mini App

Given:
- WEB ACCESS is Off and no credentials are stored

When:
- the user opens WEB ACCESS and presses Enter

Then:
- the app dispatches exactly one `web.set-enabled` with `enabled = true`
- credentials are generated, stored, and the access point starts with them
- the server starts and the state is Running
- the screen shows NETWORK, PASSWORD, OPEN `192.168.4.1` and LOGIN `admin`

### Scenario: Keep running in the background

Given:
- WEB ACCESS is Running

When:
- the user presses Esc

Then:
- the Mini App closes and the service stays Running
- Home shows the `WEB` group with a Leaf dot

### Scenario: Stored credentials are reused

Given:
- credentials are stored

When:
- the service starts

Then:
- the access point uses the stored SSID and password; nothing is generated

### Scenario: Access point cannot start (unavailable dependency)

Given:
- the adapter refuses to start the access point

When:
- the service is enabled

Then:
- the state is Error, the server is not started, Home shows a Vermilion dot
- Enter on WEB ACCESS retries

### Scenario: Sign in

Given:
- Running

When:
- `POST /api/session` arrives with `admin` and the password

Then:
- 204 with a `hub_session` cookie; a later `GET /api/hub` with it is 200

### Scenario: Wrong password, repeated

Given:
- Running

When:
- five sign-ins with a wrong password arrive

Then:
- each is 401; the next sign-in, even a correct one, is 429 for 30 s

### Scenario: Request without a session or marker

Then:
- an API request without a valid cookie is 401
- a non-GET request without `X-Hub-Request: 1` is 403 and changes nothing

### Scenario: Edit a record

Given:
- signed in; record R at revision 3

When:
- `PUT /api/inventory/records/R` with its JSON at revision 3

Then:
- InventoryService commits revision 4; the response is `{"revision":4}`

### Scenario: Changed on the Cardputer (stale completion)

Given:
- the browser loaded R at revision 3; the Mac or the NFC app saved revision 4

When:
- the browser saves at revision 3

Then:
- 409 `{"revision":4}`; nothing is written

### Scenario: microSD missing (unavailable dependency)

Then:
- list, get, put and delete answer 503 `{"error":"storage"}`

### Scenario: Service stops while a session is open (in-flight failure)

Given:
- signed in; a request is waiting in the server slot

When:
- the user stops WEB ACCESS

Then:
- the waiting request is answered 503, the server and access point stop,
  every session is gone
- after a restart (reconnect) the old cookie is 401; nothing is replayed

### Scenario: Paged list

Given:
- 40 records

When:
- the browser follows `next` from the first page

Then:
- every record arrives once, in ID order, each page within the bounds

Repeated input: a second `web.set-enabled(true)` while Running is accepted
without restarting anything (`test_repeated_enable_restarts_nothing`).

## 7. Architecture Invariants

- Only `app_main` calls `WebAccessService::update()`, once per main loop.
- `InventoryService` is called only from the main loop; the httpd task never
  touches a Service.
- Only `src/hardware/esp32/http/esp32_http_server.cpp` includes
  `esp_http_server.h`.
- The server answers only on the access-point interface.
- `InventoryWebEndpoint` never includes file storage; every record write goes
  through `InventoryService`.
- `WebAccessApp` changes the service only through ActionBus.
- Credentials, session tokens, request bodies and client addresses are never
  logged.
- The service is Off at boot.

## 8. Tests mapped to scenarios

| Scenario | Test |
| --- | --- |
| Start from the Mini App | `test_web_access_app::test_enter_dispatches_start`, `test_web_access_service::test_enable_generates_and_stores_credentials` |
| Keep running | `test_web_access_app::test_running_screen_shows_credentials`, `test_application_shell::test_home_shows_web_group_while_web_access_runs` |
| Stored credentials | `test_web_access_service::test_stored_credentials_are_reused` |
| AP cannot start | `test_web_access_service::test_access_point_failure_enters_error` |
| Sign in | `test_web_access_service::test_sign_in_sets_session_cookie` |
| Wrong password | `test_web_access_service::test_sign_in_throttles_after_failures` |
| No session or marker | `test_web_access_service::test_api_requires_session`, `test_mutation_requires_marker` |
| Edit a record | `test_inventory_web::test_put_commits_next_revision` |
| Changed on the Cardputer | `test_inventory_web::test_put_conflict_reports_current_revision` |
| microSD missing | `test_inventory_web::test_storage_unavailable_is_503` |
| Stop while signed in | `test_web_access_service::test_stop_answers_waiting_request_and_clears_sessions` |
| Paged list | `test_inventory_web::test_list_pages_follow_next` |
| Page asset in step | `test_python/test_web_ui_asset.py` |
| Bluetooth paused while running | `test_host_service::test_web_access_holds_bluetooth_only_while_it_runs`, `test_radio_hold_stops_bluetooth_without_changing_the_saved_setting`, `test_radio_hold_release_keeps_saved_off`, `test_radio_hold_ends_pairing` |
| QR join code | `test_qr_code` (golden symbol verified by a decoder), `test_web_access_app::test_q_shows_the_join_code_full_screen` |
| httpd isolation, no file access, actions only | `test_python/test_web_architecture.py` |

## 9. Physical Acceptance

On Cardputer-Adv with microSD, BLE and the Companion connected:

1. WEB ACCESS starts; an iPhone and a Mac join `HUB-XXXX`, open
   `http://192.168.4.1`, sign in.
2. The start screen, Inventory list, search, edit, delete and conflict work on
   both. A save shows up on the Cardputer when the tag is tapped.
3. Esc keeps the server up; MAC STATUS, NFC and Home stay responsive while the
   browser works.
4. Station Wi-Fi connected at the same time: both keep working; the server
   does not answer on the home network address.
5. Free heap after start and after 15 minutes of use; no reset.

## 10. Out of Scope

- Captive portal, mDNS name, HTTPS.
- Changing the password or SSID on the device, and autostart at boot.
- Creating records from the browser (a record needs a tag on the reader).
- Other web apps; only the extension point exists.
- Station-mode web access on the home network.
