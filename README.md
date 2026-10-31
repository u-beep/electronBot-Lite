# ElectronBot-Lite

ElectronBot 桌面机器人的**精简版**工程，只保留实现本项目的核心功能和完整链路，去除了原仓库中的硬件设计文件、CAD 模型、Unity 上位机、OpenCV 播放器、扩展工具等所有非核心内容。主控固件与舵机固件与原版完全一致，未做任何修改。

> 原项目：[peng-zhihui/ElectronBot](https://github.com/peng-zhihui/ElectronBot)（MIT License）

## 1. 核心链路

```
┌─────────────────────┐  USB 2.0 HS Bulk (CDC)   ┌──────────────────────────┐
│  PC (2.SDK)          │ ◄──────────────────────► │  STM32F405 主控固件       │
│  sample.exe          │  VID=0x1001 PID=0x8023   │  (1.Firmware/ElectronBot-fw)│
│  └ ElectronBotSDK-   │                          │                          │
│     Lite + USBIf.dll │                          │  ├── SPI ──► GC9A01 圆屏 │
└─────────────────────┘                          │  │    (240x240 RGB888)   │
                                                 │  └── I2C ⇄ 6x 魔改舵机   │
       3.Driver/BotDriver (USB驱动,必装)          │        │                │
                                                 └────────┼────────────────┘
                                                          ▼
                                              ┌──────────────────────────┐
                                              │ STM32F042 舵机固件        │
                                              │ (1.Firmware/ServoDrive-fw)│
                                              │ ADC电位器采样 → PID闭环    │
                                              │ → PWM 驱动电机             │
                                              └──────────────────────────┘
```

一帧同步的数据流（SDK `Sync()` ↔ 固件主循环，共 4 段）：

1. **MCU → PC**：32 字节额外数据（6 个关节当前角度，float x6）
2. **PC → MCU**：84 包 × 512 字节 + 1 包 224 字节（其中 192 字节帧尾 + 32 字节额外数据：使能位 + 6 个关节角度设定值），每段下行合计 43232 字节 = 43200 字节图像（60 行 × 240 × 3）+ 32 字节额外数据
3. 固件收到一段后立即通过 SPI DMA 刷屏，循环 4 段完成一整帧 240×240 图像

## 2. 目录说明

| 目录 | 内容 |
|---|---|
| `1.Firmware/ElectronBot-fw` | STM32F405 主控固件：USB-CDC 收发、GC9A01 屏幕驱动、I2C 舵机轮询 |
| `1.Firmware/ServoDrive-fw` | STM32F042 舵机固件：I2C 从机协议解析、电位器 ADC 采样、PID 闭环、PWM 输出 |
| `2.SDK` | PC 端精简 SDK（C++17，无 OpenCV 依赖）+ 演示程序 sample |
| `3.Driver/BotDriver` | 机器人 USB 驱动（连接电脑后必装） |
| `4.PC-Tools` | 纯软件工具链：虚拟机器人模拟器 / 表情工坊 / 上位机 Studio / 帧流转换器 ebconvert（见其 CMakeLists 与 `5.Docs/protocol.md`） |
| `5.Docs` | 协议文档与架构笔记（USB 分段协议、I2C 舵机指令、带宽计算） |

## 3. 通信协议

### 3.1 USB（PC ⇄ 主控）

- 设备：自定义 CDC 设备，`VID=0x1001`，`PID=0x8023`，Bulk 端点 EP1
- 下行：一帧图像 172800 字节（240×240×3，RGB888）+ 每段 32 字节关节数据
- 上行：每段 32 字节关节反馈
- 关节数据格式（32 字节）：`[0]` 使能标志，`[1..24]` 6 个 float（小端）角度值（模型角度，单位度）

### 3.2 I2C（主控 ⇄ 舵机）

主机先发 5 字节 `[指令, 4字节参数]`，再收 5 字节 `[指令回显, 4字节数据(通常为当前角度float)]`。舵机作为从机监听自身 ID 和 0 号广播地址。

| 指令 | 含义 |
|---|---|
| `0x01` | 设置角度 |
| `0x02` | 设置速度 |
| `0x03` | 设置力矩 |
| `0x11` | 读取角度 |
| `0x12` | 读取速度 |
| `0x21` | 设置舵机 ID |
| `0x22/0x23/0x24/0x25` | 设置 Kp / Ki / Kv / Kd |
| `0x26` | 设置力矩限制 |
| `0x27` | 设置上电初始角度 |
| `0xFF` | 使能 / 失能 |

关节与舵机 ID 对应（见 `Bsp/robot.h`）：1-头部(id=2)、2-左臂roll(id=4)、3-左臂pitch(id=6)、4-右臂roll(id=8)、5-右臂pitch(id=10)、6-腰部(id=12)。

> 注意：主控上电有 2s 延时等待所有舵机就绪；调试时建议逐个舵机验证，避免总线阻塞。

## 4. 编译与使用

### 4.1 舵机固件（ServoDrive-fw，STM32F042）

- 工具链：`arm-none-eabi-gcc`（可用 CLion + CMake，或用 `.ioc` 重新生成 Keil / STM32CubeIDE 工程）
- 每块舵机板烧录后需要通过 `0x21` 指令设置各自的 ID（见上表）
- 烧录器：ST-Link / J-Link（驱动板仅留 SH1.0 触点）

### 4.2 主控固件（ElectronBot-fw，STM32F405）

- 工具链同上，工程根目录含 `CMakeLists.txt` 与 `ElectronBot-fw.ioc`
- 若用 CubeMX 重新生成工程，请先备份 `USB_DEVICE/` 与 `UserApp/usbd_cdc_if.cpp`，生成后替换回来（CubeMX 会覆盖 USB 收发定制代码）

### 4.3 PC SDK 与示例（2.SDK）

依赖：CMake 3.15+、MSVC（x64）。`USBInterface.dll` 为 x64 库。

```bash
cd 2.SDK
cmake -B build
cmake --build build --config Release
```

运行 `build/Release/sample.exe`：屏幕显示动态彩虹画面，6 个关节做正弦摆动，控制台每秒打印一次关节反馈角度，按任意键退出。

### 4.4 驱动安装（3.Driver/BotDriver）

机器人通过 USB 连接电脑后，在设备管理器中为未知设备更新驱动，指向 `BotDriver` 目录（或直接运行 `installer_x64.exe`）。Windows 需禁用强制驱动签名（驱动未付费签名）。

## 5. 与原版工程的差异

| 内容 | 原版 | Lite |
|---|---|---|
| 主控固件 / 舵机固件 | ✅ | ✅ 原样保留 |
| LowLevel SDK | 依赖 OpenCV | ✅ 保留，去除 OpenCV（`SetImageSrc` 直接接收 RGB888 缓冲） |
| 演示程序 | Unity Studio + 视频播放器 | 纯代码生成画面的命令行 sample |
| USB 驱动 BotDriver | ✅ | ✅ 保留 |
| PCB / CAD / 文档 / 测试工程 / AHK 工具 | ✅ | ❌ 移除 |
