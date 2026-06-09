# Token Monitor BLE

[English](README.md) | 中文

> **Vibe Coding Project** — 本项目完全通过 AI 对话驱动开发（vibe coding），原始 prompt：
>
> *"参考这个项目 https://github.com/CEJXXX/token-monitor-RLCD Waveshare ESP32-S3-RLCD-4.2 做一个 Qwen Code 的 token 展示器，开发者友好显示，你有哪些建议吗？你先给一个方案，讨论和时候我们再开始实施。"*

Qwen Code token 用量实时仪表盘。ESP32-S3 墨水屏通过 BLE 接收电脑端
Bridge 推送的数据，显示今日 token 消耗、模型分布、活跃时长等指标。

```
Qwen Code sessions → .jsonl files → Bridge (Node.js)
    → BLE write → ESP32 GATT → LVGL UI → 400×300 墨水屏
```

## 硬件

- [Waveshare ESP32-S3-RLCD-4.2](https://www.waveshare.net/wiki/ESP32-S3-RLCD-4.2)
  - 400×300 反射式 LCD（墨水屏风格）
  - 板载 SHTC3 温湿度传感器
  - USB-C 供电 & 烧录

## 项目结构

```
token-monitor-ble/
├── firmware/          # ESP-IDF v5.x 固件
│   ├── main/          # 入口 + 配置
│   └── components/    # BLE、UI、传感器等模块
└── bridge/            # Node.js BLE 桥接进程
    └── src/index.js   # 读取会话文件 → BLE 推送
```

## 快速配置（新设备）

### 一、烧录固件（ESP32 端，只需一次）

**前置条件：** 已安装 [ESP-IDF v5.x](https://dl.espressif.com/dl/esp-idf/)

```bash
# 进入固件目录
cd firmware

# 创建 secrets.h（BLE 模式不用填真实值，占位即可编译）
cp main/secrets.h.example main/secrets.h

# 加载 ESP-IDF 环境（路径按实际安装位置调整）
source ~/esp/esp-idf/export.sh

# 编译 & 烧录（先插上 USB-C）
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash
```

烧录完成后屏幕亮起，显示 `QWEN CODE` 仪表盘骨架。

#### 自定义问候名

屏幕头部默认显示 "早上好～"，如需加上名字（如 "山果，早上好～"），需要两步：

**1. 重新生成中文字体**（默认字体只含有限汉字，自定义名字需要补充）：

```bash
npx lv_font_conv \
  --font /Library/Fonts/Alibaba-PuHuiTi-Medium.otf \
  --symbols "你的名字，早上好～下午晚上" \
  --size 18 --bpp 1 --format lvgl \
  --lv-font-name font_zh18 --lv-include lvgl.h --no-compress \
  -o firmware/components/ui_app/font_zh18.c
```

> 将 `你的名字` 替换为实际名字，确保所有需要的汉字都在 `--symbols` 参数中。
> 字体文件路径按系统实际情况调整（macOS 一般在 `/Library/Fonts/`）。

**2. 编译时传入名字：**

```bash
# 先通过 cmake 设置名字变量
cmake -DRLCD_GREETING_NAME="山果" -S . -B build

# 然后正常编译烧录
idf.py build
idf.py -p /dev/cu.usbmodem* flash
```

> **注意：** `idf.py build -- -DRLCD_GREETING_NAME="xxx"` **不生效**，idf.py 不支持 `--` 传递 cmake 参数，必须先单独调用 `cmake`。

> **Windows 用户：** 详见 [firmware/README.md](firmware/README.md) 中的 Windows quick start 章节。

### 二、启动 Bridge（电脑端，每次开机需启动）

**前置要求：** Node.js v18–v22

> **⚠️ macOS 用户：不要在 tmux 或 screen 中运行 Bridge。** CoreBluetooth 权限绑定在 GUI 登录会话上，终端复用器无法继承该权限，进程会静默崩溃（exit code 134）。请使用原生 Terminal.app / iTerm2 窗口。

```bash
# 安装依赖（首次）
cd bridge
npm install

# 启动
node src/index.js
```

终端看到 `[ble] ready` 表示连接成功，屏幕开始显示实时数据。

### 三、验证

1. 屏幕左侧显示今日 token 消耗数字、进度条、Top3 模型
2. 屏幕右侧显示 Session 数、活跃时长、Input/Output Tokens 等
3. 头部显示问候语、时间、室内温湿度

## 屏幕布局

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

## 数据来源

Bridge 扫描 `~/.qwen/projects/*/chats/*.jsonl`（Qwen Code 的会话记录），
从中提取 `usageMetadata` 汇总 token 用量，每秒通过 BLE 推送到 ESP32。

## BLE 协议

设备名：`QwenToken`，GATT Service/Characteristic 使用自定义 128-bit UUID。
Payload 为 `|` 分隔的文本（v3 格式）：

```
3|todayTotal|sessions|cached|cacheRate|activeMin|updatedAt|model1|pct1|model2|pct2|model3|pct3|errors|ageSec|output|weekTotal|input
```

## 环境变量（Bridge）

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `QWEN_BLE_PUSH_MS` | 1000 | 推送间隔（毫秒） |
| `QWEN_BLE_SCAN_DAYS` | 7 | 扫描最近 N 天的会话文件 |
| `QWEN_BLE_TAIL_BYTES` | 2097152 | 每个文件读取尾部字节数 |

## 注意事项

- macOS 首次运行 Bridge 会弹蓝牙权限请求，需允许终端访问蓝牙
- Bridge 必须在**有蓝牙权限的终端**里运行（iTerm / Terminal.app），sandbox 环境无法使用 BLE
- ESP32 与电脑蓝牙距离保持在 10m 以内
- 首次编译会通过 IDF Component Manager 下载 LVGL（~50MB），需联网

## Troubleshooting

### 屏幕亮了但没有数据

**检查 Bridge 是否在运行：**

```bash
# 查看 bridge 日志
tail -10 /tmp/qwen-token-ble-bridge.log

# 如果看到 [ble] wrote ... 表示 bridge 正在推送数据
# 如果看到 [ble] scanning for QwenToken 表示还没连上设备
```

**检查 BLE 设备是否可见：**

```bash
# macOS 系统自带（无需安装）
system_profiler SPBluetoothDataType 2>/dev/null | grep -A5 "QwenToken"

# 或用 Python bleak 扫描（能看到信号强度）
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

> **注意：** BLE 设备不会出现在 macOS 系统偏好设置的蓝牙列表中（那里只显示经典蓝牙），必须用上述命令查看。

### 多台设备时 Bridge 连错设备

所有设备的 BLE 名称都是 `QwenToken`，Bridge 会连接最先发现的那台。如果连错了：

1. 将不需要的设备断电（拔掉 USB-C）
2. 在 Bridge 终端按 `Ctrl+C` 停止，然后重新启动 `node src/index.js`
3. Bridge 会重新扫描并连接唯一在线的设备

### 如何关闭/断电设备

- **Waveshare ESP32-S3-RLCD-4.2 没有电源开关**，拔掉 USB-C 线即可断电
- 如果设备配有电池，直接拆下电池即可，ESP32 断电不会损坏硬件或数据
- 断电后 BLE 连接可能有几秒延迟才断开，Bridge 会自动检测并重新扫描

### idf.py 不在 PATH 中

```bash
# 加载 ESP-IDF 环境（每次新开终端都需要）
source ~/esp/esp-idf/export.sh

# 如果 idf.py 仍然找不到，尝试直接用 python 调用
python3 ~/esp/esp-idf/tools/idf.py build
```

### Bridge 启动后立即退出（exit code 134 / abort）

macOS 上 `@abandonware/noble` 使用 CoreBluetooth 原生绑定。如果进程无法访问蓝牙子系统，会静默 `abort()`。最常见的原因是**在 tmux / screen 中运行** — 终端复用器创建的 session 脱离了 macOS GUI 登录会话，蓝牙 TCC（隐私）权限无法继承。

**解决：** 在原生 Terminal.app / iTerm2 窗口中运行（不要在 tmux 或 screen 里）：

```bash
cd bridge
node src/index.js
```

> **提示：** 如果日常需要 tmux，单独留一个终端标签页跑 bridge 即可。

其他可能原因：
- 首次运行 — macOS 会弹窗请求蓝牙权限，务必点击**允许**
- 在沙箱化的 IDE 终端中运行（部分 VS Code 配置）— 换用独立终端窗口
