# Companion Protocol v1–v6

Firmware and the macOS Companion share this wire contract and the binary
fixtures in `fixtures/`. They do not share implementation code.

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
| 0 | 1 | version (`1`–`6`) |
| 1 | 1 | kind |
| 2 | 2 | session generation |
| 4 | 1 | request ID (`0` if unused) |
| 5 | 1 | operation |
| 6 | 1 | status (`0` unless RESPONSE) |
| 7 | 1 | payload length |
| 8 | n | payload |

Header is 8 bytes. Payload length is at most 248.

Decode and encode reject unknown kind/operation/status, illegal
`(kind, operation)` pairs, `status != OK` on non-RESPONSE messages, HELLO
with a non-zero session or request ID, REQUEST/RESPONSE with session or
request ID `0`, EVENT with a non-zero request ID, PING payloads other than
4 bytes, non-empty CAPABILITIES/APP_ACTIVE requests, and APP_ACTIVATE
requests without a valid bundle identifier.
SYSTEM_METRICS is valid in v2 and later; its request payload is empty and an OK
response has exactly 24 bytes (schema 1, v2–v5) or 27 bytes (schema 2, v6).
SYSTEM_DETAILS is valid only in v6; its request carries one group byte.
Normal session messages must match the selected protocol version.

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
| 2 | CAPABILITIES | REQUEST, RESPONSE |
| 3 | APP_ACTIVE | REQUEST, RESPONSE |
| 4 | APP_ACTIVATE | REQUEST, RESPONSE |
| 5 | APP_ACTIVE_CHANGED | EVENT |
| 6 | SYSTEM_METRICS | REQUEST, RESPONSE (v2+) |
| 7 | AI_USAGE | REQUEST, RESPONSE (v3+) |
| 8 | SYSTEM_DETAILS | REQUEST, RESPONSE (v6+) |

### Status

| Value | Name |
| --- | --- |
| 0 | OK |
| 1 | NOT_AVAILABLE |
| 2 | NOT_FOUND |
| 3 | UNSUPPORTED |
| 4 | MALFORMED |

### Payloads

* HELLO: `count` then up to 4 supported protocol versions.
* HELLO_ACK: selected protocol version (`1`–`6`). Session generation is in the envelope.
* PING: 4-byte token, echoed by the response.
* CAPABILITIES request: empty. Response: `count` then capability IDs `1=APP_ACTIVE`, `2=APP_ACTIVATE`, `3=APP_ACTIVE_EVENTS`. Version 2 adds `4=SYSTEM_METRICS`; version 3 adds `5=AI_USAGE`; version 6 adds `6=SYSTEM_DETAILS`. Older versions must not advertise later capabilities.
* APP_ACTIVE / APP_ACTIVATE / APP_ACTIVE_CHANGED: `length` then UTF-8 bundle identifier, 1–128 bytes. APP_ACTIVE may return `NOT_AVAILABLE` with an empty payload. APP_ACTIVATE may return `NOT_FOUND`. APP_ACTIVE_CHANGED with an empty payload and status `OK` means there is no active bundle.
* SYSTEM_METRICS request: empty. `OK` response: the fixed payload below. `NOT_AVAILABLE` or `MALFORMED`: empty response. Individual unavailable fields are represented by clear validity bits, not a failed response.

| Offset | Size | SYSTEM_METRICS response field |
| --- | --- | --- |
| 0 | 1 | schema version `1` |
| 1 | 2 | validity bits: CPU 0, RAM 1, pressure 2, SSD 3, battery 4, network 5, thermal 6 |
| 3 | 1 | CPU percentage |
| 4 | 4 | physical RAM used estimate, MiB |
| 8 | 4 | physical RAM total, MiB |
| 12 | 1 | pressure: normal 1, warning 2, critical 3 |
| 13 | 1 | root-volume disk used percentage |
| 14 | 1 | battery percentage |
| 15 | 1 | thermal: normal 1, fair 2, serious 3, critical 4 |
| 16 | 4 | download KiB/s |
| 20 | 4 | upload KiB/s |

Values with clear validity bits are ignored. Percentages must be 0–100 when
valid. Unknown pressure or thermal state uses a clear validity bit. The first
CPU and network samples may be unavailable while their rate baselines form.

Protocol v6 uses SYSTEM_METRICS schema `2`: byte 0 is `2`, bytes 1–23 are
unchanged, and three bytes follow. v2–v5 sessions keep schema `1` exactly; a
decoder rejects a schema that does not match the session version.

| Offset | Size | Schema 2 addition |
| --- | --- | --- |
| 24 | 1 | power source, validity bit 7: battery 1, AC charging 2, AC not charging 3 |
| 25 | 2 | battery minutes, validity bit 8: to full while charging, to empty on battery |

The Mac clears bit 8 while macOS is still estimating and on AC while not
charging. A Mac without an internal battery clears bits 4, 7 and 8.

* SYSTEM_DETAILS request (v6): one byte, group `1=CPU`, `2=power`, `3=network`, `4=memory/disk`. Any other value makes the request invalid; it is not answered and times out on the Cardputer. `OK` response: the group layout below. `NOT_AVAILABLE` or `MALFORMED`: empty response. The Cardputer requests only the group of its visible page.

| Offset | Size | SYSTEM_DETAILS response header |
| --- | --- | --- |
| 0 | 1 | details schema `1` |
| 1 | 1 | group, echoed from the request |
| 2 | 2 | validity bits for this group |

Every field is encoded in order even when its validity bit is clear; the Mac
then writes zero. Names are printable ASCII (`0x20`–`0x7E`); the Mac
transliterates to Latin, strips diacritics, drops other characters and
truncates. An empty name becomes `APP`.

| Group | Body after the header |
| --- | --- |
| 1 CPU | P-cluster % (1, bit 0), E-cluster % (1, bit 1), GPU % (1, bit 2), 1-minute load ×100 (2, bit 3), app count (1, `0..4`, non-zero only with bit 4); then per app: CPU % of the whole machine (1, `0..100`), name length (1, `1..20`), name |
| 2 power | system draw in deciwatts (2, bit 0), adapter watts (1, bit 1), battery health % (1, bit 2), cycle count (2, bit 3), lowest Apple peripheral battery % (1, bit 4), peripheral name length (1, `1..16` with bit 4, otherwise `0`), name |
| 3 network | internet round trip ms (2, bit 0), router round trip ms (2, bit 1), Wi-Fi RSSI dBm signed (1, bit 2), Wi-Fi link rate Mbps (2, bit 3), reserved (1, bit 4; the Mac leaves it clear) |
| 4 memory/disk | app, wired and compressed memory MiB (3 × 4, bit 0), swap used MiB (4, bit 1), root-volume free and total in decimal GB (2 + 2, bit 2; free ≤ total, total > 0), disk read and write KiB/s (4 + 4, bit 3) |

Validity bits outside the group's fields (`0x1F`, or `0x0F` for memory/disk)
invalidate the response, as do trailing bytes, out-of-range percentages and
non-ASCII names. The largest body, CPU with four 20-byte names, is 98 bytes.
The Mac never sends process paths, user names or peripheral addresses.

* AI_USAGE request: empty. `OK` response: bounded schema below. `NOT_AVAILABLE` or `MALFORMED`: empty response. Zero providers is a valid response and does not remove the AI_USAGE capability.

| Field | Size | Values |
| --- | --- | --- |
| Schema version | 1 | `1` |
| Snapshot state | 1 | `1=discovering`, `2=ready` |
| Provider count | 1 | `0..2` |
| Generation | 4 | unsigned counter |
| Each provider: ID, plan, freshness, metric count | 4 | ID `1=Codex`, `2=Cursor`; plan `0=unknown`, `1=Plus`, `2=Business`, `3=Enterprise`; freshness `1=fresh`, `2=stale`; metric count `1..2` |
| Each metric: kind, unit | 2 | kind `1=5 hour`, `2=week`, `3=credits`, `4=money`; unit `1=percent`, `2=credits`, `3=cents` |
| Each metric: used, limit, remaining | 12 | three unsigned 32-bit values |
| Each metric: remaining percent | 1 | `0..100` |
| Each metric: reset epoch, reset remaining seconds | 8 | two unsigned 32-bit values |

All multi-byte fields are little-endian. A response has at most two providers
and two metrics per provider, and must contain exactly the declared data. The
Companion sends normalized numbers and enums only; provider credentials never
cross BLE. Invalid AI usage response content is discarded without replacing
the previous firmware snapshot. A new Companion session clears that snapshot
immediately.

Protocol v4 uses AI_USAGE schema `2`. The fields above remain byte-for-byte
identical except for the schema byte. Each provider is followed by one reset
data flag (`0=unknown`, `1=known`). A known section is allowed only for Codex
Plus and adds `availableCount` (1 byte), `detailedCount` (1 byte, 0–4), then
each detail: title length (1 byte, 1–24), UTF-8 title, expiry Unix epoch (4
bytes), and expiry remaining seconds (4 bytes). Zero expiry means unknown.
The detail list may be shorter than the available count, but `detailedCount`
must not exceed `availableCount`. Titles are normalized and uppercased on the
Mac, then truncated to at most 24 UTF-8 bytes at a code-point boundary.
Invalid or truncated
sections invalidate the response. Version 3 always uses schema `1` and does
not contain reset data. No identifiers or credentials cross BLE.

Protocol v5 uses AI_USAGE schema `3`. It is byte-for-byte schema `2` except
for the schema byte and two added enum ranges: provider `3=Claude` and plans
`4=Pro`, `5=Max`. Schemas `1` and `2` reject provider `3` and plans `4`–`5`,
so the Mac omits Claude from v3/v4 responses. Reset data remains Codex Plus
only. The provider and metric bounds are unchanged; a Mac that detects more
than two providers sends the first two in the order Codex, Cursor, Claude.

Protocol v6 keeps AI_USAGE schema `3` unchanged.

The Mac sends a v1-framed HELLO offering `[6, 5, 4, 3]`. HELLO carries at most
four versions, so v1 and v2 are no longer offered; a v5 Cardputer still
selects v5, and a Cardputer that supports only v1 or v2 shares no version.
Cardputer selects the highest common version and replies with a v1-framed
HELLO_ACK. Cardputer
replies HELLO_ACK with a new session generation,
then requests capabilities and the current active application. Cardputer sends
PING heartbeats. The Mac sends APP_ACTIVE_CHANGED events.

Normal logs must not contain payloads, bundle identifiers, bond identity, or
peer identity.
