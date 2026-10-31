// ============================================================================
// 文件：expression.cpp —— 表情渲染的实现
//
// 新手导读——"表情"的本质是三件事：
//   1. 参数化脸：眼睛 6 种画法 x 嘴巴 5 种画法，组合出各种表情
//   2. 动画 = 随时间变化的参数（眨眼高度、嘴巴张合度……）
//   3. RenderFrame(t) ：输入时间，输出那一瞬间的整帧画面 —— 纯函数，无状态
// ============================================================================

#include "expression.h"

// ---------------------------------------------------------------------------
// BlinkEnvelope()：眨眼"包络"——返回眼睛当前的睁开程度（0.08 闭合 ~ 1 全睁）
// 一个周期内的动作编排（按周期进度 k = 已播时间占比）：
//   0 ~ 45% 睁眼 → 45~50% 快速闭合 → 50~55% 闭眼停留 → 55~60% 快速睁开 → 睁眼
// ---------------------------------------------------------------------------
static float BlinkEnvelope(float tMs, float periodMs)
{
    // fmod = 取余：把任意大的时间折回一个周期内（动画循环的本质）
    const float t = (float) std::fmod((double) tMs, (double) periodMs);
    const float k = t / periodMs;             // 周期进度 0..1
    if (k < 0.45f) return 1.0f;
    if (k < 0.50f) return 1.0f - (k - 0.45f) / 0.05f;      // 快速闭合（线性插值）
    if (k < 0.55f) return 0.08f;                            // 闭眼（留条缝更自然）
    if (k < 0.60f) return 0.08f + (k - 0.55f) / 0.05f;      // 快速睁开
    return 1.0f;
}

// ---------------------------------------------------------------------------
// Builtin()：内置 5 组表情动画（studio 顶部按钮就是这 5 个）
// static 局部变量：第一次调用时构造一次，之后所有调用返回同一个列表
// ---------------------------------------------------------------------------
const std::vector<ExpressionAnim>& ExpressionRenderer::Builtin()
{
    static const std::vector<ExpressionAnim> list = {
        // 名字   { 眼型,                嘴型,               底色,          墨色,        腮红色,        腮红?}  周期ms 眨眼?
        { "Idle",   { EyeStyle::OPEN,     MouthStyle::SMILE,   {255,255,255}, {45,45,45}, {255,150,150}, false }, 3600, true  },
        { "Happy",  { EyeStyle::SMILE_ARC,MouthStyle::GRIN,    {255,224,178}, {60,40,20},  {255,120,120}, true  }, 2400, false },
        { "Love",   { EyeStyle::HEART,    MouthStyle::SMILE,   {255,235,238}, {200,30,60}, {255,130,140}, true  }, 1600, false },
        { "Angry",  { EyeStyle::ANGRY,    MouthStyle::SAD,     {255,235,232}, {60,50,55},  {0,0,0},       false }, 2000, true  },
        { "Sleepy", { EyeStyle::SLEEPY,   MouthStyle::OPEN_O,  {240,240,245}, {90,90,110}, {0,0,0},       false }, 5000, false },
    };
    return list;
}

// ---------------------------------------------------------------------------
// RenderFrame()：核心入口——渲染 tMs 时刻的完整一帧到画布 out
// ---------------------------------------------------------------------------
void ExpressionRenderer::RenderFrame(const ExpressionAnim& anim, float tMs, Canvas& out)
{
    const FaceParams& f = anim.base;              // 本表情的基础配色/部件
    out.Clear(f.faceBg.r, f.faceBg.g, f.faceBg.b);   // 第一步：铺底色

    // 眨眼程度：需要眨眼的表情算包络，否则恒为 1（全睁）
    const float blinkK = anim.blink ? BlinkEnvelope(tMs, anim.periodMs) : 1.0f;

    // 布局：240x240 中心 (120,120)，眼距 100，嘴在 y=170
    const float eyeY = 105.0f;
    const float eyeLX = 72.0f, eyeRX = 168.0f;

    // 腮红（两只眼睛外侧各一团粉色）
    if (f.showBlush)
    {
        out.FillCircle(eyeLX - 34, 148, 16, f.blush.r, f.blush.g, f.blush.b);
        out.FillCircle(eyeRX + 34, 148, 16, f.blush.r, f.blush.g, f.blush.b);
    }

    DrawEye(out, f.eye, eyeLX, eyeY, blinkK, f.ink);   // 左眼
    DrawEye(out, f.eye, eyeRX, eyeY, blinkK, f.ink);   // 右眼（同一画法，镜像形状内部处理）

    // 嘴部开合度：Sleepy 表情周期性打哈欠（余弦缓入缓出）
    float openK = 0.5f;                           // 默认半开
    if (f.eye == EyeStyle::SLEEPY)
    {
        const float k = (float) std::fmod((double) tMs, (double) anim.periodMs) / anim.periodMs;
        // 0.5 - 0.5*cos(...)：余弦曲线映射到 0..1（前 1/3 周期内张到最大）
        openK = 0.25f + 0.75f * (0.5f - 0.5f * std::cos(3.14159f * std::min(1.0f, k * 3.0f)));
    }
    DrawMouth(out, f.mouth, 120.0f, 170.0f, openK, f.ink);
}

// ---------------------------------------------------------------------------
// DrawEye()：画一只眼睛（左右眼共用，靠 cx 区分位置）
// blinkK：睁眼程度，0.08=闭眼 1=全睁
// ---------------------------------------------------------------------------
void ExpressionRenderer::DrawEye(Canvas& c, EyeStyle style, float cx, float cy,
                                 float blinkK, const Rgb8& ink)
{
    const float rx = 21.0f, ry = 26.0f;           // 眼睛基础尺寸（横/纵半径）
    switch (style)
    {
        case EyeStyle::OPEN:
            c.FillEllipse(cx, cy, rx, ry * blinkK, ink.r, ink.g, ink.b);
            // 高光：小白点让眼睛"有神"（闭眼时不画）
            if (blinkK > 0.55f)
                c.FillCircle(cx - 7, cy - 8, 5.5f, 255, 255, 255);
            break;

        case EyeStyle::BLINK:
            c.FillEllipse(cx, cy, rx, ry * blinkK, ink.r, ink.g, ink.b);
            break;

        case EyeStyle::SMILE_ARC:
            // ^ 弯眼：上半弧（角度 200..340）
            c.DrawArc(cx, cy + 14, 26, 205, 335, 9, ink.r, ink.g, ink.b);
            break;

        case EyeStyle::HEART:
        {
            // 心随眨眼轻微缩放（心跳感）
            const float s = 15.0f * (0.85f + 0.15f * blinkK);
            c.FillHeart(cx, cy, s, 205, 32, 68);                 // 深红心
            c.FillHeart(cx - 4, cy - 4, 4.5f, 255, 120, 140);   // 内高光（小粉心）
            break;
        }

        case EyeStyle::ANGRY:
        {
            c.FillEllipse(cx, cy, rx * 0.9f, ry * 0.8f, ink.r, ink.g, ink.b);
            // 斜眉：内低外高（怒容的关键）——cx<120 是左眼，眉毛方向相反
            if (cx < 120)
                c.DrawLine(cx - 24, cy - 34, cx + 18, cy - 22, 8, ink.r, ink.g, ink.b);
            else
                c.DrawLine(cx - 18, cy - 22, cx + 24, cy - 34, 8, ink.r, ink.g, ink.b);
            break;
        }

        case EyeStyle::SLEEPY:
            // -- 眯眼：细横线 + 下弧
            c.DrawLine(cx - 22, cy, cx + 22, cy, 7, ink.r, ink.g, ink.b);
            c.DrawArc(cx, cy + 10, 22, 30, 150, 5, ink.r, ink.g, ink.b);
            break;
    }
}

// ---------------------------------------------------------------------------
// DrawMouth()：画嘴（openK = 张合程度 0..1，OPEN_O 用）
// ---------------------------------------------------------------------------
void ExpressionRenderer::DrawMouth(Canvas& c, MouthStyle style, float cx, float cy,
                                   float openK, const Rgb8& ink)
{
    switch (style)
    {
        case MouthStyle::SMILE:
            // 下弯弧 = 微笑（屏幕坐标 y 向下，30..150 度为下半弧）
            c.DrawArc(cx, cy - 14, 34, 35, 145, 8, ink.r, ink.g, ink.b);
            break;

        case MouthStyle::NEUTRAL:
            c.DrawLine(cx - 28, cy, cx + 28, cy, 7, ink.r, ink.g, ink.b);   // 一横
            break;

        case MouthStyle::OPEN_O:
        {
            const float r = 10.0f + 14.0f * openK;   // 嘴巴大小随开合度变化
            c.FillEllipse(cx, cy, r, r, ink.r, ink.g, ink.b);                     // O 形
            c.FillEllipse(cx, cy + r * 0.45f, r * 0.55f, r * 0.45f, 190, 70, 90); // 舌头
            break;
        }

        case MouthStyle::SAD:
            // 上弯弧 = 沮丧（与微笑相反的弧向）
            c.DrawArc(cx, cy + 18, 32, 215, 325, 8, ink.r, ink.g, ink.b);
            break;

        case MouthStyle::GRIN:
        {
            // 露齿笑：深色矩形嘴 + 上下两排白牙
            c.FillRect((int) cx - 34, (int) cy - 14, (int) cx + 34, (int) cy + 16,
                       ink.r, ink.g, ink.b);
            c.FillRect((int) cx - 30, (int) cy - 11, (int) cx + 30, (int) cy + 1, 255, 255, 255);  // 上牙
            c.FillRect((int) cx - 30, (int) cy + 3, (int) cx + 30, (int) cy + 13, 255, 255, 255);  // 下牙
            // 分齿线：5 条竖细线把牙分开
            for (int i = -2; i <= 2; i++)
                c.DrawLine(cx + i * 12, cy - 11, cx + i * 12, cy + 13, 2, ink.r, ink.g, ink.b);
            break;
        }
    }
}
