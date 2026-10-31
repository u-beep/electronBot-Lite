#ifndef EB_EXPRESSION_H
#define EB_EXPRESSION_H

// ElectronBot 圆脸表情系统：参数化脸 + 5 组内置动画。
// RenderFrame(tMs) 输出任意时刻的 240x240 RGB888 帧，
// 可直接喂给 VirtualElectronBot::SetImageSrc / 真机 ElectronLowLevel。

#include "canvas.h"
#include <vector>
#include <string>

enum class EyeStyle
{
    OPEN,       // 圆睁
    BLINK,      // 眨眼（由 t 驱动高度）
    SMILE_ARC,  // 笑弯眼 ^ ^
    HEART,      // 爱心眼
    ANGRY,      // 斜眉怒眼
    SLEEPY      // 眯眼 --
};

enum class MouthStyle
{
    SMILE,      // 微笑弧
    NEUTRAL,    // 平线
    OPEN_O,     // 惊讶 O
    SAD,        // 沮丧弧
    GRIN        // 露齿笑（矩形+弧）
};

struct FaceParams
{
    EyeStyle eye = EyeStyle::OPEN;
    MouthStyle mouth = MouthStyle::SMILE;
    Rgb8 faceBg = { 255, 255, 255 };   // 底色
    Rgb8 ink = { 40, 40, 40 };         // 五官色
    Rgb8 blush = { 255, 160, 160 };    // 腮红（可关）
    bool showBlush = false;
};

// 表情动画条目：一个循环动画 = 关键姿态 + 播放属性
struct ExpressionAnim
{
    std::string name;
    FaceParams base;              // 基础姿态
    float periodMs = 3000.0f;     // 循环周期
    bool blink = true;            // 周期内插入眨眼
};

class ExpressionRenderer
{
public:
    // 渲染 tMs 时刻的一帧到 canvas
    void RenderFrame(const ExpressionAnim& anim, float tMs, Canvas& out);

    // 内置动画集
    static const std::vector<ExpressionAnim>& Builtin();

private:
    void DrawEye(Canvas& c, EyeStyle style, float cx, float cy,
                 float blinkK, const Rgb8& ink);
    void DrawMouth(Canvas& c, MouthStyle style, float cx, float cy,
                   float openK, const Rgb8& ink);
};

#endif // EB_EXPRESSION_H
