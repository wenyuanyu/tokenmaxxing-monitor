# TokenMaxxing RLCD

[English](README.md) | 中文

TokenMaxxing RLCD 是一个本地优先的 AI Coding Agent token 活动仪表盘。电脑端
Node.js bridge 汇总配置过的数据源，通过 BLE 推送到 Waveshare
ESP32-S3-RLCD-4.2，固件在 400 x 300 反射式 LCD 上显示实时用量；macOS 菜单栏
App 可以显示同一份状态。

固件尽量保持简单，数据源适配集中在 `bridge/src/index.js`。后续要支持 Codex、
Qwen Code、Claude 或其他 Agent，通常只需要新增一个 bridge reader。

## 显示内容

- 今日 token 总量和 100M 小目标进度
- 今日 Top 3 模型
- Sessions、活跃时长、输入/输出 token、缓存率、最近 7 天总量
- 板载 SHTC3 温湿度
- 电池百分比和 USB/外部电源状态
- KEY 按键切换的 activity 页面：
  - 最近 6 个月 activity grid
  - lifetime tokens
  - peak daily tokens
  - streak
  - longest task

Activity cell 使用 5 档单色纹理。RLCD 实际按 1-bit 输出，不是真灰阶：

| Level | Token 区间 | Pattern |
| --- | --- | --- |
| 0 | `0` | 全白 |
| 1 | `(0, 1M)` | `░`，约 1/3 黑 |
| 2 | `[1M, 10M)` | `▒`，1/2 黑 |
| 3 | `[10M, 100M)` | `▓`，约 2/3 黑 |
| 4 | `[100M, +inf)` | `█`，全黑 |

## 架构

```text
Qwen Code / Codex logs
        |
        v
bridge/src/index.js
  - 扫描配置过的数据源
  - 汇总今日和历史指标
  - 写入 /tmp/qwen-token-status.json
  - 在数据变化或 heartbeat 到期时推送 BLE v3 payload
        |
        v
ESP32-S3 BLE GATT characteristic
        |
        v
firmware
  - NimBLE 接收
  - LVGL UI
  - SHTC3 / battery / KEY button tasks
  - 1-bit RLCD flush 中生成 activity 纹理
        |
        v
400 x 300 reflective LCD
```

电池状态显示在右下角：未充电时百分比居中；充电时百分比左移，右侧在同一个电池框内显示
1 x 1 pixel-art 黑色闪电。电池日志仍按固件设置的间隔写入 `batlog` 分区，UI 图标变化不会提高
flash 写入频率。

## 目录结构

```text
.
├── bridge/                 # Node.js BLE bridge 和数据源 reader
│   ├── package.json
│   └── src/index.js
├── firmware/               # ESP-IDF 固件
│   ├── main/
│   └── components/
├── menubar/                # macOS SwiftUI 菜单栏 App
│   ├── QwenBridgeBar.swift
│   └── build.sh
├── tools/lvgl_preview/     # 本地 LVGL framebuffer 渲染
├── AGENTS.md               # 给 Codex/Claude/其他 Agent 的开发说明
└── README.md
```

## 硬件

- [Waveshare ESP32-S3-RLCD-4.2](https://www.waveshare.com/wiki/ESP32-S3-RLCD-4.2)
- ESP32-S3 + BLE
- 400 x 300 反射式 LCD
- 板载 SHTC3 温湿度传感器
- KEY button，GPIO 18
- 固件支持电池 ADC 读数

## 快速开始

### 1. 编译并烧录固件

前置条件：

- ESP-IDF v5.x
- Node.js，用于构建时生成字体

```bash
cd firmware
cp main/secrets.h.example main/secrets.h
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py -DRLCD_GREETING_NAME="YourName" -DRLCD_BLE_DEVICE_NAME="QwenToken" -p /dev/cu.usbmodem* build flash
```

说明：

- `RLCD_GREETING_NAME` 可选。留空就是通用问候语。
- 中文/CJK 问候名会在构建时合并进 LVGL 字体。
- 生成字体保存在 `firmware/build/`，不会提交进 repo。

### 2. 运行 Bridge

前置条件：

- Node.js 18+
- macOS BLE bridge 依赖 `@abandonware/noble` / CoreBluetooth

```bash
cd bridge
npm install
TOKEN_MONITOR_DATASOURCES=qwen,codex QWEN_BLE_DEVICE_NAME=QwenToken npm start
```

看到下面日志表示连接成功：

```text
[ble] adapter state: poweredOn
[ble] scanning for QwenToken
[ble] connecting QwenToken
[ble] ready
[ble] wrote 3|...
```

Bridge 同时会写状态文件：

```text
/tmp/qwen-token-status.json
```

### 3. 可选：macOS 菜单栏 App

```bash
cd menubar
bash build.sh dmg
open QwenBridgeBar.app
```

App 会打包 bridge 脚本和 `node_modules`，启动 bridge 子进程，读取
`/tmp/qwen-token-status.json`，并在 macOS 13+ 上注册 Login Item。

生成的 DMG：

```text
menubar/QwenBridgeBar.dmg
```

## Bridge 数据源

用环境变量配置数据源：

```bash
TOKEN_MONITOR_DATASOURCES=qwen,codex
```

当前支持：

| Source | 扫描内容 |
| --- | --- |
| `qwen` | Qwen runtime usage 目录下的月度 JSONL usage 文件 |
| `codex` | `$CODEX_HOME/sessions` 下的 Codex rollout JSONL 文件 |

常用环境变量：

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `TOKEN_MONITOR_DATASOURCES` | `qwen` | 逗号分隔的数据源列表 |
| `TOKEN_MONITOR_HISTORY_DAYS` | `3650` | lifetime、peak、streak、longest task 使用的历史窗口 |
| `QWEN_BLE_DEVICE_NAME` | `QwenToken,Qwen Usage` | 可连接的 BLE 设备名，逗号分隔 |
| `QWEN_BLE_PUSH_MS` | `5000` | bridge 聚合 tick；BLE 只有数据变化或 heartbeat 到期才实际写入 |
| `QWEN_BLE_HEARTBEAT_MS` | `60000` | payload 不变时的最大 BLE heartbeat 间隔 |
| `QWEN_BLE_CONNECT_TIMEOUT_RESTARTS` | `3` | 连续 BLE connect timeout 到达该次数后退出，让 LaunchAgent/launchd 重启 CoreBluetooth 状态；设为 `0` 可关闭 |
| `QWEN_BLE_SCAN_DAYS` | `7` | 最近窗口，用于 week/current 指标 |
| `QWEN_RUNTIME_DIR` | 从 `~/.qwen/.env` 推导 | Qwen runtime 目录 |
| `CODEX_HOME` | `~/.codex` | Codex 主目录 |
| `CODEX_SESSIONS_DIR` | `$CODEX_HOME/sessions` | Codex rollout 目录 |
| `CODEX_STATE_DB` | `$CODEX_HOME/state_5.sqlite` | 可选，用于读取 Codex thread 的模型名 |

## BLE 协议

Service UUID：

```text
00112233-4455-6677-8899-aabbccddeeff
```

Writable characteristic UUID：

```text
00112233-4455-6677-8899-aabbccddee01
```

当前 payload 是 `|` 分隔的 v3 文本：

```text
3|todayTotal|sessionsToday|todayCached|cacheRate|activeMinutes|updatedAt|
model1|model1Pct|model2|model2Pct|model3|model3Pct|
errorsToday|ageSec|todayOutput|weekTotal|todayInput|
activity|lifetimeTotal|peakDailyTotal|streakDays|longestTaskMinutes
```

`activity` 字段为 104 个 base36 字符：

- 26 列
- 每列 7 行
- 每列用 4 个 base36 字符打包
- 行顺序为 Sunday 到 Saturday
- 每个 cell 是 `0..4` 的 base-5 level

固件保留 v1/v2 parser 兼容旧 bridge，新 bridge 应使用 v3。

## 固件 UI 说明

- 主页面：token dashboard、环境传感器、电池状态。
- Activity 页面：按板子 KEY button 切换。
- RLCD 按 1-bit 输出，activity shade 在 `firmware/main/main.cpp` 的 LVGL flush 阶段用 10 x 10 cell dithering 生成。
- UI 布局在 `firmware/components/ui_app/ui_app.cpp`。
- BLE 解包在 `firmware/components/ble_app/ble_app.c`。
- 共享 report schema 在 `firmware/components/usage_client/usage_client.h`。

## 本地 LVGL 预览

先至少 build 一次固件，让 `font_zh18.c` 被生成。

```bash
cd tools/lvgl_preview
bash render.sh
```

输出：

```text
outputs/lvgl-preview/dashboard.ppm
outputs/lvgl-preview/activity.ppm
```

macOS 下转 PNG：

```bash
sips -s format png outputs/lvgl-preview/activity.ppm --out outputs/lvgl-preview/activity.png
```

## 新增数据源

新增 Agent 数据源通常只改 bridge：

1. 在 `bridge/src/index.js` 添加 reader function。
2. 返回 `emptyTotals(sourceName)` 形状的数据。
3. 填充今日 totals、model totals、session events、task events、daily totals、latest metadata。
4. 在 `buildReport()` 的 `readers` map 中注册。
5. 用 `TOKEN_MONITOR_DATASOURCES=yourSource` 启动。

如果 BLE payload 字段不变，不需要改固件。

## Troubleshooting

### Bridge 一直 scanning

- 确认固件里的 BLE 名和 `QWEN_BLE_DEVICE_NAME` 一致。
- 确认设备已上电并在蓝牙范围内。
- BLE 设备不一定显示在 macOS 蓝牙设置里，以 bridge log 为准。

### Bridge 在 macOS 上立即退出

请在普通 Terminal.app 或 iTerm2 窗口里运行。CoreBluetooth 在 detached
shell、terminal multiplexer 或 sandboxed launch context 里可能失败。

### 烧录后屏幕没数据

烧录会重置 BLE，电脑端 bridge 可能需要重新连接或重启。

### Activity 看起来不是灰阶

这是预期行为。RLCD 是 1-bit 输出，五档 shade 是黑白纹理，不是真灰阶。

## 隐私

Bridge 读取本机 usage log，只通过 BLE 发送聚合指标，不上传远端服务。不要提交本地
chat export、生成产物、status JSON、log 或个人问候名。
