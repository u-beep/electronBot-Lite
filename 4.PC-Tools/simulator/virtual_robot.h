#ifndef EB_VIRTUAL_ROBOT_H
#define EB_VIRTUAL_ROBOT_H

// ============================================================================
// VirtualElectronBot - ElectronBot 桌面机器人纯软件模拟器
//
// 1:1 复刻 ElectronBot-fw 固件行为（UserApp/main.cpp + Bsp/robot.cpp）：
//   * Connect/Sync/SetImageSrc/SetJointAngles 接口与 2.SDK 的
//     ElectronLowLevel 完全同签名 —— 上层代码零改动迁移到真机。
//   * Sync() 内部按固件协议执行：上行 32B 关节反馈 -> 下行 4 段
//     (84 x 512B + 224B) -> 模拟 SPI DMA 分段刷入虚拟显存。
//   * 6 个关节用二阶弹簧-阻尼模型模拟舵机 PID 闭环跟随（独立 100Hz 线程）。
//   * 关节范围取自固件 Bsp/robot.h 的 modelAngelMin/Max。
//
// 用法：把 sample.cpp 里的 "ElectronLowLevel robot;" 换成
//       "VirtualElectronBot robot;" 即可无硬件运行全部上层逻辑。
// ============================================================================

#include <cstdint>
#include <cstring>
#include <cmath>
#include <thread>
#include <mutex>
#include <atomic>

#include "eb_joints.h"

#define EB_SCREEN_WIDTH  240
#define EB_SCREEN_HEIGHT 240
#define EB_FRAME_SIZE    (EB_SCREEN_WIDTH * EB_SCREEN_HEIGHT * 3) // 172800 B

class VirtualElectronBot
{
public:
    VirtualElectronBot() = default;
    ~VirtualElectronBot();

    // ---- ElectronLowLevel 兼容接口 -------------------------------------
    bool Connect();      // 虚拟设备：永远成功
    bool Disconnect();
    bool Sync();         // 异步：模拟一次完整 PC<->MCU 帧同步

    void SetImageSrc(const uint8_t* _rgb888);                     // 240x240 RGB888
    void SetExtraData(uint8_t* _data, uint32_t _len = 32);
    void SetJointAngles(float _j1, float _j2, float _j3,
                        float _j4, float _j5, float _j6,
                        bool _enable = false);
    void GetJointAngles(float* _jointAngles);                     // 舵机反馈角

    int  USB_VID = 0x1001;
    int  USB_PID = 0x8023;
    bool isConnected = false;
    uint32_t timeStamp = 0;

    // ---- 模拟器专属（预览窗口 / Studio 使用） ---------------------------
    void GetVirtualScreen(uint8_t* _outRgb888);   // 当前虚拟显存快照
    void GetServoAngles(float* _out6);            // 舵机实际角度（模型角）
    void GetJointSetPoints(float* _out6);         // PC 下发的设定角度
    bool IsServoEnabled() const { return enabled.load(); }

    // 关节范围/名称：由 eb_joints.h 统一定义（与固件 Bsp/robot.h 一致），
    // 这里以引用成员转发，保持旧代码 VirtualElectronBot::JOINT_MIN 可用。
    static constexpr const float (&JOINT_MIN)[6] = ebjoints::JOINT_MIN;
    static constexpr const float (&JOINT_MAX)[6] = ebjoints::JOINT_MAX;
    static constexpr const char* const (&JOINT_NAME)[6] = ebjoints::JOINT_NAME;

private:
    void SyncTask();              // 模拟固件主循环一帧
    void ServoThread();           // 100Hz 舵机动力学
    static float Clamp(float v, float lo, float hi);

    // 模拟固件 UsbBuffer_t（rxData[2][43232] + ping-pong）
    uint8_t rxData[2][60 * 240 * 3 + 32] = {};
    uint8_t extraDataTx[32] = {};
    uint8_t extraDataRx[32] = {};      // 上行反馈缓冲（1B使能 + 6xfloat）
    uint8_t pingPongWriteIndex = 0;
    volatile uint32_t receivedPacketLen = 0;

    uint8_t screen[EB_FRAME_SIZE] = {};          // 虚拟 GC9A01 显存
    uint8_t frameTx[2][EB_FRAME_SIZE] = {};      // SDK 侧发送双缓冲
    uint8_t extraTx[2][32] = {};

    float setPoints[6] = {0, 0, 0, 0, 0, 0};     // 关节设定值（模型角）
    float servoPos[6] = {0, 0, 0, 0, 0, 0};      // 舵机当前角
    float servoVel[6] = {0, 0, 0, 0, 0, 0};      // 角速度
    std::atomic<bool> enabled{false};

    std::thread syncTaskHandle;
    std::thread servoThreadHandle;
    std::atomic<bool> servoRunning{false};
    std::mutex screenMutex;   // 保护 screen 显存
    std::mutex dataMutex;     // 保护 setPoints / extraRx
};

#endif // EB_VIRTUAL_ROBOT_H
