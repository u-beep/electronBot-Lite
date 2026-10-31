// ============================================================================
// 文件：sample.cpp —— SDK 演示程序（你的第一个机器人程序！）
//
// 功能：让机器人屏幕播放流动的彩虹，同时 6 个关节做正弦摆动。
//
// 新手导读——一个机器人程序的固定套路只有 5 步：
//   1. Connect()          连接机器人
//   2. SetImageSrc()      准备一帧画面
//   3. SetJointAngles()   设定 6 个关节角度
//   4. Sync()             发出去（画面+角度下行，关节反馈上行）
//   5. 循环 2-4，每秒约 30 次
//
// 想改成自己的动作？只改第 53-60 行的 6 个角度公式即可。
// ============================================================================

#include <cmath>       // sinf（浮点正弦函数）
#include <cstdio>      // printf
#include <chrono>      // std::chrono（时间工具）
#include <thread>      // std::this_thread（睡眠）
#include <conio.h>     // _kbhit（检测键盘按键，Windows 专用）
#include "electron_low_level.h"   // 机器人控制类（本 SDK 的核心）

// ---------------------------------------------------------------------------
// MakeRainbowFrame()：生成一帧"流动彩虹"画面
// 参数：_buffer = 像素写到这里（172800 字节）；_t = 时间（秒），控制彩带流动
// 原理：每个像素的红/绿/蓝三个分量用相位差 120 度的正弦波生成，
//       波峰错开 → 颜色沿对角线循环渐变 → 随时间平移 → 彩带流动
// ---------------------------------------------------------------------------
static void MakeRainbowFrame(uint8_t* _buffer, float _t)
{
    const float PI2 = 6.28318f;                       // 2π，正弦波的一个完整周期
    for (int y = 0; y < EB_SCREEN_HEIGHT; y++)        // 逐行扫描（共 240 行）
    {
        for (int x = 0; x < EB_SCREEN_WIDTH; x++)     // 行内逐像素（共 240 列）
        {
            // 相位 = 位置 + 时间。+_t*30 让彩带随时间流动，0.03 控制彩带宽度
            float phase = (x + y + _t * 30.0f) * 0.03f;
            // 128 + 127*sinf(...)：把 -1..1 的正弦映射到 0..255 的亮度
            // 三个分量相位差 120 度（2π/3），叠加起来就是彩虹色
            _buffer[0] = (uint8_t) (128 + 127 * sinf(phase));            // 红
            _buffer[1] = (uint8_t) (128 + 127 * sinf(phase + PI2 / 3));  // 绿
            _buffer[2] = (uint8_t) (128 + 127 * sinf(phase + PI2 * 2 / 3)); // 蓝
            _buffer += 3;   // 指针前进 3 字节 = 跳到下一个像素
        }
    }
}


int main()
{
    ElectronLowLevel robot;                       // 创建机器人控制对象

    // ---- 第 1 步：连接 ----
    if (robot.Connect())
        printf("Robot connected!\n");
    else
    {
        printf("Connect failed! Check driver & firmware.\n");
        getchar();                                // 停一下让用户看到错误信息
        return 0;
    }

    static uint8_t frame[EB_FRAME_SIZE];          // 一帧画面的像素缓冲（172800B）
    float jointAngles[6];                         // 存关节反馈角

    uint32_t frameCount = 0;                      // 帧计数器

    // ---- 主循环：按任意键退出 ----
    while (!_kbhit())
    {
        float t = frameCount * 0.033f;            // 当前时间（秒），0.033 ≈ 1/30

        // 1. 生成彩虹画面并喂给机器人
        MakeRainbowFrame(frame, t);
        robot.SetImageSrc(frame);

        // 2. 设定 6 个关节角度 —— 正弦摆动，各关节频率/相位不同 => 舞蹈感
        //    公式：中位角度 + 摆动幅度 * sinf(速度 * t + 相位差)
        robot.SetJointAngles(
            15.0f * sinf(t * 0.5f),               // j1 头：左右各 15 度慢摆
            15.0f + 10.0f * sinf(t),              // j2 左臂Roll：15±10 度
            80.0f + 60.0f * sinf(t * 0.7f),       // j3 左臂Pitch：80±60 大幅挥舞
            15.0f + 10.0f * sinf(t + 1.0f),       // j4 右臂Roll：比左臂晚 1 秒
            80.0f + 60.0f * sinf(t * 0.7f + 1.0f),// j5 右臂Pitch：同上
            45.0f * sinf(t * 0.3f),               // j6 腰：±45 度慢转
            true);                                // true = 舵机上电（会动）

        // 3. 执行一次帧同步（画面+角度下行，关节反馈上行，后台线程完成）
        robot.Sync();

        // 4. 读关节反馈角，每 30 帧（约 1 秒）打印一次
        robot.GetJointAngles(jointAngles);
        if (frameCount % 30 == 0)                 // % 是取余数：能被 30 整除时打印
            printf("[frame %u] feedback: %.1f, %.1f, %.1f, %.1f, %.1f, %.1f\n",
                   frameCount,
                   jointAngles[0], jointAngles[1], jointAngles[2],
                   jointAngles[3], jointAngles[4], jointAngles[5]);

        frameCount++;
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30fps
        // 睡 33 毫秒 ≈ 每秒 30 帧（人眼流畅的最低标准）
    }

    // ---- 收尾：先断电舵机再断开连接（保护舵机齿轮） ----
    robot.SetJointAngles(0, 0, 0, 0, 0, 0, false); // false = 舵机断电松弛
    robot.Sync();                                  // 把"断电"指令发出去

    robot.Disconnect();
    printf("Robot Disconnected.\n");

    return 0;
}
