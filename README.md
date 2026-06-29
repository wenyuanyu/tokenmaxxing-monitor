# TokenMaxxing Monitor

English | [中文](README.zh-CN.md)

TokenMaxxing Monitor is a local-first token activity meter for AI coding agents. A
Node.js bridge aggregates token usage from configured data sources, pushes a
compact BLE payload to a Waveshare ESP32-S3-RLCD-4.2 board, and mirrors the same
state to a macOS menu bar app.

The firmware is intentionally small and offline once flashed. The host bridge is
where data-source support lives, so adding Codex, Qwen Code, Claude, or another
agent usually means extending one JavaScript reader instead of touching the
embedded UI.

## What It Shows

- Today token total and progress toward a 100M daily goal
- Top 3 models used today
- Sessions, active time, input/output tokens, cache rate, and last-7-days total
- Temperature and humidity from the board's SHTC3 sensor
- Battery percentage plus USB/external-power indicator
- A second activity page toggled by the board KEY button:
  - last 6 months activity grid
  - lifetime tokens
  - peak daily tokens
  - current streak
  - longest task

Activity cells use five monochrome shades because the RLCD is effectively 1-bit:

| Level | Meaning | Pattern |
| --- | --- | --- |
| 0 | `0` tokens | blank |
| 1 | `(0, 10M)` | `░`, about 1/3 black |
| 2 | `[10M, 50M)` | `▒`, 1/2 black |
| 3 | `[50M, 100M)` | `▓`, about 2/3 black |
| 4 | `[100M, +inf)` | `█`, all black |

## Architecture

```text
Qwen Code / Codex / Claude Code logs
        |
        v
bridge/src/index.js
  - scans configured data sources
  - aggregates daily + historical metrics
  - writes /tmp/qwen-token-status.json
  - pushes BLE v3 payload every second
        |
        v
ESP32-S3 BLE GATT characteristic
        |
        v
firmware
  - NimBLE receiver
  - LVGL UI
  - SHTC3 / battery / KEY button tasks
  - 1-bit RLCD flush with dithered activity shades
        |
        v
400 x 300 reflective LCD
```

## Repository Layout

```text
.
├── bridge/                 # Node.js BLE bridge and data-source readers
│   ├── package.json
│   └── src/index.js
├── firmware/               # ESP-IDF project for ESP32-S3-RLCD-4.2
│   ├── main/
│   └── components/
├── menubar/                # Optional macOS SwiftUI menu bar app
│   ├── QwenBridgeBar.swift
│   └── build.sh
├── tools/lvgl_preview/     # Local LVGL framebuffer renderer
├── AGENTS.md               # Notes for Codex/Claude/other coding agents
└── README.zh-CN.md
```

## Hardware

Target board:

- [Waveshare ESP32-S3-RLCD-4.2](https://docs.waveshare.com/ESP32-S3-RLCD-4.2)
- ESP32-S3 with BLE
- 400 x 300 reflective LCD
- Onboard SHTC3 temperature/humidity sensor
- KEY button on GPIO 18
- Battery ADC support in firmware

## Quick Start

### 1. Build And Flash Firmware

Prerequisites:

- ESP-IDF v5.x
- Node.js available on PATH, used by the font generation step

```bash
cd firmware
cp main/secrets.h.example main/secrets.h
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py -DRLCD_GREETING_NAME="YourName" -DRLCD_BLE_DEVICE_NAME="QwenToken" -p /dev/cu.usbmodem* build flash
```

Notes:

- `RLCD_GREETING_NAME` is optional. Leave it empty for a generic greeting.
- CJK greeting glyphs are merged into the generated LVGL font during build.
- Generated font files stay under `firmware/build/` and are not committed.

### 2. Run The Bridge

Prerequisites:

- Node.js 18+
- macOS for BLE bridge support via `@abandonware/noble`/CoreBluetooth

```bash
cd bridge
npm install
TOKEN_MONITOR_DATASOURCES=qwen,codex,claude QWEN_BLE_DEVICE_NAME=QwenToken npm start
```

Expected logs:

```text
[ble] adapter state: poweredOn
[ble] scanning for QwenToken
[ble] connecting QwenToken
[ble] ready
[ble] wrote 3|...
```

The bridge also writes:

```text
/tmp/qwen-token-status.json
```

### 3. Optional macOS Menu Bar App

```bash
cd menubar
bash build.sh dmg
open QwenBridgeBar.app
```

The app bundles the bridge script and `node_modules`, starts the bridge as a
child process, reads `/tmp/qwen-token-status.json`, and can register itself as a
Login Item on macOS 13+.

The generated DMG is:

```text
menubar/QwenBridgeBar.dmg
```

## Bridge Data Sources

Configure data sources with:

```bash
TOKEN_MONITOR_DATASOURCES=qwen,codex,claude
```

Supported readers:

| Source | What it scans |
| --- | --- |
| `qwen` | monthly Qwen usage JSONL files under the Qwen runtime usage directory |
| `codex` | Codex rollout JSONL files under `$CODEX_HOME/sessions` |
| `claude` | Claude Code session JSONL files under `$CLAUDE_HOME/projects/*/` |

Important environment variables:

| Variable | Default | Description |
| --- | --- | --- |
| `TOKEN_MONITOR_DATASOURCES` | `qwen` | Comma-separated sources to aggregate |
| `TOKEN_MONITOR_HISTORY_DAYS` | `3650` | History window used for lifetime, peak, streak, and longest task |
| `QWEN_BLE_DEVICE_NAME` | `QwenToken,Qwen Usage` | Comma-separated BLE names to connect to |
| `QWEN_BLE_PUSH_MS` | `5000` | Bridge aggregation tick; BLE writes are skipped until data changes or heartbeat is due |
| `QWEN_BLE_HEARTBEAT_MS` | `60000` | Maximum interval between unchanged BLE payload writes |
| `QWEN_BLE_CONNECT_TIMEOUT_RESTARTS` | `3` | Exit after this many consecutive BLE connect timeouts so LaunchAgent/launchd can restart CoreBluetooth state; set `0` to disable |
| `QWEN_BLE_SCAN_DAYS` | `7` | Recent window for week/current metrics |
| `QWEN_RUNTIME_DIR` | derived from `~/.qwen/.env` | Qwen runtime directory |
| `CODEX_HOME` | `~/.codex` | Codex home directory |
| `CODEX_SESSIONS_DIR` | `$CODEX_HOME/sessions` | Codex session rollout directory |
| `CODEX_STATE_DB` | `$CODEX_HOME/state_5.sqlite` | Optional Codex thread model-name lookup |
| `CLAUDE_HOME` | `~/.claude` | Claude Code home directory |
| `CLAUDE_PROJECTS_DIR` | `$CLAUDE_HOME/projects` | Claude Code per-project session directory |

## BLE Protocol

Service UUID:

```text
00112233-4455-6677-8899-aabbccddeeff
```

Writable characteristic UUID:

```text
00112233-4455-6677-8899-aabbccddee01
```

Payload format is pipe-delimited text. Current format is version `3`:

```text
3|todayTotal|sessionsToday|todayCached|cacheRate|activeMinutes|updatedAtUnix|tzOffsetMinutes|
model1|model1Pct|model2|model2Pct|model3|model3Pct|
errorsToday|ageSec|todayOutput|weekTotal|todayInput|
activity|lifetimeTotal|peakDailyTotal|streakDays|longestTaskMinutes
```

`updatedAtUnix` is Unix time in seconds. `tzOffsetMinutes` is the local
offset from UTC in minutes, for example `480` for UTC+8. Firmware uses those
two fields to derive the local header clock and the activity page's natural
week/month labels.

The `activity` field is 104 base36 characters:

- 26 columns
- 7 rows per column
- one 4-character packed base36 chunk per column
- row order is Sunday through Saturday
- each cell is a base-5 level from `0` to `4`

The firmware keeps v1/v2 parsing for backward compatibility, but new bridges
should emit v3.

## Firmware UI Notes

- Main page: token dashboard plus sensor and battery status.
- Activity page: press the board KEY button to toggle.
- Battery status is drawn as a framed percentage. When charging is reported, a
  tiny 1 x 1-pixel-art lightning glyph is placed inside the right side of the
  battery frame; when not charging, the percentage recenters to avoid an empty
  gap.
- The RLCD output is 1-bit. Activity "shades" are not true grayscale; they are
  generated in `firmware/main/main.cpp` during LVGL flush using 10 x 10 cell
  dithering.
- UI layout lives in `firmware/components/ui_app/ui_app.cpp`.
- BLE parsing lives in `firmware/components/ble_app/ble_app.c`.
- Shared report schema lives in `firmware/components/usage_client/usage_client.h`.

## Local LVGL Preview

The preview renders the LVGL UI to framebuffer files without flashing hardware.
Run a firmware build once first so `font_zh18.c` exists.

```bash
cd tools/lvgl_preview
bash render.sh
```

Outputs:

```text
outputs/lvgl-preview/dashboard.ppm
outputs/lvgl-preview/activity.ppm
```

Convert to PNG on macOS:

```bash
sips -s format png outputs/lvgl-preview/activity.ppm --out outputs/lvgl-preview/activity.png
```

## Adding A New Data Source

Most new agent integrations only require bridge changes:

1. Add a reader function in `bridge/src/index.js`.
2. Return an `emptyTotals(sourceName)` shaped object.
3. Fill:
   - today totals
   - model totals
   - session events for active time
   - task events for longest task
   - daily totals for activity/lifetime/peak/streak
   - latest call metadata
4. Register it in the `readers` map inside `buildReport()`.
5. Run the bridge with `TOKEN_MONITOR_DATASOURCES=yourSource`.

If the BLE payload fields do not change, firmware does not need to be touched.

## Troubleshooting

### The bridge scans forever

- Make sure the flashed BLE name matches `QWEN_BLE_DEVICE_NAME`.
- Keep the board powered and within BLE range.
- BLE devices may not appear in macOS Bluetooth Settings; use bridge logs.

### The bridge exits immediately on macOS

Run it from a normal Terminal.app or iTerm2 window. CoreBluetooth can fail in
detached shells, terminal multiplexers, or sandboxed launch contexts.

### The screen shows no data after flashing

Start the bridge again. Flashing resets BLE, so the host bridge may need to
reconnect.

### Activity shades look like binary black/white

That is expected. The panel is driven as 1-bit. The five visual levels are
dithered patterns, not true grayscale.

## Privacy

The bridge reads local usage logs and sends aggregate metrics over BLE. It does
not upload data to any remote service. Avoid committing local exports, generated
build output, status JSON files, logs, or personal greeting names.
