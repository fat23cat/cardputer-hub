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

Click the keyboard-shaped menu-bar icon to open the Companion popover. It shows
the Cardputer connection, negotiated protocol, and last valid message. Use
**Reconnect** to restart the existing BLE attach flow without
forgetting the bond. **Start at Login** changes the actual macOS login-item
registration; it is not enabled automatically. Diagnostics shows session and
capability state, and Quit stops the Companion without changing the login setting.

The Companion offers protocol v3 with v2 and v1 fallback. With v2 it advertises
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
cached snapshot immediately. Codex Plus rolling windows, Business credits,
and Cursor Enterprise personal spend are detected from provider data. No
account type, provider, limit or host role is configured in Companion.
Absent providers are omitted; a previously working provider with a failed
refresh is marked stale. Provider tokens remain on the Mac and never enter
BLE messages or logs. Cursor usage uses a private provider adapter that may
need updating if Cursor changes its service.
