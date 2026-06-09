# Token Monitor BLE

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

# 编译 & 烧录（先插上 USB-C）
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash
```

烧录完成后屏幕亮起，显示 `QWEN CODE` 仪表盘骨架。

> **Windows 用户：** 详见 [firmware/README.md](firmware/README.md) 中的 Windows quick start 章节。

### 二、启动 Bridge（电脑端，每次开机需启动）

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
