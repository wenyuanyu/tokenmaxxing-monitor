---
name: nimble-stale-connection
description: 排查 ESP32 NimBLE 广播被阻塞（僵死连接导致节点不广播）的步骤
source: auto-skill
extracted_at: '2026-06-17T05:40:56.764Z'
---

# ESP32 NimBLE 僵死连接排查

## 现象

`noble`（或 `@abandonware/noble`）端输出 `[ble] adapter state: poweredOn` 后，扫描不到名为 `QwenToken`（或目标设备名）的 BLE 设备。其他 BLE 设备（如耳机、手表）能正常发现。

## 可能原因

ESP32 端的 NimBLE 栈认为**还存在活跃连接**，因此不会重新广播。常见触发场景：

- Bridge 进程被 `SIGKILL` / 强制关闭，未走 disconnect 流程
- 终端被关闭但 BLE 连接没被清理
- ESP32 按 RST 按钮重启后，NimBLE controller 层残留了旧的连接状态

## 排查步骤

### 1. 确认 Mac 端 BLE 适配器正常

用 `noble` 做一次裸扫描，确认适配器工作正常且能看到周围设备：

```js
const noble = require('@abandonware/noble');
noble.on('stateChange', s => {
  if (s !== 'poweredOn') return;
  noble.startScanning([], true);
  noble.on('discover', p => {
    console.log(p.advertisement.localName || '(unnamed)', p.rssi);
  });
  setTimeout(() => { noble.stopScanning(); process.exit(0); }, 10000);
});
```

如果发现了其他设备但唯独没有目标设备 → 问题在 ESP32 端。

### 2. 检查 ESP32 串口日志（最直接的方法）

macOS 上用 `pyserial` 抓取启动日志：

```python
import serial, time

port = serial.Serial('/dev/cu.usbmodemXXXXX', 115200, timeout=1)  # 替换为实际端口
# 硬件复位
port.dtr = False; port.rts = True; time.sleep(0.1)
port.dtr = True;  port.rts = False; time.sleep(0.1)
port.dtr = False; time.sleep(0.1)
port.dtr = True

end = time.time() + 10
while time.time() < end:
    data = port.read(1024)
    if data:
        print(data.decode('utf-8', errors='replace'), end='', flush=True)
port.close()
```

关键日志：

| 日志 | 含义 |
|------|------|
| `advertising as QwenToken` | BLE 广播正常开启 |
| `connected` | 设备已被连接 |
| `rx: 3\|...` | 正在接收数据（说明 bridge 已连接并写入） |
| `Failed to restore IRKs` | 正常日志，可忽略 |
| `nimble init failed rc=xxx` | BLE 栈初始化失败 |

如果看到 `connected` + `rx:` 日志，说明 ESP32 正常工作了。

### 3. 用硬件级复位清除僵死连接

- 按 RST 按钮**不一定能**完全清除 NimBLE controller 的连接状态
- **更可靠的方式**：通过 USB 串口的 DTR/RTS 信号做硬件复位（如上述 Python 脚本中的 reset 序列）
- 或者：拔插 USB 线，走一次完整的 USB 枚举 + 电源复位

### 4. 长期修复建议

在固件中增加启动时的 BLE 状态清理逻辑：

```c
// 在 app_main 的 ble_app_init 之前调用
ble_hs_sched_reset(BLE_HS_ECODE_HCI_CTLR_TIMEOUT);
```

或者在 `on_sync` 回调中主动断开旧连接：

```c
static void on_sync(void) {
    // 先清理所有残留连接
    ble_gap_terminate_all(BLE_ERR_CONN_TERM_LOCAL);
    // ... 然后正常启动广播
}
```

## 确认修复成功

串口日志中看到以下三条关键日志序列，说明 BLE 链路完全恢复正常：

```
I (1626) ble_app: advertising as QwenToken   ← 广播恢复
I (2079) ble_app: connected                   ← bridge 连接成功
I (3186) ble_app: rx: 3|12735220|8|...       ← 数据正常推送到 ESP32
```

如果 bridge 终端正常输出 `[ble] ready` 和 `[ble] wrote 3|...`，服务可用。

## 备注

- Mac 上的 `noble` / `@abandonware/noble` 使用 CoreBluetooth，不直接显示 RSSI 127 的异常值
- ESP32 的 `ESP_ERROR_CHECK(ble_app_init(...))` 会在 init 失败时 panic 并反复重启
- **如果屏幕正常显示仪表盘但 BLE 不广播，90% 是僵死连接问题**
- **电源拔插（走完整 USB 枚举）比按 RST 按钮更可靠**——RST 按钮可能无法完全清除 NimBLE controller 的残留连接状态
- 串口的 DTR/RTS 硬件复位序列（上述 Python 代码中的 reset 逻辑）是判定僵死连接的最佳手段：如果复位后 bridge 立刻连上并开始推数据，那就是僵死连接