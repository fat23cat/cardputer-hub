# Cardputer Companion

Menu-bar macOS agent for Cardputer Hub. It has no Dock icon or main window.

## Build

```bash
cd companion/macos
swift run CompanionCoreCheck
swift run CompanionProvidersCheck
swift build -c release --product CardputerCompanion
```

Or from the repository root:

```bash
make companion-check
```

`scripts/package_macos_companion.sh` creates `Cardputer Companion.app` with `LSUIElement` set so it does not appear in the Dock.

Open `Package.swift` in Xcode to work on the same sources.

## First launch

1. Pair Cardputer with this Mac and leave BLE HID connected.
2. Launch **Cardputer Companion.app** once.
3. Grant Bluetooth permission if macOS asks.

The Companion looks up already-connected Cardputer peripherals. It does not
scan or create a second pairing. When Start at Login is enabled, macOS launches
it through `SMAppService` at the next login.

Click the computer-shaped menu-bar icon to open the Companion menu. It shows
the Cardputer connection, negotiated protocol, and last valid message. Use
**Reconnect** to restart the existing BLE attach flow without
forgetting the bond. The Companion also reconnects by itself when a session
has received nothing from the Cardputer for 12 seconds (the Cardputer pings
every three seconds). Messages to the Cardputer are split into chunks as large
as the negotiated Bluetooth MTU allows. **Start at Login** changes the actual macOS login-item
registration; it is not enabled automatically. The Diagnostics submenu shows
session and capability state, About opens the standard macOS About window with
the version and build, and Quit stops the Companion without changing the login
setting. The menu uses standard AppKit menu items and a system switch, so it
follows the current macOS appearance.

The Companion offers protocol v6 with v5, v4 and v3 fallback. With v2 and later it advertises
`SYSTEM_METRICS` and answers foreground polling from MAC STATUS. Sampling uses
native macOS APIs for CPU, physical memory usage estimate, memory pressure,
root-volume usage, battery, primary-interface network rates, and thermal state.
The RAM estimate excludes free and file-backed cache pages; compressed and
inactive app memory remain counted as used.
Unavailable metrics remain individually unavailable; a Mac without a battery
can still report the other fields. The first CPU and network samples need a
previous counter baseline. No sampling timer runs inside the Companion.

With v3, the Companion also advertises `AI_USAGE`. It discovers an installed
Codex executable, including through the user's login shell when the GUI PATH
does not contain it, and reads rate limits from Codex app-server. It checks the
existing Cursor Agent Keychain session for personal usage and derives the
request cookie in memory from the token's user ID. It refreshes these
sources in the background about once a minute; Cardputer requests read the
cached snapshot immediately. Wake requests coalesce with an active refresh;
provider failures retry with a capped 30-second backoff, and Codex process exits
between refreshes trigger recovery. Failed login-shell discovery is retried on
the next discovery cycle. Codex Plus rolling windows, Business credits,
and Cursor Enterprise personal spend are detected from provider data. No
account type, provider, limit or host role is configured in Companion.
Absent providers are omitted; a previously working provider with a failed
refresh is marked stale. Provider tokens remain on the Mac and never enter
BLE messages or logs. Cursor usage uses a private provider adapter that may
need updating if Cursor changes its service.

With v4, Codex Plus also sends its known reset-credit count and up to four
available detail rows from the same rate-limits refresh, never more than the
available count. Titles are uppercased and shortened to a compact display label
on the Mac. A single failed refresh keeps the most recent sample fresh for up
to 90 seconds; a longer gap marks it stale. The v3 AI_USAGE
payload remains unchanged for older firmware. Reset details are read-only.

With v5, the Companion also reads Claude subscription usage. It uses the
`Claude Code-credentials` Keychain item that Claude Code already keeps and asks
the Claude usage service for the plan-wide 5-hour and weekly windows at most
once a minute, because that service rate-limits frequent callers. A
rate-limit response pauses requests for its `Retry-After` time or a pause that
doubles from one minute up to 30 minutes; the last sample stays shown for
about two and a half minutes, then it is marked stale. The first
read shows a macOS Keychain prompt; choose **Always Allow**. A refresh waits
at most five seconds for that prompt, so other providers keep updating at
their normal rate; with no other account the Cardputer keeps showing
`CHECKING AI` until you answer. Declining hides Claude for an hour before Companion asks
again. The token stays in memory, and the Keychain item is checked again
every five minutes, so signing out or switching accounts shows up within about
that time; a failed check keeps the working token. The Companion never refreshes or
changes that credential: while Claude Code is not running and its token has
expired, the last sample stays visible as stale. Stale samples keep counting
down to their reset times; a stale 5-hour or weekly window whose reset time has
passed is shown as 100% left with an unknown next reset. Claude is omitted for
v3/v4 firmware, and at most two providers reach the Cardputer, in the order
Codex, Cursor, Claude; Diagnostics marks any provider the connected Cardputer
does not receive as not sent.
Claude usage is a private provider adapter that may need updating if the
service changes.

With v6, MAC STATUS also receives the power source and the minutes to full or
to empty, and the Companion advertises `SYSTEM_DETAILS` for the Cardputer's
detail pages. It reads only the group the Cardputer asks for. A request is
answered at once from that group's cached sample while a background queue
reads the group again, so the first request after a page opens is answered
`NOT_AVAILABLE` and a sample older than five seconds is never sent:

* CPU: per-core load split into performance and efficiency clusters (from the
  IORegistry cluster type of each CPU), GPU utilisation from the IOAccelerator
  statistics, the one-minute load average, and the four apps using the most
  CPU. Process CPU time comes from `proc_pid_rusage`; processes of other users
  are not readable without root and are skipped. Helper processes inside an
  `.app` bundle count toward the outermost app. Each app's share is averaged
  over about ten seconds; apps under 0.5% leave the list, and listed apps swap
  places only when one leads by more than 1.5 percentage points.
* Power: system power draw from the battery telemetry, adapter wattage,
  battery health (nominal over design capacity), cycle count, and the
  lowest-charged Apple Bluetooth mouse, keyboard or trackpad.
* Network: round-trip time to `1.1.1.1` and to the router with an unprivileged
  ICMP echo; a router that drops ICMP is timed with a TCP connection to port 80
  or 443. Probes run on their own queue at most every five seconds and stop ten
  seconds after the last NETWORK request. Wi-Fi signal and link rate come from
  CoreWLAN without reading the network name, so no Location permission is
  needed.
* Memory and disk: app, wired and compressed memory, swap in use, free and
  total space on `/` in decimal GB, and disk read/write rates from the block
  storage counters.

App and peripheral names are transliterated to plain ASCII before they are
sent. Process CPU and disk rates need a previous sample; after ten seconds
without a request the first answer omits them. Diagnostics shows whether
`SYSTEM_DETAILS` is available.
