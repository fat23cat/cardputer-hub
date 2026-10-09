# Companion Protocol

Firmware and the macOS Companion share this wire contract and the binary
fixtures in `fixtures/`. They do not share implementation code.

There is one wire format and no version negotiation: firmware and Companion
are always built from the same commit and updated together.
`generate_fixtures.py` defines every layout below and writes the fixtures,
`companion_fixtures.h`, `src/connectivity/companion/companion_fingerprint.h`
and `companion/macos/Sources/CompanionCore/ProtocolFingerprint.swift`. The
protocol fingerprint is the first 8 bytes of SHA-256 over the generator
source, so any wire change must be made there, and rerunning it changes the
fingerprint on both sides. `test_python/test_companion_fixtures.py` fails when
committed outputs differ from what the generator writes.

All multi-byte integers are little-endian. A logical protocol message is at
most 256 bytes.

## GATT

| Role | UUID | Properties | Security |
| --- | --- | --- | --- |
| Service `CARDPUTER_COMPANION_SERVICE` | `07B23AB1-3938-418A-8E16-0AEC1CAA517F` | Primary | — |
| `HOST_TO_DEVICE` | `792B8431-054D-4758-B198-3EE728EA6FE1` | Write With Response | Encrypted + authenticated |
| `DEVICE_TO_HOST` | `BE3869D8-8F00-4D4B-A4CA-954A2D5B3EE1` | Notify | Encrypted + authenticated |

Semantic operations do not have dedicated characteristics. GATT carries framed
protocol messages only.

## Chunk frame

Each ATT write or notification value is one chunk:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | message ID |
| 1 | 1 | chunk index, 0-based |
| 2 | 1 | chunk count, 1–16 |
| 3 | n | payload bytes |

Limits: 256-byte logical message, 16 chunks, 2-second reassembly timeout.
A sender may use any chunk payload up to its negotiated write length minus the
3-byte header. The Mac writes chunks of `MTU − 6` bytes (at least 17, at most
256), so most responses take one ATT write; the Cardputer notifies in 17-byte
chunks.
Partial payloads never reach `CompanionService`.

## Envelope

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | frame marker `0xC7` |
| 1 | 1 | kind |
| 2 | 2 | session generation |
| 4 | 1 | request ID (`0` if unused) |
| 5 | 1 | operation |
| 6 | 1 | status (`0` unless RESPONSE or a mismatch HELLO_ACK) |
| 7 | 1 | payload length |
| 8 | n | payload |

Header is 8 bytes. Payload length is at most 248.

Decode and encode reject another marker, unknown kind/operation/status,
illegal `(kind, operation)` pairs, `status != OK` on REQUEST, EVENT and HELLO,
HELLO with a non-zero session or request ID, a HELLO_ACK that is neither
accepted (session non-zero, `OK`) nor mismatch (session `0`, `UNSUPPORTED`),
REQUEST/RESPONSE with session or request ID `0`, EVENT with a non-zero request
ID, PING payloads other than 4 bytes, non-empty APP_ACTIVE, SYSTEM_METRICS,
AI_USAGE and AI_AGENT_STATUS requests, APP_ACTIVATE requests without a valid bundle identifier,
SERVICE_STATUS requests without an https URL,
and any payload that does not match its layout exactly.

Frames from Companion builds before plan 043 start with a protocol version
`1`–`6` instead of the marker. The firmware recognizes such a HELLO only to
report the mismatch; it never answers it.

### Kinds

| Value | Name |
| --- | --- |
| 1 | HELLO |
| 2 | HELLO_ACK |
| 3 | REQUEST |
| 4 | RESPONSE |
| 5 | EVENT |

### Operations

| Value | Name | Used by |
| --- | --- | --- |
| 0 | NONE | HELLO, HELLO_ACK |
| 1 | PING | REQUEST, RESPONSE |
| 3 | APP_ACTIVE | REQUEST, RESPONSE |
| 4 | APP_ACTIVATE | REQUEST, RESPONSE |
| 5 | APP_ACTIVE_CHANGED | EVENT |
| 6 | SYSTEM_METRICS | REQUEST, RESPONSE |
| 7 | AI_USAGE | REQUEST, RESPONSE |
| 8 | SYSTEM_DETAILS | REQUEST, RESPONSE |
| 9 | INVENTORY_LIST | REQUEST (Mac), RESPONSE (Cardputer) |
| 10 | INVENTORY_GET | REQUEST (Mac), RESPONSE (Cardputer) |
| 11 | INVENTORY_PUT | REQUEST (Mac), RESPONSE (Cardputer) |
| 12 | INVENTORY_DELETE | REQUEST (Mac), RESPONSE (Cardputer) |
| 13 | AI_AGENT_STATUS | REQUEST, RESPONSE |
| 14 | SERVICE_STATUS | REQUEST, RESPONSE |

Value `2` (the former CAPABILITIES) is unused and rejected.

### Status

| Value | Name |
| --- | --- |
| 0 | OK |
| 1 | NOT_AVAILABLE |
| 2 | NOT_FOUND |
| 3 | UNSUPPORTED |
| 4 | MALFORMED |
| 5 | CONFLICT |
| 6 | REJECTED |
| 7 | STORAGE_ERROR |

## Session start

HELLO and HELLO_ACK carry the same payload: protocol fingerprint (8 bytes),
build ID length (1 byte, `1..24`), build ID (printable ASCII). A build ID is
`YYYY-MM-DD <commit>`, with `+` after the commit for a build from uncommitted
changes, for example `2026-09-29 abc1234`.

1. The Mac sends HELLO with its fingerprint and Companion build ID.
2. When the fingerprints match, the Cardputer replies HELLO_ACK with a new
   session generation, status `OK`, its own fingerprint and firmware build ID,
   then requests APP_ACTIVE. The session is ready when that answer arrives.
3. When they differ, the Cardputer replies HELLO_ACK with session `0`, status
   `UNSUPPORTED`, its fingerprint and firmware build ID, and starts no
   session. Both sides show which build to update (the one with the older
   build date; both when the dates are equal or unknown). Nothing else is
   exchanged until a new HELLO.
4. A pre-043 HELLO gets no reply; the Cardputer asks for a Companion update.

Cardputer sends PING heartbeats. The Mac sends APP_ACTIVE_CHANGED events.
Requests normally go from the Cardputer to the Mac; the inventory operations
are the exception and go from the Mac to the Cardputer. Each side numbers and
correlates its own requests. The Cardputer accepts inventory requests only for
the current session, including the final handshake step (a request from an
earlier session is ignored, one outside a live session is a protocol error),
queues at most two and answers a third with `NOT_AVAILABLE` immediately. The Mac
keeps one inventory request outstanding.

## Payloads

* PING: 4-byte token, echoed by the response.
* APP_ACTIVE / APP_ACTIVATE / APP_ACTIVE_CHANGED: `length` then UTF-8 bundle identifier, 1–128 bytes. APP_ACTIVE may return `NOT_AVAILABLE` with an empty payload. APP_ACTIVATE may return `NOT_FOUND`. APP_ACTIVE_CHANGED with an empty payload and status `OK` means there is no active bundle.
* SYSTEM_METRICS request: empty. `OK` response: the fixed 26-byte payload below. `NOT_AVAILABLE` or `MALFORMED`: empty response. Individual unavailable fields are represented by clear validity bits, not a failed response.

| Offset | Size | SYSTEM_METRICS response field |
| --- | --- | --- |
| 0 | 2 | validity bits: CPU 0, RAM 1, pressure 2, SSD 3, battery 4, network 5, thermal 6, power source 7, battery minutes 8 |
| 2 | 1 | CPU percentage |
| 3 | 4 | physical RAM used estimate, MiB |
| 7 | 4 | physical RAM total, MiB |
| 11 | 1 | pressure: normal 1, warning 2, critical 3 |
| 12 | 1 | root-volume disk used percentage |
| 13 | 1 | battery percentage |
| 14 | 1 | thermal: normal 1, fair 2, serious 3, critical 4 |
| 15 | 4 | download KiB/s |
| 19 | 4 | upload KiB/s |
| 23 | 1 | power source: battery 1, AC charging 2, AC not charging 3 |
| 24 | 2 | battery minutes: to full while charging, to empty on battery |

Values with clear validity bits are ignored. Percentages must be 0–100 when
valid. Unknown pressure or thermal state uses a clear validity bit. The first
CPU and network samples may be unavailable while their rate baselines form.
The Mac clears bit 8 while macOS is still estimating and on AC while not
charging. A Mac without an internal battery clears bits 4, 7 and 8.

* SYSTEM_DETAILS request: one byte, group `1=CPU`, `2=power`, `3=network`, `4=memory/disk`. Any other value makes the request invalid; it is not answered and times out on the Cardputer. `OK` response: the group layout below. `NOT_AVAILABLE` or `MALFORMED`: empty response. The Cardputer requests only the group of its visible page.

| Offset | Size | SYSTEM_DETAILS response header |
| --- | --- | --- |
| 0 | 1 | group, echoed from the request |
| 1 | 2 | validity bits for this group |

Every field is encoded in order even when its validity bit is clear; the Mac
then writes zero. Names are printable ASCII (`0x20`–`0x7E`); the Mac
transliterates to Latin, strips diacritics, drops other characters and
truncates. An empty name becomes `APP`.

| Group | Body after the header |
| --- | --- |
| 1 CPU | P-cluster % (1, bit 0), E-cluster % (1, bit 1), GPU % (1, bit 2), app count (1, `0..4`, non-zero only with bit 3); then per app: CPU % of the whole machine (1, `0..100`), name length (1, `1..20`), name |
| 2 power | system draw in deciwatts (2, bit 0), adapter watts (1, bit 1), battery health % (1, bit 2), cycle count (2, bit 3), lowest Apple peripheral battery % (1, bit 4), peripheral name length (1, `1..16` with bit 4, otherwise `0`), name |
| 3 network | internet round trip ms (2, bit 0), router round trip ms (2, bit 1), Wi-Fi RSSI dBm signed (1, bit 2), Wi-Fi link rate Mbps (2, bit 3) |
| 4 memory/disk | app, wired and compressed memory MiB (3 × 4, bit 0), swap used MiB (4, bit 1), root-volume free and total in decimal GB (2 + 2, bit 2; free ≤ total, total > 0), disk read and write KiB/s (4 + 4, bit 3) |

Validity bits outside the group's fields (`0x1F` for power, `0x0F` for the
others) invalidate the response, as do trailing bytes, out-of-range
percentages and non-ASCII names. The largest body, CPU with four 20-byte
names, is 92 bytes. The Mac never sends process paths, user names or
peripheral addresses.

* AI_USAGE request: empty. `OK` response: bounded layout below. `NOT_AVAILABLE` or `MALFORMED`: empty response. Zero providers is a valid response.

| Field | Size | Values |
| --- | --- | --- |
| Snapshot state | 1 | `1=discovering`, `2=ready` |
| Provider count | 1 | `0..2` |
| Generation | 4 | unsigned counter |
| Each provider: ID, plan, freshness, metric count | 4 | ID `1=Codex`, `2=Cursor`, `3=Claude`; plan `0=unknown`, `1=Plus`, `2=Business`, `3=Enterprise`, `4=Pro`, `5=Max`; freshness `1=fresh`, `2=stale`; metric count `1..2` |
| Each metric: kind, unit | 2 | kind `1=5 hour`, `2=week`, `3=credits`, `4=money`; unit `1=percent`, `2=credits`, `3=cents` |
| Each metric: used, limit, remaining | 12 | three unsigned 32-bit values |
| Each metric: remaining percent | 1 | `0..100` |
| Each metric: reset epoch, reset remaining seconds | 8 | two unsigned 32-bit values |
| Each provider, after its metrics: reset data flag | 1 | `0=unknown`, `1=known` |

A known reset section is allowed only for Codex Plus and adds `availableCount`
(1 byte), `detailedCount` (1 byte, 0–4), then each detail: title length
(1 byte, 1–24), UTF-8 title, expiry Unix epoch (4 bytes), and expiry remaining
seconds (4 bytes). Zero expiry means unknown. The detail list may be shorter
than the available count, but `detailedCount` must not exceed
`availableCount`. Titles are normalized and uppercased on the Mac, then
truncated to at most 24 UTF-8 bytes at a code-point boundary.

A response has at most two providers and two metrics per provider, and must
contain exactly the declared data; a Mac that detects more than two providers
sends the first two in the order Codex, Cursor, Claude. The Companion sends
normalized numbers and enums only; provider identifiers and credentials never
cross BLE. Invalid AI usage response content is discarded without replacing
the previous firmware snapshot. A new Companion session clears that snapshot
immediately.

* AI_AGENT_STATUS request: empty. `OK` response: a count (`0..3`) of the
  applications whose hooks Companion has installed, then one (application,
  state) pair for each, in the Cardputer row order Codex, Claude, Cursor; any
  other order, a repeated application or a length other than `1 + 2 × count`
  is invalid. Applications use the AI_USAGE provider IDs (`1=Codex`,
  `2=Cursor`, `3=Claude`). A count of `0` means no hooks are installed. States are
  `0=unknown` (no live session or no hook events), `1=working`, `2=needs you`
  (a permission or question wait, or an execution error), `3=done` and
  `4=done earlier` (the latest finish is more than ten minutes old).
  `NOT_AVAILABLE` or `MALFORMED`: empty response. The Mac answers from its
  cached hook-event store; no chat identifiers, counts, prompts or paths cross
  BLE.
* SERVICE_STATUS request: URL length (`1..200`), then an `https://` URL of
  printable ASCII with a host. The Cardputer sends it for a public Atlassian
  Statuspage `/api/v2/status.json` when it has no Wi-Fi, or when its own fetch
  failed. The Mac fetches that URL (8 s limit) and reads `status.indicator` and
  `status.description`. `OK` response: a level (`1=operational`,
  `2=maintenance`, `3=minor`, `4=major`, `5=critical`, in severity order; `0`
  is never sent), a description length (`0..48`) and the UTF-8 description,
  cut at a character boundary. `NOT_AVAILABLE`: empty; the page could not be
  fetched or had no known indicator. The answer is sent only while the
  session that asked is live; firmware waits ten seconds for it.
* Inventory records are UTF-8 JSON of at most 4096 bytes,
  `{"schema":2,"id":"<32 lowercase hex>","revision":N,"name":"…","description":"…"}`
  with a name of 1–32 code points (no control or line separator characters, no
  edge spaces) and a description of 0–900 code points whose only control
  character is a line break, with no space or line break at either end. Both
  sides write the canonical form: keys in that order, no whitespace, only `"`,
  `\` and the line break (`\n`) escaped. Every OK answer below is exactly its layout; an error answer
  (`NOT_AVAILABLE` for a missing microSD card, `NOT_FOUND`, `REJECTED`,
  `STORAGE_ERROR`, `MALFORMED`) has an empty payload, except `CONFLICT`.
* INVENTORY_LIST request: start index (2). OK response: total records (2),
  next index (2), entry count (1), then per entry: ID (16), valid flag (1),
  revision (4), name length (1, `0..128`), UTF-8 name. A stored file that is not
  a valid record has valid flag `0`, revision `0` and no name; a valid record
  has a non-zero revision and a name. Records are sorted by ID and a page holds
  as many entries as fit one message; the Mac asks again from `next` until
  `next == total`.
* INVENTORY_GET request: ID (16), byte offset (2). OK response: revision (4),
  total length (2, `1..4096`), offset (2), then 1–240 bytes of the record's
  canonical JSON. Offset 0 takes a fresh snapshot; later offsets come from it,
  and every chunk names its revision, so a Mac that sees the revision change
  starts again. A damaged record answers `REJECTED`.
* INVENTORY_PUT request: ID (16), revision the edit is based on (4, non-zero),
  total length (2, `1..4096`), offset (2), then 1–224 bytes of the edited
  record's canonical JSON, whose `revision` is that same base revision. Chunks
  arrive in order from offset 0; offset 0 starts a new upload and is answered
  `CONFLICT` at once when the base is stale. OK response: bytes received (2) and
  the committed revision (4), which is `0` until the last chunk and then the
  base plus one. CONFLICT response: the current revision (4). An out-of-order
  chunk, JSON that is not a valid record or names another ID, or a damaged
  stored record answers `REJECTED`. The Cardputer buffers one upload of at most
  4 KiB, commits it only after the complete record validates, and discards it
  when the session changes or after ten seconds without a chunk. A timed-out
  PUT may or may not have committed; the Mac reloads instead of resending.
* INVENTORY_DELETE request: ID (16), the revision the editor saw (4; `0` for a
  stored file that is not a valid record). OK response: empty; the record, with
  any temporary or backup file, is gone for good and any transfer of it ends.
  CONFLICT response: the current revision (4). A base that does not match a
  damaged record answers `REJECTED`; a missing record `NOT_FOUND`. The tag
  keeps its ID; erasing a tag is a Cardputer action, never a protocol one.

## Failure isolation

A malformed SYSTEM_METRICS, SYSTEM_DETAILS, AI_USAGE, AI_AGENT_STATUS or
SERVICE_STATUS response
for the current session is reported to the waiting app as `MALFORMED` and never ends the
session; other malformed frames do. A request that times out after the
handshake fails only for its caller; the session ends when the heartbeat
liveness window expires.

Normal logs must not contain payloads, bundle identifiers, bond identity, or
peer identity. Build IDs may be logged.
