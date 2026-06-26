# Firmware

ESP-IDF firmware for the Waveshare ESP32-S3-RLCD-4.2 board.

This firmware does not fetch token data over Wi-Fi. It exposes a BLE GATT server,
accepts compact text payloads from the host bridge, and renders the current
report with LVGL on the 400 x 300 reflective LCD.

## Responsibilities

- Advertise a BLE peripheral named by `RLCD_BLE_DEVICE_NAME`.
- Receive bridge payloads through a writable GATT characteristic.
- Decode v1/v2/v3 payload formats into `usage_report_t`.
- Render:
  - main token dashboard
  - activity page
  - temperature/humidity
  - battery status
- Poll KEY button on GPIO 18 to toggle pages.
- Sample battery data and append it to the `batlog` partition.

## Layout

```text
firmware/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── main/
│   ├── main.cpp              # LVGL flush, RLCD dither patterns, app entry
│   ├── user_config.h         # display pins and dimensions
│   └── secrets.h.example     # copied to secrets.h for compatibility
└── components/
    ├── app_bsp/              # LVGL display port, ticks, buffers, lock
    ├── ble_app/              # NimBLE GATT server and payload parser
    ├── port_bsp/             # Waveshare RLCD panel driver
    ├── sensor/               # SHTC3, battery ADC, battery log partition
    ├── ui_app/               # LVGL UI and font generation
    ├── usage_client/         # shared usage_report_t schema
    └── user_app/             # app glue, tasks, key handling
```

## Build And Flash

```bash
cd firmware
cp main/secrets.h.example main/secrets.h
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py -DRLCD_GREETING_NAME="YourName" -DRLCD_BLE_DEVICE_NAME="QwenToken" -p /dev/cu.usbmodem* build flash
```

`RLCD_GREETING_NAME` can be left empty for a generic greeting. If it contains
CJK characters, the `ui_app` CMake target runs `lv_font_conv` and generates a
font containing those glyphs under `build/`.

## BLE Payload

The current bridge emits v3:

```text
3|todayTotal|sessionsToday|todayCached|cacheRate|activeMinutes|updatedAt|
model1|model1Pct|model2|model2Pct|model3|model3Pct|
errorsToday|ageSec|todayOutput|weekTotal|todayInput|
activity|lifetimeTotal|peakDailyTotal|streakDays|longestTaskMinutes
```

Parser entry point:

```text
components/ble_app/ble_app.c
```

Shared decoded struct:

```text
components/usage_client/usage_client.h
```

The firmware keeps older parsers so older bridges do not immediately break, but
new work should target v3.

## Activity Rendering

The activity grid is 26 columns x 7 rows. Each cell is 10 x 10 pixels with a
12-pixel stride.

The panel is driven as 1-bit monochrome, so activity levels are represented by
black/white dithering rather than real grayscale. UI cells are assigned marker
colors in `components/ui_app/ui_app.cpp`; `main/main.cpp` recognizes those marker
colors in `Lvgl_FlushCallback()` and converts them to the final 1-bit output.

| Level | Visual | Black pixels in a 10 x 10 cell |
| --- | --- | --- |
| 0 | blank | 0 |
| 1 | `░` | 33 |
| 2 | `▒` | 50 |
| 3 | `▓` | 67 |
| 4 | `█` | 100 |

This avoids creating hundreds of tiny LVGL child objects and keeps the output
consistent on hardware.

## Sensors And Battery

- SHTC3 is on I2C address `0x70`.
- Battery percentage is estimated from ADC voltage in `components/sensor/battery.c`.
- USB/external power is inferred in `components/user_app/user_app.cpp`.
- Battery samples are appended every 30 seconds to the `batlog` partition.

## KEY Button

`USER_KEY_GPIO` is GPIO 18. It is polled and debounced in
`components/user_app/user_app.cpp`. A press toggles between dashboard and
activity page.

## Local LVGL Preview

The local preview builds a host executable that renders UI frames to PPM files.
Run a firmware build once first so generated fonts exist.

```bash
cd ../tools/lvgl_preview
bash render.sh
```

Outputs:

```text
outputs/lvgl-preview/dashboard.ppm
outputs/lvgl-preview/activity.ppm
```

## Common Changes

### Change the BLE device name

```bash
idf.py -DRLCD_BLE_DEVICE_NAME="MyTokenDisplay" build flash
```

Then run the bridge with:

```bash
QWEN_BLE_DEVICE_NAME=MyTokenDisplay npm start
```

### Change the greeting

```bash
idf.py -DRLCD_GREETING_NAME="YourName" build flash
```

Do not commit generated font output or personal names.

### Add a payload field

1. Append the field to `bridge/src/index.js` `toPayload()`.
2. Add the field to `usage_report_t`.
3. Parse it in `ble_app.c` as an optional v3 tail field.
4. Render it in `ui_app.cpp`.
5. Keep older fields in the same order.

## Hardware Reference

```text
RLCD: 400 x 300
SPI3: MOSI=12, SCK=11, DC=5, CS=40, RST=41
I2C:  SDA=13, SCL=14
KEY:  GPIO18
```
