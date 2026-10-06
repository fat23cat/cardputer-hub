# Cardputer Companion

Menu-bar macOS agent for Cardputer Hub. It has no Dock icon; its only window is
the optional Inventory editor.

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
It stamps the build ID `YYYY-MM-DD <commit>` (`+` after the commit for
uncommitted changes) into `CardputerBuildId`. The macOS bundle version fields
use numeric values: `CFBundleShortVersionString` is the dotted build date
`YYYY.MM.DD`, and `CFBundleVersion` is the Git revision count. The bundle
version falls back to `1` when Git metadata is unavailable.
After writing all metadata, packaging signs and strictly verifies the complete
bundle with identifier `org.cardputer.companion`. It uses an ad-hoc signature
by default; Keychain trust for that identity is specific to the binary and may
need authorization again after a rebuild. Set
`CARDPUTER_COMPANION_SIGNING_IDENTITY` to an installed code-signing certificate
identity to preserve the application's signing identity across builds.
`CARDPUTER_HUB_BUILD_DATE` and `CARDPUTER_HUB_COMMIT` override the date and
commit in the build ID, as for the firmware.

Build the Companion and the firmware from the same checkout and install both.
There is no protocol version negotiation: HELLO carries a fingerprint of the
protocol definition, and builds with different fingerprints do not connect
(see `protocol/companion/README.md`).

Open `Package.swift` in Xcode to work on the same sources.

## First launch

1. Pair Cardputer with this Mac and leave BLE HID connected.
2. Launch **Cardputer Companion.app** once.
3. Grant Bluetooth permission if macOS asks.

The Companion looks up already-connected Cardputer peripherals. It does not
scan or create a second pairing. When Start at Login is enabled, macOS launches
it through `SMAppService` at the next login.

Click the computer-shaped menu-bar icon to open the Companion menu. It shows
the Cardputer connection, the firmware build ID, and how long ago the last valid
message arrived. Use
**Reconnect** to restart the existing BLE attach flow without
forgetting the bond. The Companion also reconnects by itself when a session
has received nothing from the Cardputer for 12 seconds (the Cardputer pings
every three seconds). Messages to the Cardputer are split into chunks as large
as the negotiated Bluetooth MTU allows. **Start at Login** changes the actual macOS login-item
registration; it is not enabled automatically. The Diagnostics submenu shows
session state and, under **Builds**, the Companion and Cardputer build IDs. About opens the standard macOS About window with
the version and build, and Quit stops the Companion without changing the login
setting. The menu uses standard AppKit menu items and a system switch, so it
follows the current macOS appearance.

When the Cardputer answers HELLO with a different protocol fingerprint, the
menu names the side to update — the one with the older build date, or both
when the dates are equal — and the Companion stops reconnecting until you
choose **Reconnect** after updating. When the Cardputer does not answer HELLO
at all (firmware from before the fingerprint), the menu says the firmware may
be older and the Companion keeps retrying.

The Companion answers `SYSTEM_METRICS` foreground polling from MAC STATUS. Sampling uses
native macOS APIs for CPU, physical memory usage estimate, memory pressure,
root-volume usage, battery, primary-interface network rates, and thermal state.
The RAM estimate excludes free and file-backed cache pages; compressed and
inactive app memory remain counted as used.
Unavailable metrics remain individually unavailable; a Mac without a battery
can still report the other fields. The first CPU and network samples need a
previous counter baseline. No sampling timer runs inside the Companion.

The Companion answers `AI_USAGE` from a cached snapshot. It discovers an installed
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

Codex Plus also sends its known reset-credit count and up to four
available detail rows from the same rate-limits refresh, never more than the
available count. Titles are uppercased and shortened to a compact display label
on the Mac. A single failed refresh keeps the most recent sample fresh for up
to 90 seconds; a longer gap marks it stale. Reset details are read-only.

The Companion also reads Claude subscription usage. It uses the
`Claude Code-credentials` Keychain item that Claude Code already keeps and asks
the Claude usage service for the plan-wide 5-hour and weekly windows at most
once a minute, because that service rate-limits frequent callers. A
rate-limit response pauses requests for its `Retry-After` time or a pause that
doubles from one minute up to 30 minutes; the last sample stays shown for
about two and a half minutes, then it is marked stale. The first
read after launching Companion may show a macOS Keychain prompt; choose
**Always Allow**. A refresh waits
at most five seconds for that prompt, so other providers keep updating at
their normal rate; with no other account the Cardputer keeps showing
`CHECKING AI` until you answer. Declining hides Claude for an hour before silent
checks resume; relaunch Companion to authorize access again. Only the first
read after launch may prompt. Wake, periodic checks, and expired-token recovery
read the Keychain without UI. The token stays in memory, and the Keychain item
is checked again every five minutes, so signing out or switching accounts
normally shows up within about that time. A failed check or unavailable
authorization keeps a working token. If authorization is needed after that
token expires, the last sample becomes stale and silent checks retry every five
minutes; relaunch Companion to grant access. The Companion never refreshes or
changes that credential: while Claude Code is not running and its token has
expired, the last sample stays visible as stale. Stale samples keep counting
down to their reset times; a stale 5-hour or weekly window whose reset time has
passed is shown as 100% left with an unknown next reset. At most two providers
reach the Cardputer, in the order Codex, Cursor, Claude; Diagnostics marks any provider the connected Cardputer
does not receive as not sent.
Claude usage is a private provider adapter that may need updating if the
service changes.

The Companion answers `AI_AGENT_STATUS` for AI's STATUS page and its USAGE
attention indicator from desktop agent lifecycle hooks. **AI Agent Hooks** in
the menu installs or removes the hooks
of Codex (`~/.codex/hooks.json`, or `$CODEX_HOME`), Claude Code
(`~/.claude/settings.json`, which the desktop Code tab shares) and Cursor
Agent Chat (`~/.cursor/hooks.json`). It changes only entries that run
`CardputerAgentHook`, keeps `<file>.cardputer-backup`, writes atomically
through symlinks, and refuses a file that is not plain JSON. Restart the app
afterwards; Codex asks you to review and trust new hooks. Installing copies the
helper to `~/Library/Application Support/Cardputer Companion/`, and each launch
refreshes that copy, so moving or updating the app keeps hooks working.

The helper forwards one event over a private socket in that folder (mode 0600)
and exits; when Companion is not running it exits at once. It sends only the
application, event name, session and turn IDs, status, notification type, tool
name and the agent's process ID, never prompts, answers, paths, commands or
e-mail, and it returns no permission decision. Companion keeps per-session
states in memory: a question, an error or an approval request still
unanswered after 15 seconds is NEEDS YOU (Codex auto-review and other automatic
approvals answer sooner, so they never show), an active run
WORKING, a finished or user-stopped run DONE, reported as done earlier once
the latest finish is ten minutes old. A session ends with its end
event, when its agent process exits, or after 15 minutes working or 2 hours
waiting without events. Cursor's `stop` counts after 0.7 seconds without new
activity. Diagnostics shows each application's hook state and last event. The
Cardputer lists only applications whose hooks are installed; Companion rereads
that set every few seconds and at once after an install or removal.
Claude Code sends no hook when you interrupt a run or deny a permission, and
Cursor has no observe-only approval event, so those cases rely on expiry or
show WORKING.

MAC STATUS also receives the power source and the minutes to full or
to empty, and the Companion answers `SYSTEM_DETAILS` for the Cardputer's
detail pages. It reads only the group the Cardputer asks for. A request is
answered at once from that group's cached sample while a background queue
reads the group again, so the first request after a page opens is answered
`NOT_AVAILABLE` and a sample older than five seconds is never sent:

* CPU: per-core load split into performance and efficiency clusters (from the
  IORegistry cluster type of each CPU), GPU utilisation from the IOAccelerator
  statistics, and the four apps using the most CPU. Process CPU time comes from `proc_pid_rusage`; processes of other users
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
without a request the first answer omits them.

**Inventory…** in the menu opens the Inventory window, which lists the NFC
inventory records on the connected Cardputer's microSD card, edits a
container's name and free-text description, and deletes records after a
confirmation. Requests are the Mac-to-Cardputer `INVENTORY_LIST`,
`INVENTORY_GET`, `INVENTORY_PUT` and `INVENTORY_DELETE` operations of the live
session, one at a time with a four-second timeout; records move as canonical
JSON in bounded chunks and every save is checked against the revision the edit
started from. The Companion keeps no copy of the records after the session
ends, never resends an edit after a disconnect, and enforces the same limits as
the firmware (names 32 and descriptions 900 characters, 4 KiB). See the
[device guide](../../docs/manuals/device-guide.md#inventory-on-the-mac).
