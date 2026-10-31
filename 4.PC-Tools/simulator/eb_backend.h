#ifndef EB_BACKEND_H
#define EB_BACKEND_H

// ============================================================================
// RobotBackend - Studio 的机器人后端门面（编译期二选一）
//
//   默认          EB_WITH_REAL_SDK 未定义 -> VirtualElectronBot（无硬件）
//   EB_WITH_REAL_SDK 定义                -> ElectronLowLevel（2.SDK 真机链路）
//
// 上位机代码只认 RobotBackend，切后端不需要改一行 UI 逻辑。
// 真机模式差异说明：
//   * GetServoAngles  = 真机 GetJointAngles（USB 上行的舵机反馈角）
//   * GetJointSetPoints = 本地缓存（SetJointAngles 的设定值，真机不回读）
//   * GetVirtualScreen  = 最近一次 SetImageSrc 的画面（真机屏无法回读，
//                         Studio 预览用它代替虚拟显存快照）
//   * 运行期需要 USBInterface.dll 与 3.Driver 驱动，且机器人已连接
// ============================================================================

#include "eb_joints.h"
#include <cstdint>
#include <cstring>

// 预处理器条件编译：编译时二选一，另一份代码完全不参与编译
#if defined(EB_WITH_REAL_SDK)
    #include "electron_low_level.h"   // -I 指向 2.SDK
    #define EB_BACKEND_MODE_NAME "Real(USB)"    // 状态栏显示的后端名
#else
    #include "virtual_robot.h"
    #define EB_BACKEND_MODE_NAME "Virtual"      // 状态栏显示的后端名
#endif

class RobotBackend
{
public:
    // 关节常量转发（UI 直接使用 RobotBackend::JOINT_*）
    static constexpr const float (&JOINT_MIN)[6] = ebjoints::JOINT_MIN;
    static constexpr const float (&JOINT_MAX)[6] = ebjoints::JOINT_MAX;
    static constexpr const char* const (&JOINT_NAME)[6] = ebjoints::JOINT_NAME;

    // 下面每个函数都只是"转发"给内部的 impl 对象（门面模式）：
    // UI 调 RobotBackend::Connect()，实际干活的是模拟器或真机类
    bool Connect()
    {
        timeStamp = 0;
        return impl.Connect();
    }

    bool Disconnect()
    {
        return impl.Disconnect();
    }

    bool Sync()
    {
        const bool ok = impl.Sync();
        timeStamp = impl.timeStamp;   // 同步内部帧号，状态栏要显示
        return ok;
    }

    void SetImageSrc(const uint8_t* _rgb888)
    {
        impl.SetImageSrc(_rgb888);
#if defined(EB_WITH_REAL_SDK)
        memcpy(lastFrame, _rgb888, EB_FRAME_SIZE);
        // 真机屏无法回读，存一份最新画面供 studio 预览
#endif
    }

    void SetJointAngles(float _j1, float _j2, float _j3,
                        float _j4, float _j5, float _j6, bool _enable)
    {
        impl.SetJointAngles(_j1, _j2, _j3, _j4, _j5, _j6, _enable);
        const float j[6] = { _j1, _j2, _j3, _j4, _j5, _j6 };
        memcpy(cachedSetPoints, j, sizeof(j));
        // 真机不回读设定值，本地缓存一份给 UI 显示
    }

    void GetServoAngles(float* _out6)
    {
#if defined(EB_WITH_REAL_SDK)
        impl.GetJointAngles(_out6);          // USB 上行的舵机反馈
#else
        impl.GetServoAngles(_out6);
#endif
    }

    void GetJointSetPoints(float* _out6)
    {
        memcpy(_out6, cachedSetPoints, sizeof(cachedSetPoints));
    }

    void GetVirtualScreen(uint8_t* _outRgb888)
    {
#if defined(EB_WITH_REAL_SDK)
        memcpy(_outRgb888, lastFrame, EB_FRAME_SIZE);
#else
        impl.GetVirtualScreen(_outRgb888);
#endif
    }

    uint32_t timeStamp = 0;

private:
#if defined(EB_WITH_REAL_SDK)
    ElectronLowLevel impl;
    float cachedSetPoints[6] = {};
    uint8_t lastFrame[EB_FRAME_SIZE] = {};
#else
    VirtualElectronBot impl;
    float cachedSetPoints[6] = {};
#endif
};

#endif // EB_BACKEND_H
