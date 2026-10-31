#ifndef ELECTRONBOTSDK_ELECTRONLOWLEVEL_H
#define ELECTRONBOTSDK_ELECTRONLOWLEVEL_H

#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

// Screen resolution: 240x240 RGB888
#define EB_SCREEN_WIDTH 240
#define EB_SCREEN_HEIGHT 240
#define EB_FRAME_SIZE (EB_SCREEN_WIDTH * EB_SCREEN_HEIGHT * 3) // 172800 bytes

class ElectronLowLevel
{
public:
    ElectronLowLevel() = default;

    ElectronLowLevel(int _vid, int _pid) :
        USB_PID(_pid), USB_VID(_vid)
    {}

    bool Connect();
    bool Disconnect();
    bool Sync();

    // Feed a 240x240 RGB888 image (172800 bytes) to the robot screen.
    void SetImageSrc(const uint8_t* _rgb888);
    void SetExtraData(uint8_t* _data, uint32_t _len = 32);
    void SetJointAngles(float _j1, float _j2, float _j3, float _j4, float _j5, float _j6,
                        bool _enable = false);
    void GetJointAngles(float* _jointAngles);
    uint8_t* GetExtraData(uint8_t* _data = nullptr);

    int USB_VID = 0x1001;
    int USB_PID = 0x8023;
    bool isConnected = false;
    uint32_t timeStamp = 0;


private:
    uint8_t pingPongWriteIndex = 0;
    uint8_t usbBuffer200[200]{};
    uint8_t frameBufferTx[2][EB_FRAME_SIZE]{};
    uint8_t extraDataBufferTx[2][32]{};
    uint8_t extraDataBufferRx[32]{};
    std::thread syncTaskHandle;

    static bool ReceivePacket(uint8_t* _buffer, uint32_t _packetCount, uint32_t _packetSize);
    static bool TransmitPacket(uint8_t* _buffer, uint32_t _packetCount, uint32_t _packetSize);
    static void SyncTask(ElectronLowLevel* _obj);
};


#endif
