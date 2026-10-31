// 专项调试：验证模拟器关节设定值解析与舵机跟随是否错位
#include "virtual_robot.h"
#include <windows.h>
#include <cstdio>

int main()
{
    VirtualElectronBot r;
    r.Connect();

    float sets[6], servos[6];
    // 恒定设定值：j1=0, j2=15, j3=80, j4=25, j5=130, j6=-45（各不相同便于辨认）
    for (int i = 0; i < 60; i++)   // ~2 秒
    {
        r.SetJointAngles(0, 15, 80, 25, 130, -45, true);
        r.Sync();
        Sleep(33);
        r.GetJointSetPoints(sets);
        r.GetServoAngles(servos);
        printf("[%2d] set:  %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f\n"
               "     servo:%6.2f %6.2f %6.2f %6.2f %6.2f %6.2f\n",
               i, sets[0], sets[1], sets[2], sets[3], sets[4], sets[5],
               servos[0], servos[1], servos[2], servos[3], servos[4], servos[5]);
        if (i >= 8) break;   // 打印前 9 帧足够
    }
    r.Disconnect();
    return 0;
}
