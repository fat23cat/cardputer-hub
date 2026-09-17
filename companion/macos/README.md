# Cardputer Companion

Headless macOS agent for Cardputer Hub Plan 030.

## Build

```bash
cd companion/macos
swift run CompanionCoreCheck
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
4. Allow login-item startup if macOS asks.

The Companion looks up already-connected Cardputer peripherals. It does not scan or create a second pairing. After login it starts automatically through `SMAppService`.
