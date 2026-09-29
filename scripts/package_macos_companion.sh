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

# Build identity "YYYY-MM-DD <commit>[+]", the same format as the firmware (see
# scripts/write_build_identity.cmake); the Companion shows it and sends it in HELLO.
BUILD_DATE="${CARDPUTER_HUB_BUILD_DATE:-$(date +%Y-%m-%d)}"
COMMIT="${CARDPUTER_HUB_COMMIT:-}"
if [ -z "$COMMIT" ]; then
    COMMIT="$(git -C "$ROOT" rev-parse --short=7 HEAD 2>/dev/null || echo unknown)"
    if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no 2>/dev/null)" ]; then
        COMMIT="$COMMIT+"
    fi
fi
BUILD_ID="$BUILD_DATE $COMMIT"
# Apple's bundle version keys accept only numbers and periods. Keep the
# human-readable identity in CardputerBuildId; use a dotted date and the
# Git commit count for bundle metadata.
BUNDLE_SHORT_VERSION="${BUILD_DATE//-/.}"
BUNDLE_VERSION="$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null || echo 1)"
PLIST="$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Add :CardputerBuildId string $BUILD_ID" "$PLIST"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $BUNDLE_SHORT_VERSION" "$PLIST"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $BUNDLE_VERSION" "$PLIST"
chmod +x "$APP/Contents/MacOS/CardputerCompanion"
test -x "$APP/Contents/MacOS/CardputerCompanion"
/usr/libexec/PlistBuddy -c 'Print :LSUIElement' "$APP/Contents/Info.plist" | grep -Fx true
/usr/libexec/PlistBuddy -c 'Print :NSBluetoothAlwaysUsageDescription' "$APP/Contents/Info.plist" | grep -q .
/usr/libexec/PlistBuddy -c 'Print :CardputerBuildId' "$APP/Contents/Info.plist" | grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2} '
/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$PLIST" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'
/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$PLIST" | grep -Eq '^[0-9]+$'
echo "Packaged Cardputer Companion $BUILD_ID"
