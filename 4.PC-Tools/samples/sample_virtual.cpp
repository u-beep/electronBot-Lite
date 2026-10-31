// ============================================================================
// sample_virtual - 2.SDK/sample.cpp 的无硬件版本
//
// 与原版 sample.cpp 的差异仅有两处：
//   1. ElectronLowLevel robot;  ->  VirtualElectronBot robot;
//   2. 附加打开预览窗口（真实机器人时就是那块圆屏本身）
//
// 其余逻辑（彩虹帧 + 正弦关节 + 反馈打印）与原版逐行一致 ——
// 验证上层代码到真机的零成本迁移。
//
// 用法：
//   sample_virtual            彩虹动画模式（对应原版 sample）
//   sample_virtual face       表情动画模式（复用表情库）
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <thread>
#include <conio.h>

#include "../simulator/virtual_robot.h"
#include "../simulator/win32_preview.h"
#include "../emoji-studio/expression.h"
#include "../emoji-studio/canvas.h"
#include "../emoji-studio/ebanim.h"

// 与 2.SDK/sample.cpp 相同的彩虹帧生成器
static void MakeRainbowFrame(uint8_t* _buffer, float _t)
{
    const float PI2 = 6.28318f;
    for (int y = 0; y < EB_SCREEN_HEIGHT; y++)
    {
        for (int x = 0; x < EB_SCREEN_WIDTH; x++)
        {
            float phase = (x + y + _t * 30.0f) * 0.03f;
            _buffer[0] = (uint8_t) (128 + 127 * sinf(phase));
            _buffer[1] = (uint8_t) (128 + 127 * sinf(phase + PI2 / 3));
            _buffer[2] = (uint8_t) (128 + 127 * sinf(phase + PI2 * 2 / 3));
            _buffer += 3;
        }
    }
}

int main(int argc, char** argv)
{
    const bool faceMode = argc > 1 && strcmp(argv[1], "face") == 0;

    // 自动退出秒数（可选，供自动化测试）：sample_virtual [face] <seconds>
    double autoExitSec = -1.0;
    for (int i = 1; i < argc; i++)
        if (atof(argv[i]) > 0.5)
            autoExitSec = atof(argv[i]);

    VirtualElectronBot robot;
    if (robot.Connect())
        printf("[virtual] Robot connected! (VID=0x%04X PID=0x%04X)\n", robot.USB_VID, robot.USB_PID);
    else
    {
        printf("Connect failed!\n");
        return 0;
    }

    RobotPreviewWindow::Open(&robot,
        faceMode ? "ElectronBot Virtual Screen [face]" : "ElectronBot Virtual Screen [rainbow]");

    static uint8_t frame[EB_FRAME_SIZE];
    float jointAngles[6];
    ExpressionRenderer expr;
    Canvas canvas;

    // face 模式下顺带演示 .ebanim 导出（30fps x 3s 的 Idle 表情）
    EbAnim exported;
    if (faceMode)
    {
        exported = EbAnim::Capture(expr, ExpressionRenderer::Builtin()[0], 90, 30);
        const std::string path = "idle_face.ebanim";
        if (exported.Save(path))
            printf("[export] %zu frames -> %s (%.1fs, %uKB)\n",
                   exported.frames.size(), path.c_str(),
                   exported.TotalDurationMs() / 1000.0f,
                   (uint32_t) (exported.frames.size() * (240 * 240 * 3 + 2) / 1024));
    }

    uint32_t frameCount = 0;
    printf(faceMode ? "Face mode. " : "Rainbow mode. ");
    printf("Press any key to exit...\n");
    const auto t0 = std::chrono::steady_clock::now();

    while (!_kbhit())
    {
        if (autoExitSec > 0 &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()
                > autoExitSec)
            break;

        const float t = frameCount * 0.033f;

        // 1. 生成画面
        if (faceMode)
        {
            expr.RenderFrame(ExpressionRenderer::Builtin()[frameCount / 150 % 5],
                             t * 1000.0f, canvas);
            canvas.BlitTo(frame);
        }
        else
        {
            MakeRainbowFrame(frame, t);
        }
        robot.SetImageSrc(frame);

        // 2. 与原版相同的关节设定（模型角，度）
        robot.SetJointAngles(
            15.0f * sinf(t * 0.5f),               // j1 head
            15.0f + 10.0f * sinf(t),              // j2 left arm roll
            80.0f + 60.0f * sinf(t * 0.7f),       // j3 left arm pitch
            15.0f + 10.0f * sinf(t + 1.0f),       // j4 right arm roll
            80.0f + 60.0f * sinf(t * 0.7f + 1.0f),// j5 right arm pitch
            45.0f * sinf(t * 0.3f),               // j6 body
            true);

        // 3. 同步一帧（虚拟协议往返）
        robot.Sync();

        // 4. 读取反馈（模拟舵机 PID 闭环后的实际角度）
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
        robot.GetJointAngles(jointAngles);
        if (frameCount % 30 == 0)
            printf("[frame %u] feedback: %.1f, %.1f, %.1f, %.1f, %.1f, %.1f\n",
                   frameCount,
                   jointAngles[0], jointAngles[1], jointAngles[2],
                   jointAngles[3], jointAngles[4], jointAngles[5]);

        frameCount++;
    }

    // 退出前失能舵机（与原版一致）
    robot.SetJointAngles(0, 0, 0, 0, 0, 0, false);
    robot.Sync();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    robot.Disconnect();
    printf("Robot Disconnected.\n");
    return 0;
}
