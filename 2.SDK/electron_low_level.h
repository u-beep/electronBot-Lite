#ifndef ELECTRONBOTSDK_ELECTRONLOWLEVEL_H
#define ELECTRONBOTSDK_ELECTRONLOWLEVEL_H
// ============================================================================
// 文件：electron_low_level.h —— ElectronBot SDK 底层接口（头文件）
// 作用：定义 ElectronLowLevel 类，它是 PC 端控制机器人的"唯一入口"。
//
// 新手导读：
//   * .h 文件叫"头文件"，只写类的"声明"（有哪些函数、有哪些变量），
//     具体实现在同名 .cpp 里。别的代码只要 #include 这个 .h，
//     就知道怎么调用这个类。
//   * #ifndef/#define/#endif 三件套叫"包含保护"，
//     防止同一个头文件被重复包含导致编译错误（固定写法，背下来即可）。
//
// 这个类屏蔽了所有底层细节（USB 传输、分包、双缓冲），
// 你只需要会 5 个函数：Connect / SetImageSrc / SetJointAngles / Sync / GetJointAngles。
// ============================================================================

// C++ 标准库头文件：
#include <cstdint>   // uint8_t / uint32_t 等固定宽度整数类型
#include <cstring>   // memcpy（内存拷贝函数）
#include <string>    // std::string
#include <thread>    // std::thread（多线程，Sync 用它做后台同步）

// ---- 屏幕参数宏 ----
// GC9A01 圆屏分辨率 240x240，RGB888 = 每像素 3 字节（红绿蓝各 1 字节）
// 240 * 240 * 3 = 172800 字节，就是一帧完整画面的数据量
#define EB_SCREEN_WIDTH 240
#define EB_SCREEN_HEIGHT 240
#define EB_FRAME_SIZE (EB_SCREEN_WIDTH * EB_SCREEN_HEIGHT * 3) // 172800 bytes

class ElectronLowLevel
{
public:
    // ---- 构造函数：创建对象时自动调用 ----
    ElectronLowLevel() = default;   // 默认构造：不传参就用默认 VID/PID

    // 带参构造：允许自定义 USB 设备的 VID/PID（厂商 ID/产品 ID）
    ElectronLowLevel(int _vid, int _pid) :
        USB_PID(_pid), USB_VID(_vid)   // 初始化列表：直接给成员赋初值
    {}

    // ==================== 5 个核心 API ====================

    // 连接机器人（扫描并打开 USB 设备）。成功返回 true
    bool Connect();

    // 断开连接（关闭 USB 设备）
    bool Disconnect();

    // 执行一次完整的 PC<->MCU 帧同步：
    // 把最新的画面 + 关节角度发给机器人，同时收回关节反馈角。
    // 注意：它只"启动"同步（开了一个后台线程），不会卡住你的程序
    bool Sync();

    // 喂一帧画面给机器人屏幕：240x240 RGB888（172800 字节）
    // 只是"拷贝到发送缓冲区"，真正发出去要等下一次 Sync()
    void SetImageSrc(const uint8_t* _rgb888);

    // 设置 32 字节"额外数据"（里面装着关节角度，见 SetJointAngles）
    void SetExtraData(uint8_t* _data, uint32_t _len = 32);

    // 设置 6 个关节的目标角度（单位：度）
    // j1=头 j2=左臂Roll j3=左臂Pitch j4=右臂Roll j5=右臂Pitch j6=腰
    // _enable=true 表示给舵机上电（使其能转动），false 是断电松弛
    void SetJointAngles(float _j1, float _j2, float _j3, float _j4, float _j5, float _j6,
                        bool _enable = false);

    // 读取 6 个关节的"反馈角度"（机器人实际转到了哪，由 MCU 回传）
    void GetJointAngles(float* _jointAngles);

    // 读取 32 字节原始额外数据（一般不用直接调，用 GetJointAngles 即可）
    uint8_t* GetExtraData(uint8_t* _data = nullptr);

    // ==================== 公开成员变量 ====================
    // USB 设备的厂商 ID / 产品 ID（机器人出厂固定为 0x1001/0x8023）
    int USB_VID = 0x1001;
    int USB_PID = 0x8023;

    bool isConnected = false;    // 当前是否已连接
    uint32_t timeStamp = 0;      // 已执行的 Sync 次数（相当于帧计数器）


private:
    // ==================== 私有成员（底层细节，应用层不用管） ====================

    // ping-pong 双缓冲的"写侧"索引（0 或 1）：
    // 两块缓冲区轮流写/读——你写 A 块时硬件读 B 块，避免读到写了一半的数据
    uint8_t pingPongWriteIndex = 0;

    uint8_t usbBuffer200[200]{};                   // 每段"帧尾+关节数据"的打包缓冲
    uint8_t frameBufferTx[2][EB_FRAME_SIZE]{};     // 发送画面双缓冲（2 块 x 172800B）
    uint8_t extraDataBufferTx[2][32]{};            // 发送关节数据双缓冲（2 块 x 32B）
    uint8_t extraDataBufferRx[32]{};               // 接收关节反馈（32B = 使能位 + 6 个 float）
    std::thread syncTaskHandle;                    // Sync 启动的后台传输线程

    // 三个内部工具函数（static = 不依赖具体对象，只是打包 USB 收发循环）
    static bool ReceivePacket(uint8_t* _buffer, uint32_t _packetCount, uint32_t _packetSize);
    static bool TransmitPacket(uint8_t* _buffer, uint32_t _packetCount, uint32_t _packetSize);
    static void SyncTask(ElectronLowLevel* _obj);  // 后台线程的主循环
};


#endif
