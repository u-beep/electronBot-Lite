#ifndef EB_WIN32_PREVIEW_H
#define EB_WIN32_PREVIEW_H

// 独立线程运行的 Win32 预览窗口：左侧虚拟 GC9A01 圆屏（圆形剪裁），
// 右侧 6 关节实时仪表（设定值游标 + 舵机实际角度指针）。
// 控制台程序 sample_virtual 与 GUI 程序 studio 均可使用。

#include "virtual_robot.h"

class RobotPreviewWindow
{
public:
    // 创建并立即返回（窗口在后台线程运行，~30fps 刷新）
    static void Open(VirtualElectronBot* _robot, const char* _title = "ElectronBot Virtual Screen");
};

#endif // EB_WIN32_PREVIEW_H
