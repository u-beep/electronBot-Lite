#include "virtual_robot.h"
#include <chrono>

// ---------------------------------------------------------------------------
// 关节动力学参数：二阶弹簧-阻尼，模拟舵机 PID 闭环的收敛速度和轻微惯性
// ---------------------------------------------------------------------------
static constexpr float SERVO_KS = 90.0f;    // 位置刚度（1/s^2）
static constexpr float SERVO_KD = 10.0f;    // 阻尼（1/s）
static constexpr float SERVO_NOISE = 0.15f; // 反馈噪声（度）


float VirtualElectronBot::Clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}


VirtualElectronBot::~VirtualElectronBot()
{
    Disconnect();
}


bool VirtualElectronBot::Connect()
{
    // 虚拟设备：模拟 USB_ScanDevice + USB_OpenDevice 成功
    isConnected = true;
    timeStamp = 0;

    // 启动 100Hz 舵机闭环线程（模拟固件 1kHz 主循环 + PID）
    if (!servoRunning.load())
    {
        servoRunning.store(true);
        servoThreadHandle = std::thread(&VirtualElectronBot::ServoThread, this);
    }
    return true;
}


bool VirtualElectronBot::Disconnect()
{
    if (syncTaskHandle.joinable())
        syncTaskHandle.join();

    if (servoRunning.load())
    {
        servoRunning.store(false);
        if (servoThreadHandle.joinable())
            servoThreadHandle.join();
    }

    if (isConnected)
    {
        isConnected = false;
        return true;
    }
    return false;
}


bool VirtualElectronBot::Sync()
{
    if (!isConnected)
        return false;

    // 与 SDK 相同的异步语义：join 上一帧，再起新线程
    if (syncTaskHandle.joinable())
        syncTaskHandle.join();
    syncTaskHandle = std::thread(&VirtualElectronBot::SyncTask, this);

    timeStamp++;
    return true;
}


void VirtualElectronBot::SetImageSrc(const uint8_t* _rgb888)
{
    memcpy(frameTx[pingPongWriteIndex], _rgb888, EB_FRAME_SIZE);
}


void VirtualElectronBot::SetExtraData(uint8_t* _data, uint32_t _len)
{
    if (_len <= 32)
        memcpy(extraTx[pingPongWriteIndex], _data, _len);
}


void VirtualElectronBot::SetJointAngles(float _j1, float _j2, float _j3,
                                        float _j4, float _j5, float _j6,
                                        bool _enable)
{
    float j[6] = { _j1, _j2, _j3, _j4, _j5, _j6 };
    uint8_t* dst = extraTx[pingPongWriteIndex];
    dst[0] = _enable ? 1 : 0;
    for (int jn = 0; jn < 6; jn++)
    {
        auto* b = (unsigned char*) &j[jn];
        for (int i = 0; i < 4; i++)
            dst[jn * 4 + i + 1] = *(b + i);
    }
}


void VirtualElectronBot::GetJointAngles(float* _jointAngles)
{
    std::lock_guard<std::mutex> lk(dataMutex);
    memcpy(_jointAngles, servoPos, sizeof(float) * 6);
}


// ---------------------------------------------------------------------------
// 模拟固件主循环一帧（对应 UserApp/main.cpp 的 for p in 0..3 循环体）
// ---------------------------------------------------------------------------
void VirtualElectronBot::SyncTask()
{
    const uint8_t index = pingPongWriteIndex;
    pingPongWriteIndex = pingPongWriteIndex == 0 ? 1 : 0;

    for (int p = 0; p < 4; p++)
    {
        // 1) MCU -> PC：32B 额外数据（使能 + 6 关节当前角度，小端 float）
        {
            std::lock_guard<std::mutex> lk(dataMutex);
            extraDataRx[0] = enabled.load() ? 1 : 0;
            for (int jn = 0; jn < 6; jn++)
            {
                auto* b = (unsigned char*) &servoPos[jn];
                for (int i = 0; i < 4; i++)
                    extraDataRx[jn * 4 + i + 1] = *(b + i);
            }
        }

        // 2) PC -> MCU：84 包 x 512B + 1 包 224B = 43200B（含 32B 额外数据）
        //    固件收到一段后立即通过 SPI DMA 刷屏（这里是把帧段拷入显存）
        const uint32_t segOffset = (uint32_t) p * (60 * 240 * 3);
        const uint8_t* segSrc = frameTx[index] + segOffset;
        uint8_t* segDst = rxData[p & 1];          // 模拟 ping-pong 接收缓冲
        memcpy(segDst, segSrc, 60 * 240 * 3);
        receivedPacketLen = 224;                  // 模拟"收到最后一段"事件

        // 模拟 SPI DMA 刷屏耗时（USB HS ~3.5ms/帧 -> 1ms/段）
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        {
            std::lock_guard<std::mutex> lk(screenMutex);
            memcpy(screen + segOffset, segDst, 60 * 240 * 3);
        }

        // 3) 段尾 192B 帧数据 + 32B 额外数据（使能 + 关节设定值）
        //    真实固件：帧尾数据已包含在 43200B 内，此处直接解析 extraDataTx
        const uint8_t* extra = extraTx[index];
        bool en = extra[0] != 0;
        float j[6];
        for (int jn = 0; jn < 6; jn++)
            j[jn] = *((float*) (extra + 4 * jn + 1));

        std::lock_guard<std::mutex> lk(dataMutex);
        enabled.store(en);
        for (int jn = 0; jn < 6; jn++)
            setPoints[jn] = Clamp(j[jn], JOINT_MIN[jn], JOINT_MAX[jn]);
    }
}


// ---------------------------------------------------------------------------
// 100Hz 舵机闭环：模拟 STM32F042 固件的 DCE PID 位置环
// ---------------------------------------------------------------------------
void VirtualElectronBot::ServoThread()
{
    const auto dt = std::chrono::milliseconds(10);
    unsigned tick = 0;
    while (servoRunning.load())
    {
        std::this_thread::sleep_for(dt);
        // servoPos / servoVel 同样受 dataMutex 保护（SyncTask 上行反馈
        // 与 GetServoAngles 都会并发读取）
        std::lock_guard<std::mutex> lk(dataMutex);
        if (!enabled.load())
        {
            // 失能：自由缓慢回中（模拟无力矩）
            for (int jn = 0; jn < 6; jn++)
            {
                servoPos[jn] += (0.0f - servoPos[jn]) * 0.02f;
                servoVel[jn] *= 0.9f;
            }
        }
        else
        {
            for (int jn = 0; jn < 6; jn++)
            {
                const float sp = setPoints[jn];
                const float accel = (sp - servoPos[jn]) * SERVO_KS
                                    - servoVel[jn] * SERVO_KD;
                servoVel[jn] += accel * 0.01f;
                servoPos[jn] += servoVel[jn] * 0.01f;

                // 轻微测量噪声（ADC 采样抖动）
                // 注意：必须先转 int 再减 1，否则 tick%3==0 时无符号下溢
                // 回绕成 4294967295，噪声变成 +6400 万度的踢动
                if ((tick + jn) % 7 == 0)
                    servoPos[jn] += SERVO_NOISE * 0.1f * (float) ((int) (tick % 3) - 1);
                servoPos[jn] = Clamp(servoPos[jn], JOINT_MIN[jn], JOINT_MAX[jn]);
            }
        }
        tick++;
    }
}


// ---------------------------------------------------------------------------
// 预览接口
// ---------------------------------------------------------------------------
void VirtualElectronBot::GetVirtualScreen(uint8_t* _outRgb888)
{
    std::lock_guard<std::mutex> lk(screenMutex);
    memcpy(_outRgb888, screen, EB_FRAME_SIZE);
}


void VirtualElectronBot::GetServoAngles(float* _out6)
{
    std::lock_guard<std::mutex> lk(dataMutex);   // servoPos 由舵机线程写入
    memcpy(_out6, servoPos, sizeof(float) * 6);
}


void VirtualElectronBot::GetJointSetPoints(float* _out6)
{
    std::lock_guard<std::mutex> lk(dataMutex);
    memcpy(_out6, setPoints, sizeof(float) * 6);
}
