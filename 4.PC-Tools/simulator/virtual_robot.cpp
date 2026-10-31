#include "virtual_robot.h"
#include <chrono>

// ============================================================================
// 文件：virtual_robot.cpp —— 虚拟机器人的全部实现
//
// 新手导读——这个文件模拟了真机系统的三大块：
//   1. Connect/Disconnect   ：模拟 USB 插拔（真机走 USBInterface.dll）
//   2. SyncTask()           ：模拟固件主循环（4 段收帧 → 刷虚拟显存）
//   3. ServoThread()        ：模拟舵机 PID 闭环（100Hz 独立线程）
//
// 为什么要多线程和锁？
//   你的主线程在调 SetImageSrc/SetJointAngles 的同时，
//   舵机线程在 100Hz 地转关节、Sync 线程在传帧——
//   三拨人同时读写同一份数据就会"打架"（数据竞争），
//   mutex（互斥锁）就是保证同一时刻只有一拨人能碰数据的排队机制。
// ============================================================================

// ---------------------------------------------------------------------------
// 关节动力学参数：二阶弹簧-阻尼，模拟舵机 PID 闭环的收敛速度和轻微惯性
// （调大 KS 收敛更快但容易"过冲"，调大 KD 运动更"黏滞"）
// ---------------------------------------------------------------------------
static constexpr float SERVO_KS = 90.0f;    // 位置刚度（1/s^2）：离目标越远加速度越大
static constexpr float SERVO_KD = 10.0f;    // 阻尼（1/s）：消耗速度，防止无限振荡
static constexpr float SERVO_NOISE = 0.15f; // 反馈噪声（度）：模拟真实 ADC 采样抖动


// Clamp()：把 v 限制在 [lo, hi] 区间内（低于取 lo，高于取 hi）
float VirtualElectronBot::Clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);   // 三目运算符嵌套：先查下限再查上限
}


// 析构函数：对象销毁时自动调用——确保舵机线程被停掉，不留僵尸线程
VirtualElectronBot::~VirtualElectronBot()
{
    Disconnect();
}


// ---------------------------------------------------------------------------
// Connect()：模拟 USB 连接成功 + 启动舵机仿真线程
// ---------------------------------------------------------------------------
bool VirtualElectronBot::Connect()
{
    // 虚拟设备：模拟 USB_ScanDevice + USB_OpenDevice 成功
    isConnected = true;
    timeStamp = 0;

    // 启动 100Hz 舵机闭环线程（模拟固件 1kHz 主循环 + PID）
    // 只启动一次：servoRunning 是"原子布尔"（多线程读写不会撕裂）
    if (!servoRunning.load())
    {
        servoRunning.store(true);
        servoThreadHandle = std::thread(&VirtualElectronBot::ServoThread, this);
        // &类名::函数名 取成员函数地址；this 让线程知道操作哪个对象
    }
    return true;
}


// ---------------------------------------------------------------------------
// Disconnect()：停掉所有线程，标记断开
// ---------------------------------------------------------------------------
bool VirtualElectronBot::Disconnect()
{
    if (syncTaskHandle.joinable())            // 还有帧在传，等它传完
        syncTaskHandle.join();

    if (servoRunning.load())                  // 舵机线程在跑
    {
        servoRunning.store(false);            // 先发"停止"信号
        if (servoThreadHandle.joinable())
            servoThreadHandle.join();         // 再等线程自己退出（不能硬杀）
    }

    if (isConnected)
    {
        isConnected = false;
        return true;
    }
    return false;
}


// ---------------------------------------------------------------------------
// Sync()：与 SDK 的 Sync 完全同语义——join 上一帧线程，起新线程
// ---------------------------------------------------------------------------
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


// SetImageSrc()：与 SDK 相同——把画面拷进"写侧"双缓冲
void VirtualElectronBot::SetImageSrc(const uint8_t* _rgb888)
{
    memcpy(frameTx[pingPongWriteIndex], _rgb888, EB_FRAME_SIZE);
}


// SetExtraData()：与 SDK 相同——写 32B 额外数据（一般用不到）
void VirtualElectronBot::SetExtraData(uint8_t* _data, uint32_t _len)
{
    if (_len <= 32)
        memcpy(extraTx[pingPongWriteIndex], _data, _len);
}


// ---------------------------------------------------------------------------
// SetJointAngles()：与 SDK 相同的打包逻辑——6 个角度 → 32B
// 格式：[0]=使能位 [1..24]=6 个 float 逐字节
// ---------------------------------------------------------------------------
void VirtualElectronBot::SetJointAngles(float _j1, float _j2, float _j3,
                                        float _j4, float _j5, float _j6,
                                        bool _enable)
{
    float j[6] = { _j1, _j2, _j3, _j4, _j5, _j6 };   // 收拢成数组好循环
    uint8_t* dst = extraTx[pingPongWriteIndex];       // 目标：写侧缓冲
    dst[0] = _enable ? 1 : 0;                         // 第 0 字节 = 使能标志
    for (int jn = 0; jn < 6; jn++)                    // 6 个角度
    {
        auto* b = (unsigned char*) &j[jn];            // float 地址当字节看
        for (int i = 0; i < 4; i++)                   // 每个角度拆 4 字节
            dst[jn * 4 + i + 1] = *(b + i);           // +1：第 0 字节被使能占了
    }
}


// GetJointAngles()：读舵机反馈角（注意加锁——舵机线程在并发写）
void VirtualElectronBot::GetJointAngles(float* _jointAngles)
{
    std::lock_guard<std::mutex> lk(dataMutex);
    // lock_guard：RAII 锁——构造时上锁，离开作用域自动解锁（防忘记解锁）
    memcpy(_jointAngles, servoPos, sizeof(float) * 6);   // 6 个 float = 24 字节
}


// ---------------------------------------------------------------------------
// SyncTask()：模拟固件主循环一帧（对应 UserApp/main.cpp 的 for p in 0..3 循环体）
// 与真机协议相同：4 段，每段"先收 32B 反馈，再收 43200B 图像"
// ---------------------------------------------------------------------------
void VirtualElectronBot::SyncTask()
{
    const uint8_t index = pingPongWriteIndex;          // 本帧读哪块缓冲
    pingPongWriteIndex = pingPongWriteIndex == 0 ? 1 : 0;   // 翻转写侧

    for (int p = 0; p < 4; p++)                        // 4 段循环
    {
        // 1) MCU -> PC：32B 额外数据（使能 + 6 关节当前角度，小端 float）
        //    （打包逻辑与 SDK 的 SetJointAngles 一模一样，方向相反）
        {
            std::lock_guard<std::mutex> lk(dataMutex);   // 读 servoPos 前先锁
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
        //    每段图像 = 240 列 x 60 行 x 3 字节 = 43200 字节
        const uint32_t segOffset = (uint32_t) p * (60 * 240 * 3);  // 段在整帧中的起点
        const uint8_t* segSrc = frameTx[index] + segOffset;        // 源：SDK 发的帧
        uint8_t* segDst = rxData[p & 1];          // 目的：ping-pong 接收缓冲（p&1 = 交替 0/1）
        memcpy(segDst, segSrc, 60 * 240 * 3);
        receivedPacketLen = 224;                  // 模拟"收到段尾"事件标志

        // 模拟 SPI DMA 刷屏耗时（USB HS ~3.5ms/帧 -> 1ms/段）
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        {
            std::lock_guard<std::mutex> lk(screenMutex);   // 显存是共享的，先锁
            memcpy(screen + segOffset, segDst, 60 * 240 * 3);  // 段数据落显存
        }

        // 3) 解析段尾 32B 额外数据（使能 + 关节设定值）
        //    真实固件：帧尾数据已包含在 43200B 内，此处直接解析 extraDataTx
        const uint8_t* extra = extraTx[index];
        bool en = extra[0] != 0;                  // 第 0 字节 → 使能
        float j[6];
        for (int jn = 0; jn < 6; jn++)
            j[jn] = *((float*) (extra + 4 * jn + 1));   // 4 字节还原成 float

        std::lock_guard<std::mutex> lk(dataMutex);
        enabled.store(en);                        // 更新使能状态
        for (int jn = 0; jn < 6; jn++)
            setPoints[jn] = Clamp(j[jn], JOINT_MIN[jn], JOINT_MAX[jn]);
            // 设定值钳位到关节物理范围（和真机一样，超范围的角度被拒绝）
    }
}


// ---------------------------------------------------------------------------
// ServoThread()：100Hz 舵机闭环 —— 模拟 STM32F042 固件的 DCE PID 位置环
//
// 二阶弹簧-阻尼模型的直觉：
//   加速度 = 刚度 x 距目标的距离 - 阻尼 x 当前速度
//   就像弹簧拉着小球去目标点：距离越远拉力越大，速度越快阻力越大。
// ---------------------------------------------------------------------------
void VirtualElectronBot::ServoThread()
{
    const auto dt = std::chrono::milliseconds(10);   // 10ms 一拍 = 100Hz
    unsigned tick = 0;                               // 节拍计数
    while (servoRunning.load())                      // Disconnect 时置 false 退出
    {
        std::this_thread::sleep_for(dt);
        // servoPos / servoVel 同样受 dataMutex 保护（SyncTask 上行反馈
        // 与 GetServoAngles 都会并发读取）
        std::lock_guard<std::mutex> lk(dataMutex);
        if (!enabled.load())
        {
            // 失能：没有力矩，机械臂自由缓慢垂回中位（阻尼衰减）
            for (int jn = 0; jn < 6; jn++)
            {
                servoPos[jn] += (0.0f - servoPos[jn]) * 0.02f;   // 每拍向 0 靠近 2%
                servoVel[jn] *= 0.9f;                            // 速度逐渐衰减
            }
        }
        else
        {
            for (int jn = 0; jn < 6; jn++)
            {
                const float sp = setPoints[jn];                  // 本关节目标角
                // 弹簧-阻尼加速度：位置误差产生加速度，速度产生阻力
                const float accel = (sp - servoPos[jn]) * SERVO_KS
                                    - servoVel[jn] * SERVO_KD;
                servoVel[jn] += accel * 0.01f;                   // 积分：加速度→速度
                servoPos[jn] += servoVel[jn] * 0.01f;            // 积分：速度→位置

                // 轻微测量噪声（ADC 采样抖动）
                // 注意：必须先转 int 再减 1，否则 tick%3==0 时无符号下溢
                // 回绕成 4294967295，噪声变成 +6400 万度的踢动（历史 bug！）
                if ((tick + jn) % 7 == 0)
                    servoPos[jn] += SERVO_NOISE * 0.1f * (float) ((int) (tick % 3) - 1);
                servoPos[jn] = Clamp(servoPos[jn], JOINT_MIN[jn], JOINT_MAX[jn]);
            }
        }
        tick++;
    }
}


// ---------------------------------------------------------------------------
// 以下三个是给预览窗口 / studio 用的"只读快照"接口
// ---------------------------------------------------------------------------
void VirtualElectronBot::GetVirtualScreen(uint8_t* _outRgb888)
{
    std::lock_guard<std::mutex> lk(screenMutex);   // 显存可能正被 SyncTask 更新
    memcpy(_outRgb888, screen, EB_FRAME_SIZE);
}


void VirtualElectronBot::GetServoAngles(float* _out6)
{
    std::lock_guard<std::mutex> lk(dataMutex);     // servoPos 由舵机线程写入
    memcpy(_out6, servoPos, sizeof(float) * 6);
}


void VirtualElectronBot::GetJointSetPoints(float* _out6)
{
    std::lock_guard<std::mutex> lk(dataMutex);
    memcpy(_out6, setPoints, sizeof(float) * 6);
}
