#ifndef EB_ANIM_H
#define EB_ANIM_H

// .ebanim 动画序列文件格式（小端二进制）：
//   [8B magic "EBANIM1\0"][4B frameCount][2B width][2B height]
//   frameCount x { [2B durationMs][width*height*3 B RGB888] }
// 可用 emoji_export 导出、由 Studio / 任意播放器加载、也可离线转真机帧流。

#include <cstdint>
#include <string>
#include <vector>

struct AnimFrame
{
    uint16_t durationMs = 33;
    uint8_t pixels[240 * 240 * 3] = {};
};

class EbAnim
{
public:
    std::vector<AnimFrame> frames;

    // 对某个表情动画采样 N 帧（periodMs 循环）
    // 由 ExpressionRenderer 驱动，见 ebanim.cpp
    static EbAnim Capture(class ExpressionRenderer& renderer,
                          const struct ExpressionAnim& anim,
                          int framesToCapture, int fps);

    bool Save(const std::string& path) const;
    static EbAnim Load(const std::string& path);   // 失败返回空 frames

    uint32_t TotalDurationMs() const;
};

#endif // EB_ANIM_H
