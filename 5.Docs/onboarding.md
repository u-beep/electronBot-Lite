# ElectronBot-Lite 项目导读（Onboarding Guide）

> 目标：不碰硬件的前提下，从零到吃透整个系统。全程约 2-3 天，
> 每个阶段都有"动手实验"和"验收标准"，做完再进入下一阶段。

---

## 0. 项目全景（先建立心智模型）

这个项目的"大脑"在 PC 端，身体才是硬件：

```
┌───────────────────────── PC 端（纯软件，无硬件可玩）─────────────────────────┐
│                                                                              │
│  sample.cpp ──► ElectronLowLevel ──► USBInterface.dll ──► 真机 (2.SDK + USB)  │
│      │                                                                       │
│      │  把上面这条链里的类换成同签名的 Mock，整条上层逻辑零改动：              │
│      ▼                                                                       │
│  sample.cpp ──► VirtualElectronBot（4.PC-Tools/simulator）                   │
│                    ├─► 虚拟 GC9A01 圆屏（240x240 RGB888 显存）                │
│                    └─► 6 关节弹簧-阻尼舵机仿真（100Hz 独立线程）              │
│                                                                              │
│  内容生产：emoji-studio（表情库/Canvas/.ebanim）→ ebconvert（图/GIF→帧流）    │
│  上位机  ：studio（圆屏预览 + Robot View + 滑块 + 表情按钮）                  │
└──────────────────────────────────────────────────────────────────────────────┘
              │ USB CDC（4 段 × 43232B + 32B 关节数据，41.5Mbps → 必须 USB HS）
┌─────────────▼────────────── 硬件端 ──────────────────────────────────────────┐
│  ElectronBot-fw（STM32H750）                                                 │
│    UserApp/main.cpp ─ USB 收帧 → SPI DMA 刷屏（ping-pong 双缓冲）            │
│    Bsp/robot.cpp    ─ 关节角度 → I2C 总线下发舵机                            │
│  ServoDrive-fw（舵机协 MCU）─ I2C 从机，13 条指令（0x01 角度/0xFF 使能/…）   │
└──────────────────────────────────────────────────────────────────────────────┘
```

目录分层（数字即依赖层级，上层依赖下层）：

| 目录 | 角色 | 你现阶段要花的时间 |
|---|---|---|
| `1.Firmware/` | 两块 MCU 的固件 | 阶段 4 才碰 |
| `2.SDK/` | PC→硬件的唯一通道（85 行 sample 就是用户代码范例） | **最多** |
| `3.Driver/` | Windows USB 驱动（装一次就忘） | 5 分钟 |
| `4.PC-Tools/` | 模拟器 + 表情工坊 + 上位机 + 转换器 | **其次** |
| `5.Docs/` | 协议文档、本文档 | 随用随查 |

---

## 阶段 1：跑起来（半天）→ 目标：看到"活的机器人"

**读**：`README.md`（112 行）+ `2.SDK/sample.cpp`（85 行，唯一必读入门代码）

**做**（按顺序，每条 1 分钟内）：

```bat
cd D:\C\code\ElectronBot-Lite\4.PC-Tools\build\studio\Release
studio.exe                       :: GUI：拖滑块、点表情、勾 Servo Enable

cd ..\..\Release
sample_virtual.exe               :: 终端：彩虹动画 + 关节反馈流
sample_virtual.exe face          :: 表情模式，导出 idle_face.ebanim

cd ..\frame-converter\Release
ebconvert.exe 任意图片.png --preview out.bmp --bg 20,20,20
ebconvert.exe 任意动画.gif -o out.ebanim
```

**实验 1.1**：studio 里把 L-Arm Pitch 拉到 180 再拉回 0，观察 Robot View 手臂画弧线。
**实验 1.2**：取消勾选 Servo Enable 再拖滑块——反馈值为什么归 0？
（答：舵机未使能无力矩，模拟器 1:1 复刻真机失电行为）

**验收**：能解释 studio 每个控件对应模拟器的哪个接口调用。

---

## 阶段 2：吃透 SDK 应用层（半天）→ 目标：会用 5 个 API 写自己的动作

整个应用层就 5 个函数，定义在 `2.SDK/electron_low_level.h`（55 行）：

| API | 作用 |
|---|---|
| `Connect()` | 打开 USB CDC（虚拟版永远成功） |
| `SetImageSrc(uint8_t*)` | 喂一帧 240×240 RGB888（172800 字节） |
| `SetJointAngles(j1..j6, enable)` | 设 6 关节角度 + 使能 |
| `Sync()` | 完成一次 PC↔MCU 帧同步（图像下行 + 关节反馈上行） |
| `GetJointAngles()` | 读关节反馈角 |

**读**：`2.SDK/electron_low_level.cpp`（173 行）——重点看 `Sync()` 里图像怎么被切成
4 段 × (84×512 + 224) 字节下发、32 字节 extraData 怎么携带关节角。

**做**：改 `2.SDK/sample.cpp` 的正弦摆动参数（频率/幅度/相位差），用模拟器跑
`sample_virtual.exe` 看效果——改的是 sample.cpp 逻辑，跑的是模拟器，这正是
"无硬件开发"的工作流。

**验收**：能默写 Sync() 一次同步的数据流向图（不含具体字节）。

---

## 阶段 3：深入 PC-Tools 三件套（1 天）→ 目标：能给项目加功能

按依赖顺序读（每块都 <600 行）：

| 顺序 | 模块 | 文件（行数） | 看什么 |
|---|---|---|---|
| 1 | 公共层 | `common/eb_screen.h`（23） | 唯一分辨率定义处 |
| 2 | 模拟器 | `simulator/virtual_robot.h/.cpp`（92+232） | Sync 协议仿真、舵机 100Hz 线程、ping-pong 双缓冲 |
| 3 | 模拟器 | `simulator/eb_backend.h`（109） | Virtual/Real 后端切换门面（真机迁移的关键设计） |
| 4 | 表情 | `emoji-studio/canvas.cpp`（121） | 抗锯齿圆/线怎么画的 |
| 5 | 表情 | `emoji-studio/expression.cpp`（149） | 6 眼型 × 5 嘴型参数化 + 5 组关键帧动画 |
| 6 | 表情 | `emoji-studio/ebanim.cpp`（90） | .ebanim 二进制格式 Save/Load |
| 7 | 上位机 | `studio/main.cpp`（457） | Win32 消息循环、双缓冲绘制、Robot View |
| 8 | 转换器 | `frame-converter/ebconvert.cpp`（594） | WIC 解码、GIF 局部帧/disposal/透明合成 |

**做（选一个，难度递增）**：

- 简单：在 `expression.cpp` 加一组新表情（如"星星眼"），studio 重编译就能点
- 中等：给 `ebconvert` 加 `--fps` 参数覆盖 GIF 原始延迟
- 较难：给 studio 加动作时间轴（录制滑块序列 → 回放），数据结构可直接用 `EbAnim` 思路

**验收**：能画出 studio 一帧的绘制流水线（谁触发 → 画哪几块 → 为什么不闪）。

---

## 阶段 4：碰硬件与固件（拿到板子之后）

**先读文档**：`5.Docs/protocol.md`（181 行）——USB 分段协议字段表、32B 关节结构、
I2C 舵机 13 条指令、双缓冲时序。这份文档的字节数都是从代码里核实过的。

**固件代码地图**（ElectronBot-fw）：

| 想了解 | 去看 |
|---|---|
| USB 收包 → 显存的完整链路 | `UserApp/main.cpp` 的 `#else` 分支主循环（当前被 `SCREEN_DEMO=1` 关闭） |
| 屏幕驱动 + SPI DMA | `Bsp/screen.cpp`（Screen 类） |
| 关节定义 / 角度范围 / ID 分配 | `Bsp/robot.h` 的 `JointStatus_t{ id, min, max, init }`（头=2/左臂=4,6/右臂=8,10/腰=12） |
| I2C 舵机指令收发 | `Bsp/robot.cpp` + 舵机侧 `ServoDrive-fw/UserApp/main.cpp`（238 行） |

**上手真机的检查单**：

1. 装 `3.Driver/BotDriver`
2. ⚠️ 把 `UserApp/main.cpp` 的 `#define SCREEN_DEMO 1` 改回 `0`（当前是屏幕自测模式）
3. studio 用真机后端：`cmake -S 4.PC-Tools -B build3 -DEB_WITH_REAL_SDK=ON`
4. 先只开 Head 一个关节测试，确认 I2C 通了再全开

---

## 日常命令速查

```bat
:: 构建（必须清 proxy，否则 MSBuild 报 MSB6001）
set HTTP_PROXY=& set http_proxy=& set HTTPS_PROXY=& set https_proxy=
cmake --build D:\C\code\ElectronBot-Lite\4.PC-Tools\build --config Release

:: 真机后端构建
cmake -S 4.PC-Tools -B 4.PC-Tools\build3 -DEB_WITH_REAL_SDK=ON
cmake --build 4.PC-Tools\build3 --config Release --target studio

:: 提交推送
cd D:\C\code\ElectronBot-Lite
git add -A && git commit -m "..." && git push
```

---

## 一句话总结每层的设计意图

- **SDK**：把"USB 协议"抽象成 5 个函数，上层永远不知道字节的存在
- **模拟器**：和 SDK 同签名，让"有没有硬件"对上层代码完全透明
- **emoji-studio**：把"像素"抽象成"表情参数"，内容生产不碰显存
- **studio**：把三者拼成可视化工作台，Robot View 让关节动作可见
- **固件**：屏幕走 SPI DMA 双缓冲（带宽吃满），关节走慢速 I2C——快慢分离
