#ifndef EB_JOINTS_H
#define EB_JOINTS_H

// 关节常量：与固件 1.Firmware/ElectronBot-fw/Bsp/robot.h 的
// modelAngelMin / modelAngelMax 保持一致。
// 模拟器与真机后端共用（eb_backend.h 门面层向上转发）。

namespace ebjoints
{
    // 关节模型角范围：头 / 左臂roll / 左臂pitch / 右臂roll / 右臂pitch / 腰
    constexpr float JOINT_MIN[6] = { -15, 0, -20, 0, -20, -90 };
    constexpr float JOINT_MAX[6] = {  15, 30, 180, 30, 180, 90 };
    constexpr const char* JOINT_NAME[6] = {
        "Head", "L-Arm Roll", "L-Arm Pitch", "R-Arm Roll", "R-Arm Pitch", "Body" };
}

#endif // EB_JOINTS_H
