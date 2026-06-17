#!/bin/bash
#  Build QwenBridgeBar.app — a macOS menu bar controller for the BLE bridge.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_NAME="QwenBridgeBar"
APP_DIR="$SCRIPT_DIR/$APP_NAME.app"

echo "Compiling $APP_NAME.swift …"
swiftc -o "$SCRIPT_DIR/$APP_NAME" \
    -framework Cocoa \
    -suppress-warnings \
    "$SCRIPT_DIR/$APP_NAME.swift"

echo "Bundling $APP_DIR …"
rm -rf "$APP_DIR"
mkdir -p "$APP_DIR/Contents/MacOS" "$APP_DIR/Contents/Resources"
cp "$SCRIPT_DIR/$APP_NAME" "$APP_DIR/Contents/MacOS/"
cp "$SCRIPT_DIR/capybara_icon.png" "$APP_DIR/Contents/Resources/"
cat > "$APP_DIR/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>QwenBridgeBar</string>
    <key>CFBundleIdentifier</key>
    <string>com.pomelo.qwen-bridge-bar</string>
    <key>CFBundleVersion</key>
    <string>1.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>LSUIElement</key>
    <true/>
</dict>
</plist>
PLIST

rm "$SCRIPT_DIR/$APP_NAME"
echo "✓ Built: $APP_DIR"
echo "  Open with: open $APP_DIR"
