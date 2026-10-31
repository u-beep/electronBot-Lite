# ElectronBot-Lite 通信协议与架构笔记

> 方向 4（协议研究与文档化）产出。所有字段均已对照源码核实：
> `2.SDK/electron_low_level.cpp`、`1.Firmware/ElectronBot-fw/UserApp/main.cpp`、
> `1.Firmware/ElectronBot-fw/Bsp/robot.{h,cpp}`、`1.Firmware/ServoDrive-fw/UserApp/main.cpp`。

---

## 1. USB 协议（PC ⇄ 主控，STM32F405）

### 1.1 设备与端点

| 项 | 值 |
|---|---|
| 设备类型 | 自定义 CDC，Bulk 传输 |
| VID / PID | `0x1001` / `0x8023` |
| 端点 | EP1 OUT（下行）/ EP1 IN（上行） |
| PC 驱动 | `3.Driver/BotDriver`（未签名，需禁用强制签名） |
| DLL | `USBInterface.dll`（`USB_ScanDevice / USB_OpenDevice / USB_BulkTransmit / USB_BulkReceive`） |

### 1.2 一帧同步的时序（`ElectronLowLevel::SyncTask` ⇄ 固件主循环）

一帧 = 240×240 RGB888 = **172800 B** 图像，分 **4 段**（每段 60 行）。每段流程：

| 步骤 | 方向 | 内容 | 字节数 |
|---|---|---|---|
| 1 | MCU → PC | 32 B 额外数据（舵机反馈） | 1 包 × 32 |
| 2 | PC → MCU | 图像段主体 | 84 包 × 512 = **43008** |
| 3 | PC → MCU | 段尾 + 关节设定 | 1 包 × 224 = 192（图像尾）+ 32（额外数据） |

每段下行合计 **43232 B**（43008 + 224，其中图像 43200 = 43008 + 192，额外数据 32）。

关键点：
* **步 1 是 MCU 主动请求**：固件发 32 B 后 PC 才发图像段，天然形成逐段流控。
* 固件收到一段立即经 **SPI DMA** 刷到 GC9A01（分段刷屏，不需要整帧缓冲）。
* 双方都用 **ping-pong 双缓冲**（SDK 侧 `frameBufferTx[2]`，固件侧 `usbBuffer.rxData[2][43232]`），
  收发与解耦互不阻塞。
* PC 侧 `Sync()` 为异步语义：join 上一帧线程后起新线程，30fps 调用不会堆积。
* 224 B 包的尾 32 B 同时承载"使能位 + 6 关节设定值"，即**图像和关节数据同帧到达**。

### 1.3 额外数据结构（32 B，上下行同构，小端）

| 偏移 | 类型 | 含义 |
|---|---|---|
| `[0]` | u8 | 舵机使能标志（1 = 全部使能） |
| `[1..4]` | float LE | 关节 1（头部）角度，单位度（模型角） |
| `[5..8]` | float LE | 关节 2（左臂 roll） |
| `[9..12]` | float LE | 关节 3（左臂 pitch） |
| `[13..16]` | float LE | 关节 4（右臂 roll） |
| `[17..20]` | float LE | 关节 5（右臂 pitch） |
| `[21..24]` | float LE | 关节 6（腰部） |
| `[25..31]` | — | 保留 |

上行方向为**舵机当前实际角度**（I2C 读回的反馈值），下行方向为**设定值**。

### 1.4 带宽账

```
172800 B/帧 × 30 fps = 5.184 MB/s ≈ 41.5 Mbps（净载荷）
USB 2.0 Full Speed  = 12 Mbps  → 不够
USB 2.0 High Speed  = 480 Mbps → 充裕（实测协议开销后 ~3.5 ms/帧）
```

这就是原版选择 USB 2.0 HS 的原因；每段约 0.9 ms 传输 + SPI DMA 刷屏并行。

### 1.5 关节模型角范围（固件 `Bsp/robot.h`）

| 关节 | 舵机 I2C ID | 机械角范围 | 模型角范围（PC 侧使用） | 反向 |
|---|---|---|---|---|
| 1 头部 | 2 | 70~95° | -15 ~ +15° | 是 |
| 2 左臂 roll | 4 | -9~3° | 0 ~ 30° | 否 |
| 3 左臂 pitch | 6 | -16~117° | -20 ~ 180° | 否 |
| 4 右臂 roll | 8 | 133~141° | 0 ~ 30° | 是 |
| 5 右臂 pitch | 10 | 15~150° | -20 ~ 180° | 是 |
| 6 腰部 | 12 | 0~180° | -90 ~ +90° | 否 |

`SetJointAngles` 传入的是模型角，固件内部经 `UpdateJointAngle` 做线性映射到机械角。

---

## 2. I2C 协议（主控 ⇄ 舵机，STM32F042）

### 2.1 帧格式

主机先发 **5 字节** `[指令, 4B 参数(float LE)]`，再读回 **5 字节** `[指令回显, 4B 数据]`。
舵机作为从机监听**自身 ID** 与 **0 号广播地址**；主控 I2C 地址 = ID 本身。

### 2.2 指令表（对照 ServoDrive-fw/UserApp/main.cpp）

| 指令 | 名称 | 参数 / 返回 | 备注 |
|---|---|---|---|
| `0x01` | 设置角度 | float（机械角） | |
| `0x02` | 设置速度 | float | README 未列出 |
| `0x03` | 设置力矩 | float | README 未列出 |
| `0x11` | 读取角度 | 返回 float | |
| `0x12` | 读取速度 | 返回 float | README 未列出 |
| `0x21` | 设置舵机 ID | 新 ID（u8） | 烧录后逐个分配，写入前需复位 |
| `0x22` | 设置 Kp | float | |
| `0x23` | 设置 Ki | float | |
| `0x24` | 设置 Kv | float | |
| `0x25` | 设置 Kd | float | |
| `0x26` | 设置力矩限制 | float（0~1 比例） | |
| `0x27` | 设置上电初始角度 | float | |
| `0xFF` | 使能 / 失能 | 1 = 使能 | |

> 每个 Set 类指令后固件 `HAL_Delay(500)` 等待舵机复位，**不要在总线上快速连发**。

### 2.3 舵机固件内部（F042 侧）

`ADC 电位器采样 → PID 位置闭环 → PWM 驱动电机`，I2C 中断里解析上述指令并回包。
上电后主控先 `HAL_Delay(2000)` 等所有舵机就绪。

---

## 3. 架构剖析笔记

```
PC 30fps 循环                      STM32F405 主控                    STM32F042 舵机 x6
─────────────                     ───────────────                  ─────────────
ExpressionRenderer/图像源          USB CDC EP1 收发                  I2C 从机监听
    │                                  │                                │
SetImageSrc -> frameTx[2] 双缓冲  --4段x43232B-->  rxData[2] 双缓冲      │
SetJointAngles -> extraTx[2]          │ 每段: 收32B反馈                 │
    │                                  │  ├─ SPI DMA 刷 60 行到 GC9A01  │
Sync() 异步线程                        │  └─ 解析 32B 使能+设定值 ──────┼─I2C→ 舵机
GetJointAngles <- 32B 反馈 <───────────┘                               │
                                  UpdateJointAngle 模型角→机械角映射    ├─ ADC 采样
                                                                       ├─ PID 闭环
                                                                       └─ PWM 出力
```

设计上值得学的三件事：
1. **分段流控**：MCU 主动发 32 B 请求才收下一段，PC 永远不会淹掉 MCU，且把"读反馈"和"发图像"合并在同一个握手节拍里，不需要独立的控制通道。
2. **ping-pong 全链路**：SDK 帧缓冲、固件 USB 缓冲双层双缓冲，30fps 下采集/传输/刷屏三级流水互不等待。
3. **图像与控制同路**：32 B 额外数据搭图像段尾的便车，一条 Bulk 管道同时解决显示与控制，协议零额外开销。

---

## 4. PC-Tools 使用速查（无硬件全流程）

```bash
cmake -S 4.PC-Tools -B 4.PC-Tools/build
cmake --build 4.PC-Tools/build --config Release
# 注：若终端同时存在 http_proxy/HTTP_PROXY 等大小写重复的环境变量，
# MSBuild 会报 MSB6001，需先 unset 其中一组。

# 虚拟机器人命令行 demo（彩虹 / 表情两种模式，自动 N 秒退出）
4.PC-Tools/build/Release/sample_virtual.exe [face] [seconds]

# 上位机 GUI（虚拟后端）
4.PC-Tools/build/studio/Release/studio.exe

# 真机后端上位机（需 USBInterface.dll、BotDriver 驱动、真机在线）
cmake -S 4.PC-Tools -B 4.PC-Tools/build2 -DEB_WITH_REAL_SDK=ON
cmake --build 4.PC-Tools/build2 --config Release --target studio
4.PC-Tools/build2/studio/Release/studio.exe

# 图片/GIF -> .ebanim 帧流
4.PC-Tools/build/frame-converter/Release/ebconvert.exe in.gif -o out.ebanim --preview first.bmp
#   选项: --bg R,G,B 底色（contain 留边填充） / --cover 满屏裁剪
#   支持: PNG / JPG / BMP / TIFF / GIF(动画，含局部帧、透明、disposal 0-3)
```

### `.ebanim` 格式（小端二进制）

```
[8B  magic "EBANIM1\0"]
[4B  frameCount]
[2B  width=240][2B height=240]
frameCount × { [2B durationMs][172800B RGB888] }
```

未压缩（30fps ≈ 5.2 MB/s），供 Studio / 真机帧流离线播放；如需长时间动画建议后续加 RLE。

### 已知问题

* 模拟器 `SyncTask` 中 `enabled/setPoints` 写入与舵机线程读已加 `dataMutex` 保护；
  若扩展模拟器，注意新增共享状态同样要加锁。
* WIC 不通过元数据查询接口暴露 GIF 的 GCE（本机实测 `/grct/*` 返回
  `WINCODEC_ERR_PROPERTYNOTFOUND`），ebconvert 因此**直接解析文件内 GCE 块**获取
  delay/disposal/透明索引，不依赖 WIC 元数据。
