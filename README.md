# Token Monitor BLE

English | [中文](README.zh-CN.md)

> **Vibe Coding Project** — This project was built entirely through AI-driven conversational development (vibe coding). The original prompt:
>
> *"Referencing https://github.com/CEJXXX/token-monitor-RLCD, build a Qwen Code token monitor on the Waveshare ESP32-S3-RLCD-4.2 with a developer-friendly display. What do you suggest? Give me a plan first, we'll discuss before implementing."*

A real-time token usage dashboard for [Qwen Code](https://github.com/QwenLM/qwen-code). An ESP32-S3 reflective LCD receives data from a Node.js BLE bridge and displays daily token consumption, model breakdown, active time, and more.

```
Qwen Code sessions → .jsonl files → Bridge (Node.js)
    → BLE write → ESP32 GATT → LVGL UI → 400×300 reflective LCD
```

## Hardware

- [Waveshare ESP32-S3-RLCD-4.2](https://www.waveshare.net/wiki/ESP32-S3-RLCD-4.2)
  - 400×300 reflective LCD (e-ink style, always-on, no backlight)
  - Onboard SHTC3 temperature/humidity sensor
  - USB-C for power & flashing

## Project Structure

```
token-monitor-ble/
├── firmware/          # ESP-IDF v5.x firmware
│   ├── main/          # Entry point + config
│   └── components/    # BLE, UI, sensor modules
└── bridge/            # Node.js BLE bridge
    └── src/index.js   # Reads session files → BLE push
```

## Quick Start (New Device)

### Step 1: Flash Firmware (ESP32, one-time)

**Prerequisites:** [ESP-IDF v5.x](https://dl.espressif.com/dl/esp-idf/) installed

```bash
cd firmware

# Create secrets.h (BLE mode doesn't need real values, placeholders are fine)
cp main/secrets.h.example main/secrets.h

# Load ESP-IDF environment (adjust path to your installation)
source ~/esp/esp-idf/export.sh

# Build & flash (plug in USB-C first)
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash
```

After flashing, the screen lights up showing the `QWEN CODE` dashboard skeleton.

#### Custom Greeting Name

The header shows "早上好～" (Good morning) by default. To add a name (e.g. "山果，早上好～"):

**1. Regenerate the Chinese font** (the default font only contains limited CJK characters):

```bash
npx lv_font_conv \
  --font /Library/Fonts/Alibaba-PuHuiTi-Medium.otf \
  --symbols "YourName，早上好～下午晚上" \
  --size 18 --bpp 1 --format lvgl \
  --lv-font-name font_zh18 --lv-include lvgl.h --no-compress \
  -o firmware/components/ui_app/font_zh18.c
```

> Replace `YourName` with the actual CJK characters. All required characters must be in `--symbols`.

**2. Pass the name at build time:**

```bash
cmake -DRLCD_GREETING_NAME="山果" -S . -B build
idf.py build
idf.py -p /dev/cu.usbmodem* flash
```

> **Note:** `idf.py build -- -DRLCD_GREETING_NAME="xxx"` **does not work** — idf.py doesn't support `--` for passing cmake variables. You must call `cmake` separately first.

> **Windows users:** See the Windows quick start section in [firmware/README.md](firmware/README.md).

### Step 2: Start Bridge (Host Computer, on every boot)

**Prerequisites:** Node.js v18–v22

> **⚠️ macOS users: Do NOT run the bridge inside tmux or screen.** CoreBluetooth permissions are tied to the GUI login session and cannot be inherited by terminal multiplexers — the process will silently abort (exit code 134). Use a regular Terminal.app / iTerm2 window.

```bash
# Install dependencies (first time only)
cd bridge
npm install

# Start
node src/index.js
```

When you see `[ble] ready` in the terminal, the connection is established and data starts flowing to the screen.

### Step 3: Verify

1. Left side shows daily token count with progress bar and Top 3 models
2. Right side shows session count, active time, input/output tokens, cache rate, and 7-day total
3. Header shows greeting, time, and indoor temperature/humidity

## Screen Layout

```
┌──────────────────────────────────────────────────────────────────┐
│  山果，下午好～        06-09 14:30         26.5°C 62%           │
├──────────────────────────────────────────────────────────────────┤
│                                      │                           │
│  QWEN CODE                           │  Today Sessions           │
│                                      │  11                       │
│  DAILY SMALL GOAL: 100M              │                           │
│  2,000,000 Tokens                    │  Active Time              │
│  [████░░░░░░░░░░░░░░░░░] 2.0%       │  1h27                     │
│                                      │                           │
│  Today Top3 Models                   │  Input Tokens             │
│  qwen3-coder                  62%    │  4.2M                     │
│  claude-opus-4                25%    │                           │
│  deepseek-r1                  13%    │  Output Tokens            │
│                                      │  0.8M                     │
│  ─────────────────────────           │                           │
│  QwenLM/qwen-code                    │  Cache Rate               │
│                                      │  33%                      │
│                                      │  ─────────────            │
│                                      │  Last 7 Days              │
│                                      │  28.8M                    │
└──────────────────────────────────────┴───────────────────────────┘
```

## Data Source

The bridge scans `~/.qwen/projects/*/chats/*.jsonl` (Qwen Code session logs),
extracts `usageMetadata` to aggregate token usage, and pushes it to the ESP32 via BLE every second.

## BLE Protocol

Device name: `QwenToken`. Uses custom 128-bit UUIDs for GATT Service/Characteristic.
Payload is `|`-delimited text (v3 format):

```
3|todayTotal|sessions|cached|cacheRate|activeMin|updatedAt|model1|pct1|model2|pct2|model3|pct3|errors|ageSec|output|weekTotal|input
```

## Environment Variables (Bridge)

| Variable | Default | Description |
|----------|---------|-------------|
| `QWEN_BLE_PUSH_MS` | 1000 | Push interval in milliseconds |
| `QWEN_BLE_SCAN_DAYS` | 7 | Scan session files from the last N days |
| `QWEN_BLE_TAIL_BYTES` | 2097152 | Tail bytes to read per session file |

## Notes

- macOS will prompt for Bluetooth permission on first Bridge run — allow it
- Bridge must run in a **terminal with Bluetooth access** (iTerm / Terminal.app), not in sandboxed environments
- Keep ESP32 within 10m of the host computer
- First build downloads LVGL (~50MB) via IDF Component Manager — requires internet

## Troubleshooting

### Screen is on but no data

**Check if Bridge is running:**

```bash
tail -10 /tmp/qwen-token-ble-bridge.log

# [ble] wrote ... → bridge is pushing data
# [ble] scanning for QwenToken → not yet connected
```

**Check if the BLE device is visible:**

```bash
# macOS built-in (no install needed)
system_profiler SPBluetoothDataType 2>/dev/null | grep -A5 "QwenToken"

# Or scan with Python bleak (shows signal strength)
pip3 install bleak
python3 -c "
import asyncio
from bleak import BleakScanner
async def scan():
    devices = await BleakScanner.discover(timeout=5)
    for d in sorted(devices, key=lambda x: x.rssi, reverse=True):
        if d.name:
            print(f'{d.rssi:>4}dBm  {d.name:<20} {d.address}')
asyncio.run(scan())
"
```

> **Note:** BLE devices do NOT appear in macOS System Preferences → Bluetooth (that only shows classic Bluetooth). Use the commands above.

### Bridge connects to wrong device (multiple devices)

All devices advertise as `QwenToken`. Bridge connects to whichever it discovers first.

1. Power off the unwanted device (unplug USB-C)
2. `Ctrl+C` the bridge, then restart `node src/index.js`
3. Bridge will rescan and connect to the only online device

### How to power off the device

- **The Waveshare ESP32-S3-RLCD-4.2 has no power switch** — just unplug USB-C
- If the device has a battery, simply remove it. Cutting power to ESP32 will not damage hardware or data
- BLE disconnect may take a few seconds after power-off; Bridge will auto-detect and rescan

### idf.py not found

```bash
# Load ESP-IDF environment (needed for each new terminal session)
source ~/esp/esp-idf/export.sh

# If idf.py is still not found, call it via Python directly
python3 ~/esp/esp-idf/tools/idf.py build
```

### Bridge exits immediately (exit code 134 / abort)

On macOS, `@abandonware/noble` uses CoreBluetooth native bindings. The process will silently `abort()` if it cannot access the Bluetooth subsystem. The most common cause is **running inside tmux / screen** — these multiplexers create sessions detached from the macOS GUI login session, so the Bluetooth TCC (privacy) permission is not inherited.

**Fix:** Run the bridge in a regular Terminal.app / iTerm2 window (not inside tmux or screen):

```bash
cd bridge
node src/index.js
```

> **Tip:** If you need tmux for other work, just keep one standalone terminal tab for the bridge.

Other possible causes:
- First run — macOS will prompt for Bluetooth permission; make sure to click **Allow**
- Running in a sandboxed IDE terminal (some VS Code configurations) — try a standalone terminal instead
