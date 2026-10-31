#include <cmath>
#include <cstdio>
#include <chrono>
#include <thread>
#include <conio.h>
#include "electron_low_level.h"

// Generate an animated rainbow pattern into a 240x240 RGB888 buffer.
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


int main()
{
    ElectronLowLevel robot;

    if (robot.Connect())
        printf("Robot connected!\n");
    else
    {
        printf("Connect failed! Check driver & firmware.\n");
        getchar();
        return 0;
    }

    static uint8_t frame[EB_FRAME_SIZE];
    float jointAngles[6];

    uint32_t frameCount = 0;

    while (!_kbhit())
    {
        float t = frameCount * 0.033f;

        // 1. Prepare screen image (animated rainbow)
        MakeRainbowFrame(frame, t);
        robot.SetImageSrc(frame);

        // 2. Prepare joint set-points (model angles, in degrees)
        robot.SetJointAngles(
            15.0f * sinf(t * 0.5f),               // j1 head
            15.0f + 10.0f * sinf(t),              // j2 left arm roll
            80.0f + 60.0f * sinf(t * 0.7f),       // j3 left arm pitch
            15.0f + 10.0f * sinf(t + 1.0f),       // j4 right arm roll
            80.0f + 60.0f * sinf(t * 0.7f + 1.0f),// j5 right arm pitch
            45.0f * sinf(t * 0.3f),               // j6 body
            true);

        // 3. Send one frame: image + joints down, joint feedback up
        robot.Sync();

        // 4. Read joint feedback
        robot.GetJointAngles(jointAngles);
        if (frameCount % 30 == 0)
            printf("[frame %u] feedback: %.1f, %.1f, %.1f, %.1f, %.1f, %.1f\n",
                   frameCount,
                   jointAngles[0], jointAngles[1], jointAngles[2],
                   jointAngles[3], jointAngles[4], jointAngles[5]);

        frameCount++;
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30fps
    }

    // Disable servos before leaving
    robot.SetJointAngles(0, 0, 0, 0, 0, 0, false);
    robot.Sync();

    robot.Disconnect();
    printf("Robot Disconnected.\n");

    return 0;
}
