#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MACOS="$ROOT/companion/macos"
BUILD="$MACOS/.build/release/CardputerCompanion"
APP="$MACOS/Cardputer Companion.app"

cd "$MACOS"
swift build -c release --product CardputerCompanion
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
cp "$BUILD" "$APP/Contents/MacOS/CardputerCompanion"
cp "$MACOS/Sources/App/Info.plist" "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"
chmod +x "$APP/Contents/MacOS/CardputerCompanion"
test -x "$APP/Contents/MacOS/CardputerCompanion"
/usr/libexec/PlistBuddy -c 'Print :LSUIElement' "$APP/Contents/Info.plist" | grep -Fx true
/usr/libexec/PlistBuddy -c 'Print :NSBluetoothAlwaysUsageDescription' "$APP/Contents/Info.plist" | grep -q .
