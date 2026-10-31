#include <cmath>
#include "common_inc.h"
#include "screen.h"
#include "robot.h"

// ============================================================================
// SCREEN_DEMO: Route-B phase-1 standalone screen test.
// Set to 1 to run a local rainbow animation (same as 2.SDK/sample.cpp) with
// NO USB host and NO I2C servos connected - only a GC9A01 screen on SPI1.
// Set to 0 to restore the original USB-sync main loop.
// ============================================================================
#define SCREEN_DEMO 1


Robot electron(&hspi1, &hi2c1);
float jointSetPoints[6];
bool isEnabled = false;
/* Thread Definitions -----------------------------------------------------*/


/* Timer Callbacks -------------------------------------------------------*/


/* Default Entry -------------------------------------------------------*/

#if SCREEN_DEMO

// Mirrors 2.SDK/sample.cpp MakeRainbowFrame(): the whole 240x240 RGB888 frame
// is generated and streamed in 4 segments of 60 rows (60*240*3 = 43200 bytes),
// exactly like the original PC sync path, reusing one USB ping-pong buffer.
void Main(void)
{
    HAL_Delay(2000); // allow power rails to settle

    // Dupont wires are not reliable at the PCB default SPI clock (42 MHz,
    // APB2 84MHz / prescaler 2). Re-init SPI1 slower for wiring convenience:
    //   prescaler 4 -> 21 MHz, prescaler 8 -> 10.5 MHz (safest to start).
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    HAL_SPI_Init(&hspi1);

    electron.lcd->Init(Screen::DEGREE_0);
    electron.lcd->SetWindow(0, 239, 0, 239);

    // Reuse one ping-pong USB buffer as the 60-row segment buffer (43232 >= 43200).
    uint8_t* segBuf = electron.usbBuffer.rxData[0];
    const float PI2 = 6.28318f;
    uint32_t frameCount = 0;

    while (true)
    {
        float t = frameCount * 0.033f; // ~30fps like the PC demo

        for (int p = 0; p < 4; p++) // 4 x 60 rows = 240 rows
        {
            uint8_t* buf = segBuf;
            for (int y = 0; y < 60; y++)
            {
                int row = y + p * 60;
                for (int x = 0; x < 240; x++)
                {
                    float phase = (x + row + t * 30.0f) * 0.03f;
                    buf[0] = (uint8_t) (128 + 127 * sinf(phase));
                    buf[1] = (uint8_t) (128 + 127 * sinf(phase + PI2 / 3));
                    buf[2] = (uint8_t) (128 + 127 * sinf(phase + PI2 * 2 / 3));
                    buf += 3;
                }
            }
            while (electron.lcd->isBusy); // wait for previous SPI DMA
            electron.lcd->WriteFrameBuffer(segBuf, 60 * 240 * 3, p != 0);
        }

        frameCount++;
        HAL_Delay(33); // ~30fps
    }
}

#else // original USB-sync main loop

void Main(void)
{
    HAL_Delay(2000);
    electron.lcd->Init(Screen::DEGREE_0);
    electron.lcd->SetWindow(0, 239, 0, 239);


    electron.UpdateJointAngle(electron.joint[1], 0);
    electron.UpdateJointAngle(electron.joint[2], 0);
    electron.UpdateJointAngle(electron.joint[3], 0);
    electron.UpdateJointAngle(electron.joint[4], 0);
    electron.UpdateJointAngle(electron.joint[5], 0);
    electron.UpdateJointAngle(electron.joint[6], 0);

    float t = 0;

    while (true)
    {
#if 1
        for (int p = 0; p < 4; p++)
        {
            // send joint angles
            for (int j = 0; j < 6; j++)
                for (int i = 0; i < 4; i++)
                {
                    auto* b = (unsigned char*) &(electron.joint[j + 1].angle);
                    electron.usbBuffer.extraDataTx[j * 4 + i + 1] = *(b + i);
                }
            electron.SendUsbPacket(electron.usbBuffer.extraDataTx, 32);

            electron.ReceiveUsbPacketUntilSizeIs(224); // last packet is 224bytes

            // get joint angles
            uint8_t* ptr = electron.GetExtraDataRxPtr();
            if (isEnabled != (bool) ptr[0])
            {
                isEnabled = ptr[0];
                electron.SetJointEnable(electron.joint[1], isEnabled);
                electron.SetJointEnable(electron.joint[2], isEnabled);
                electron.SetJointEnable(electron.joint[3], isEnabled);
                electron.SetJointEnable(electron.joint[4], isEnabled);
                electron.SetJointEnable(electron.joint[5], isEnabled);
                electron.SetJointEnable(electron.joint[6], isEnabled);
            }
            for (int j = 0; j < 6; j++)
            {
                jointSetPoints[j] = *((float*) (ptr + 4 * j + 1));
            }

            while (electron.lcd->isBusy);
            if (p == 0)
                electron.lcd->WriteFrameBuffer(electron.GetLcdBufferPtr(),
                                               60 * 240 * 3);
            else
                electron.lcd->WriteFrameBuffer(electron.GetLcdBufferPtr(),
                                               60 * 240 * 3, true);
        }
        HAL_Delay(1);
#endif


        t += 0.01;

        electron.UpdateJointAngle(electron.joint[1], jointSetPoints[0]);
        electron.UpdateJointAngle(electron.joint[2], jointSetPoints[1]);
        electron.UpdateJointAngle(electron.joint[3], jointSetPoints[2]);
        electron.UpdateJointAngle(electron.joint[4], jointSetPoints[3]);
        electron.UpdateJointAngle(electron.joint[5], jointSetPoints[4]);
        electron.UpdateJointAngle(electron.joint[6], jointSetPoints[5]);

        HAL_Delay(1);

//      electron.UpdateJointAngle(electron.joint[ANY], 65 + 75 * std::sin(t));

        printf("%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
               jointSetPoints[0], jointSetPoints[1], jointSetPoints[2],
               jointSetPoints[3], jointSetPoints[4], jointSetPoints[5]);
    }
}


extern "C"
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef* hspi)
{
    electron.lcd->isBusy = false;
}

#endif // SCREEN_DEMO