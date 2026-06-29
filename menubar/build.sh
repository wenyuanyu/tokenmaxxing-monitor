#!/bin/bash
#  Build QwenBridgeBar.app — a macOS menu bar controller for the BLE bridge.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_NAME="QwenBridgeBar"
APP_DIR="$SCRIPT_DIR/$APP_NAME.app"
BRIDGE_SRC_DIR="$SCRIPT_DIR/../bridge/src"
BRIDGE_BUNDLED_DIR="$APP_DIR/Contents/Resources/Bridge"
BRIDGE_FILES=(index.js lib.js)

# `bash build.sh verify` — fail if the bundled bridge has drifted from the
# in-repo source. Run before committing if you have a built .app on disk.
if [ "${1:-}" = "verify" ]; then
    if [ ! -d "$BRIDGE_BUNDLED_DIR" ]; then
        echo "verify: no bundled bridge at $BRIDGE_BUNDLED_DIR — run \`bash build.sh\` first" >&2
        exit 1
    fi
    drift=0
    for f in "${BRIDGE_FILES[@]}"; do
        if ! diff -q "$BRIDGE_SRC_DIR/$f" "$BRIDGE_BUNDLED_DIR/$f" >/dev/null 2>&1; then
            echo "verify: drift in $f" >&2
            diff "$BRIDGE_SRC_DIR/$f" "$BRIDGE_BUNDLED_DIR/$f" || true
            drift=1
        fi
    done
    if [ "$drift" -ne 0 ]; then
        echo "verify: bundle is out of date — rebuild with \`bash build.sh\`" >&2
        exit 1
    fi
    echo "✓ bundled bridge matches source"
    exit 0
fi

echo "Compiling $APP_NAME.swift …"
swiftc -o "$SCRIPT_DIR/$APP_NAME" \
    -framework Cocoa \
    -framework CoreBluetooth \
    -framework ServiceManagement \
    -framework SwiftUI \
    -target arm64-apple-macos12.0 \
    -suppress-warnings \
    "$SCRIPT_DIR/$APP_NAME.swift"

echo "Bundling $APP_DIR …"
rm -rf "$APP_DIR"
mkdir -p "$APP_DIR/Contents/MacOS" "$APP_DIR/Contents/Resources"
cp "$SCRIPT_DIR/$APP_NAME" "$APP_DIR/Contents/MacOS/"
cp "$SCRIPT_DIR/capybara_icon.png" "$APP_DIR/Contents/Resources/"
cp "$SCRIPT_DIR/capybara_icon@2x.png" "$APP_DIR/Contents/Resources/"
cp "$SCRIPT_DIR/avatar.png" "$APP_DIR/Contents/Resources/"
cp "$SCRIPT_DIR/menubar_icon.png" "$APP_DIR/Contents/Resources/"

# Generate AppIcon.icns from capybara.png if not present
if [ ! -f "$SCRIPT_DIR/AppIcon.icns" ]; then
    echo "Generating AppIcon.icns from capybara.png …"
    ICONSET="$SCRIPT_DIR/.tmp.iconset"
    rm -rf "$ICONSET" && mkdir "$ICONSET"
    sips -z 16 16   "$SCRIPT_DIR/capybara.png" --out "$ICONSET/icon_16x16.png" >/dev/null
    sips -z 32 32   "$SCRIPT_DIR/capybara.png" --out "$ICONSET/icon_16x16@2x.png" >/dev/null
    sips -z 32 32   "$SCRIPT_DIR/capybara.png" --out "$ICONSET/icon_32x32.png" >/dev/null
    sips -z 64 64   "$SCRIPT_DIR/capybara.png" --out "$ICONSET/icon_32x32@2x.png" >/dev/null
    sips -z 128 128 "$SCRIPT_DIR/capybara.png" --out "$ICONSET/icon_128x128.png" >/dev/null
    sips -z 256 256 "$SCRIPT_DIR/capybara.png" --out "$ICONSET/icon_128x128@2x.png" >/dev/null
    sips -z 256 256 "$SCRIPT_DIR/capybara.png" --out "$ICONSET/icon_256x256.png" >/dev/null
    sips -z 512 512 "$SCRIPT_DIR/capybara.png" --out "$ICONSET/icon_256x256@2x.png" >/dev/null
    sips -z 512 512 "$SCRIPT_DIR/capybara.png" --out "$ICONSET/icon_512x512.png" >/dev/null
    iconutil -c icns "$ICONSET" -o "$SCRIPT_DIR/AppIcon.icns"
    rm -rf "$ICONSET"
fi
cp "$SCRIPT_DIR/AppIcon.icns" "$APP_DIR/Contents/Resources/"

# --- Bundle Bridge (Node.js daemon) ---
BRIDGE_SRC="$SCRIPT_DIR/../bridge"
if [ -d "$BRIDGE_SRC/node_modules" ]; then
    echo "Bundling bridge into $BRIDGE_BUNDLED_DIR …"
    rm -rf "$BRIDGE_BUNDLED_DIR"
    mkdir -p "$BRIDGE_BUNDLED_DIR"
    for f in "${BRIDGE_FILES[@]}"; do
        cp "$BRIDGE_SRC_DIR/$f" "$BRIDGE_BUNDLED_DIR/"
    done
    cp "$BRIDGE_SRC/package.json" "$BRIDGE_BUNDLED_DIR/"
    cp -R "$BRIDGE_SRC/node_modules" "$BRIDGE_BUNDLED_DIR/"
    echo "  $(du -sh "$BRIDGE_BUNDLED_DIR" | cut -f1) bundled"
else
    echo "⚠ bridge/node_modules not found — bridge won't be bundled"
fi

cat > "$APP_DIR/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>QwenBridgeBar</string>
    <key>CFBundleExecutable</key>
    <string>QwenBridgeBar</string>
    <key>CFBundleIdentifier</key>
    <string>io.github.tokenmaxxing.rlcd-bridge-bar</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleVersion</key>
    <string>1.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>LSUIElement</key>
    <true/>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>NSBluetoothAlwaysUsageDescription</key>
    <string>QwenBridgeBar uses Bluetooth to push token usage data to the ESP32 e-ink display.</string>
</dict>
</plist>
PLIST

chmod +x "$APP_DIR/Contents/MacOS/$APP_NAME"
codesign --force --deep --sign - "$APP_DIR" >/dev/null

rm "$SCRIPT_DIR/$APP_NAME"
echo "✓ Built: $APP_DIR"
echo "  Open with: open $APP_DIR"

# --- DMG packaging (optional) ---
# Usage: ./build.sh dmg
if [ "${1:-}" = "dmg" ]; then
    DMG="$SCRIPT_DIR/$APP_NAME.dmg"
    echo "Creating $DMG …"
    rm -f "$DMG"

    # Build a staging folder with the app + /Applications symlink
    STAGE="$SCRIPT_DIR/.dmg-stage"
    rm -rf "$STAGE" && mkdir "$STAGE"
    cp -R "$APP_DIR" "$STAGE/"
    ln -s /Applications "$STAGE/Applications"

    TMP_DMG="$SCRIPT_DIR/${APP_NAME}-tmp.dmg"
    hdiutil create -ov -volname "$APP_NAME" -fs HFS+ -srcfolder "$STAGE" "$TMP_DMG"
    hdiutil convert -ov -format UDZO -imagekey zlib-level=9 "$TMP_DMG" -o "$DMG"
    rm -f "$TMP_DMG"
    rm -rf "$STAGE"
    echo "✓ DMG: $DMG"
fi
