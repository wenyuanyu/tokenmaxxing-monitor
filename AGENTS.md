# Agent Notes

This file is for Codex, Claude, and other coding agents working on this repo.
Read it before changing code.

## Project Intent

TokenMaxxing RLCD is a BLE-driven token activity display for AI coding agents.
The host bridge owns data-source aggregation. The ESP32 firmware owns display,
sensor, battery, BLE receive, and KEY-button behavior.

Avoid committing personal names, local paths, generated chat exports, logs,
status JSON, or build output.

## High-Level Map

```text
bridge/src/index.js
  Data-source readers, aggregation, BLE writes, status JSON.

firmware/components/ble_app/ble_app.c
  BLE GATT service and payload parser.

firmware/components/usage_client/usage_client.h
  Shared report struct used by BLE and UI.

firmware/components/ui_app/ui_app.cpp
  LVGL layout and label updates.

firmware/main/main.cpp
  App entry and RLCD flush callback. Activity dithering happens here.

firmware/components/user_app/user_app.cpp
  Sensor task, battery task, KEY-button page toggle.

menubar/QwenBridgeBar.swift
  Optional macOS menu bar app that starts the bridge and reads status JSON.

tools/lvgl_preview/
  Host-side LVGL framebuffer render for quick UI checks.
```

## Current Bridge Semantics

Run with:

```bash
TOKEN_MONITOR_DATASOURCES=qwen,codex,claude QWEN_BLE_DEVICE_NAME=QwenToken npm start
```

Supported data sources:

- `qwen`
- `codex`
- `claude` (reads `$CLAUDE_HOME/projects/*/<sessionId>.jsonl`; skips `<synthetic>` model rows)

Aggregation rules:

- configured data sources are summed together
- activity window is 26 x 7 days
- level buckets:
  - `0`: `0`
  - `1`: `(0, 10M)`
  - `2`: `[10M, 50M)`
  - `3`: `[50M, 100M)`
  - `4`: `[100M, +inf)`
- lifetime, peak, streak, and longest task are computed over
  `TOKEN_MONITOR_HISTORY_DAYS` days, default `3650`

Status file:

```text
/tmp/qwen-token-status.json
```

## BLE Protocol Rules

Current payload version is `3`.

Do not reorder existing fields. If adding data, append optional tail fields and
keep firmware parsing backward-compatible.

The activity field is packed as 26 columns x 7 rows:

- one column = 4 base36 chars
- cells are base-5 digits
- row order is Sunday to Saturday

## Firmware Rules

- Keep `RLCD_GREETING_NAME` default generic or empty.
- Pass personal greeting names through build flags only:

  ```bash
  idf.py -DRLCD_GREETING_NAME="YourName" build flash
  ```

- Do not commit generated `font_zh18.c`.
- Do not move activity dithering into LVGL child objects unless there is a
  strong reason. The current 1-bit pattern conversion in `main.cpp` is cheaper
  and more predictable.
- Use the KEY button behavior for page switching unless the UX requirement
  changes.

## Verification Commands

Bridge syntax:

```bash
cd bridge
node --check src/index.js
```

Firmware build:

```bash
cd firmware
source ~/esp/esp-idf/export.sh
idf.py -DRLCD_GREETING_NAME="" build
```

Flash:

```bash
idf.py -DRLCD_GREETING_NAME="" -p /dev/cu.usbmodem* flash
```

LVGL preview:

```bash
cd tools/lvgl_preview
bash render.sh
```

macOS menu bar app:

```bash
cd menubar
bash build.sh dmg
```

## Common Pitfalls

- macOS BLE may fail from tmux/screen/sandboxed sessions. Prefer Terminal.app or
  the bundled menu bar app.
- Flashing resets BLE; restart or reconnect the bridge afterward.
- The RLCD is 1-bit. Do not use RGB gray values expecting true grayscale.
- The preview PPM shows LVGL color output; hardware dithering in `main.cpp`
  happens at flush time.
- The generated `.app` and DMG are build artifacts. Keep source changes in
  Swift/build scripts, not only inside an already-built app bundle.
