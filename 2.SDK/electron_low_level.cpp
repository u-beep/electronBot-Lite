// ============================================================================
// 文件：electron_low_level.cpp —— ElectronLowLevel 类的实现
// 新手导读：
//   * .cpp 是"实现文件"：把 .h 里声明的函数逐个写出来。
//   * 函数名前的 "ElectronLowLevel::" 表示"这个函数属于这个类"。
//   * 本文件做三件事：管理 USB 连接、搬运图像数据、
//     在后台线程里按协议把数据发给 MCU。
// ============================================================================

#include "electron_low_level.h"   // 自己的类声明
#include "USBInterface.h"         // 厂商提供的 USB 动态库接口（USB_ScanDevice 等）


// ---------------------------------------------------------------------------
// Sync()：启动一次帧同步（应用层最常用的函数之一）
// 流程：如果上一帧还没传完，先等它结束 → 再开一个新线程去传当前帧。
// 这样调用方（你的 main 循环）永远不会被 USB 传输卡住。
// ---------------------------------------------------------------------------
bool ElectronLowLevel::Sync()
{
    if (isConnected)                              // 只有连接上了才有得传
    {
        if (syncTaskHandle.joinable())            // joinable() = 上一个线程还在跑
            syncTaskHandle.join();                // 等上一帧传完（防止两帧混在一起）

        syncTaskHandle = std::thread(SyncTask, this);
        // 新开一个后台线程执行 SyncTask(this)，
        // this = 把当前对象指针传进去，让静态函数能访问成员变量

        timeStamp++;                              // 帧计数器 +1（相当于帧号）
        return true;
    }

    return false;                                 // 没连接就返回失败
}


// ---------------------------------------------------------------------------
// SetImageSrc()：把一帧 240x240 RGB888 画面拷进"当前写侧"的双缓冲
// 注意：这里只是拷贝（瞬间完成），真正的 USB 传输在 Sync 开的线程里做
// ---------------------------------------------------------------------------
void ElectronLowLevel::SetImageSrc(const uint8_t* _rgb888)
{
    // memcpy(目的地, 来源, 字节数)：整块内存一次性拷贝
    // frameBufferTx[pingPongWriteIndex] = 双缓冲中的"写侧"那块（172800 字节）
    memcpy(frameBufferTx[pingPongWriteIndex], _rgb888, EB_FRAME_SIZE);
}


// ---------------------------------------------------------------------------
// SetExtraData()：直接写 32 字节额外数据（一般用 SetJointAngles 就够了）
// ---------------------------------------------------------------------------
void ElectronLowLevel::SetExtraData(uint8_t* _data, uint32_t _len)
{
    if (_len <= 32)                               // 缓冲只有 32 字节，防止越界
        memcpy(extraDataBufferTx[pingPongWriteIndex], _data, _len);
}


// ---------------------------------------------------------------------------
// GetExtraData()：取出 MCU 回传的 32 字节原始数据
// 传入 _data 就拷贝一份给你；不传就直接返回内部缓冲的指针
// ---------------------------------------------------------------------------
uint8_t* ElectronLowLevel::GetExtraData(uint8_t* _data)
{
    if (_data != nullptr)                         // nullptr = 空指针（调用方没给缓冲）
        memcpy(_data, extraDataBufferRx, 32);

    return extraDataBufferRx;                     // 无论如何都返回内部缓冲地址
}


// ---------------------------------------------------------------------------
// ReceivePacket()：从 USB 收 _packetCount 个包，每包 _packetSize 字节
// 内层 do-while：单个包没收满就反复重试（超时 100ms）
// 外层 do-while：一个包成功后继续收下一个，直到收满 packetCount 个
// ---------------------------------------------------------------------------
bool ElectronLowLevel::ReceivePacket(uint8_t* _buffer, uint32_t _packetCount, uint32_t _packetSize)
{
    uint32_t packetCount = _packetCount;          // 剩余要收的包数
    uint32_t ret;                                 // 本次实际收到的字节数
    do
    {
        do
        {
            // USB_BulkReceive(设备号, 端点, 缓冲区, 期望字节数, 超时ms)
            // EP1_IN 是 MCU→PC 方向的批量传输端点
            ret = USB_BulkReceive(0, EP1_IN, reinterpret_cast<char*>(_buffer), _packetSize, 100);
            // reinterpret_cast<char*>：把 uint8_t* 强转成库要求的 char* 类型
        } while (ret != _packetSize);             // 没收满就再来一次

        packetCount--;                            // 收满一包，计数-1
    } while (packetCount > 0);

    return packetCount == 0;                      // 全收完返回 true
}


// ---------------------------------------------------------------------------
// TransmitPacket()：向 USB 发 _packetCount 个包（逻辑与上面完全对称）
// ---------------------------------------------------------------------------
bool ElectronLowLevel::TransmitPacket(uint8_t* _buffer, uint32_t _packetCount, uint32_t _packetSize)
{
    uint32_t packetCount = _packetCount;          // 剩余要发的包数
    uint32_t dataOffset = 0;                      // 已发到缓冲区的哪个字节
    uint32_t ret;
    do
    {
        do
        {
            // USB_BulkTransmit(设备号, 端点, 数据地址, 发送字节数, 超时ms)
            // 注意传的是 _buffer + dataOffset：每次从"上次发完的位置"继续
            ret = USB_BulkTransmit(0, EP1_OUT,
                                   reinterpret_cast<char*>(_buffer) + dataOffset,
                                   _packetSize, 100);
        } while (!ret);                           // ret==0 表示没发出去，重试

        dataOffset += _packetSize;                // 前进一包的长度
        packetCount--;
    } while (packetCount > 0);

    return packetCount == 0;
}


// ---------------------------------------------------------------------------
// Connect()：扫描并打开机器人 USB 设备
// ---------------------------------------------------------------------------
bool ElectronLowLevel::Connect()
{
    // 按 VID/PID 扫描设备，返回找到的设备数量
    int devNum = USB_ScanDevice(USB_PID, USB_VID);

    if (devNum > 0)                               // 找到了至少一台
    {
        if (USB_OpenDevice(0))                    // 打开 0 号设备
        {
            isConnected = true;                   // 标记已连接
            timeStamp = 0;                        // 帧号清零

            return true;
        }
    }

    return false;                                 // 没找到或打不开
}


// ---------------------------------------------------------------------------
// Disconnect()：断开连接。先等后台线程传完，再关设备
// ---------------------------------------------------------------------------
bool ElectronLowLevel::Disconnect()
{
    if (syncTaskHandle.joinable())                // 还有帧在传
        syncTaskHandle.join();                    // 等它传完（否则会撕裂数据）

    if (isConnected && USB_CloseDevice(0))
    {
        isConnected = false;
        return true;
    }

    return false;
}


// ---------------------------------------------------------------------------
// SyncTask()：后台线程主循环 —— 整个协议的核心！
//
// 一次 Sync 把 172800B 画面 + 32B 关节数据 分成 4 段下发：
//   每段 = 84 包 x 512B（图像主体 43008B） + 1 包 224B（帧尾 192B + 关节 32B）
//   4 x (43008 + 192) = 172800（图像）  4 x 32 = 128（关节数据分 4 份）
// 每段开头先"收"MCU 发来的 32B 关节反馈 —— 一收一发，节奏由 MCU 主导。
//
// ping-pong 关键两行：进来先把"写侧"换成另一块缓冲，
// 这样主线程可以继续往新缓冲写下一帧，而本线程慢慢传旧缓冲。
// ---------------------------------------------------------------------------
void ElectronLowLevel::SyncTask(ElectronLowLevel* _obj)
{
    uint32_t frameBufferOffset = 0;               // 当前传到图像缓冲的哪个字节
    uint8_t index = _obj->pingPongWriteIndex;     // 记下本帧用的是哪块缓冲（读侧）
    // 立刻翻转索引：主线程下一次 SetImageSrc 会写到另一块，互不干扰
    _obj->pingPongWriteIndex = _obj->pingPongWriteIndex == 0 ? 1 : 0;
    for (int p = 0; p < 4; p++)                   // 4 个段，一段一段传
    {
        // 1) 等 MCU 发"要数据"的请求，顺便收回 32B 关节反馈角
        _obj->ReceivePacket(reinterpret_cast<uint8_t*>(_obj->extraDataBufferRx),
                            1, 32);

        // 2) 发本段的图像主体：84 包 x 512B = 43008 字节
        _obj->TransmitPacket(reinterpret_cast<uint8_t*>(_obj->frameBufferTx[index]) + frameBufferOffset,
                             84, 512);
        frameBufferOffset += 43008;               // 偏移推进到下一段的起点

        // 3) 打包"帧尾 + 关节数据"：192B 图像尾巴 + 32B 关节 = 224B
        memcpy(_obj->usbBuffer200, reinterpret_cast<uint8_t*>(_obj->frameBufferTx[index]) + frameBufferOffset,
               192);
        memcpy(_obj->usbBuffer200 + 192, reinterpret_cast<uint8_t*>(_obj->extraDataBufferTx[index]), 32);

        // 4) 发这 224 字节（1 包搞定）
        _obj->TransmitPacket(_obj->usbBuffer200, 1, 224);
        frameBufferOffset += 192;                 // 192B 图像尾巴算进图像偏移
    }
}

// ---------------------------------------------------------------------------
// SetJointAngles()：把 6 个角度打包成 32B 关节数据
// 格式：[0]=使能位(1字节)  [1..24]=6 个角度各 4 字节（float 的原始字节）
// float 占 4 字节，这里逐字节拆开存（小端序，和 MCU 约定一致）
// ---------------------------------------------------------------------------
void ElectronLowLevel::SetJointAngles(float _j1, float _j2, float _j3, float _j4, float _j5, float _j6,
                                      bool _enable)
{
    float jointAngleSetPoints[6];                 // 先收拢到数组方便循环处理

    jointAngleSetPoints[0] = _j1;                 // 头
    jointAngleSetPoints[1] = _j2;                 // 左臂 Roll
    jointAngleSetPoints[2] = _j3;                 // 左臂 Pitch
    jointAngleSetPoints[3] = _j4;                 // 右臂 Roll
    jointAngleSetPoints[4] = _j5;                 // 右臂 Pitch
    jointAngleSetPoints[5] = _j6;                 // 腰

    extraDataBufferTx[pingPongWriteIndex][0] = _enable ? 1 : 0;
    // 第 0 字节 = 使能标志（? : 是三目运算符：真取 1，假取 0）
    for (int j = 0; j < 6; j++)                   // 6 个角度
        for (int i = 0; i < 4; i++)               // 每个角度 4 个字节
        {
            auto* b = (unsigned char*) &(jointAngleSetPoints[j]);
            // 把 float 的地址当成字节序列来看（才能逐字节拆）
            extraDataBufferTx[pingPongWriteIndex][j * 4 + i + 1] = *(b + i);
            // +1 是因为第 0 字节被使能位占了；b+i = float 的第 i 个字节
        }
}


// ---------------------------------------------------------------------------
// GetJointAngles()：从 32B 反馈数据里解出 6 个角度（打包的逆过程）
// 反馈格式：[0]=保留  [1..24]=6 个 float 角度  （与打包格式对应）
// ---------------------------------------------------------------------------
void ElectronLowLevel::GetJointAngles(float* _jointAngles)
{
    for (int j = 0; j < 6; j++)
    {
        // 把 extraDataBufferRx + 4*j + 1 处的 4 个字节"还原"成 float
        _jointAngles[j] = *((float*) (extraDataBufferRx + 4 * j + 1));
    }
}
