#include "expression.h"

// ---------------------------------------------------------------------------
// 眨眼包络：周期内短暂闭合（0..1，1 = 全睁开）
// ---------------------------------------------------------------------------
static float BlinkEnvelope(float tMs, float periodMs)
{
    const float t = (float) std::fmod((double) tMs, (double) periodMs);
    const float k = t / periodMs;
    if (k < 0.45f) return 1.0f;
    if (k < 0.50f) return 1.0f - (k - 0.45f) / 0.05f;      // 快速闭合
    if (k < 0.55f) return 0.08f;                            // 闭眼
    if (k < 0.60f) return 0.08f + (k - 0.55f) / 0.05f;      // 快速睁开
    return 1.0f;
}

const std::vector<ExpressionAnim>& ExpressionRenderer::Builtin()
{
    static const std::vector<ExpressionAnim> list = {
        { "Idle",   { EyeStyle::OPEN,     MouthStyle::SMILE,   {255,255,255}, {45,45,45}, {255,150,150}, false }, 3600, true  },
        { "Happy",  { EyeStyle::SMILE_ARC,MouthStyle::GRIN,    {255,224,178}, {60,40,20},  {255,120,120}, true  }, 2400, false },
        { "Love",   { EyeStyle::HEART,    MouthStyle::SMILE,   {255,235,238}, {200,30,60}, {255,130,140}, true  }, 1600, false },
        { "Angry",  { EyeStyle::ANGRY,    MouthStyle::SAD,     {255,235,232}, {60,50,55},  {0,0,0},       false }, 2000, true  },
        { "Sleepy", { EyeStyle::SLEEPY,   MouthStyle::OPEN_O,  {240,240,245}, {90,90,110}, {0,0,0},       false }, 5000, false },
    };
    return list;
}

void ExpressionRenderer::RenderFrame(const ExpressionAnim& anim, float tMs, Canvas& out)
{
    const FaceParams& f = anim.base;
    out.Clear(f.faceBg.r, f.faceBg.g, f.faceBg.b);

    const float blinkK = anim.blink ? BlinkEnvelope(tMs, anim.periodMs) : 1.0f;

    // 布局：240x240 中心 (120,120)，眼距 100，嘴在 y=170
    const float eyeY = 105.0f;
    const float eyeLX = 72.0f, eyeRX = 168.0f;

    // 腮红
    if (f.showBlush)
    {
        out.FillCircle(eyeLX - 34, 148, 16, f.blush.r, f.blush.g, f.blush.b);
        out.FillCircle(eyeRX + 34, 148, 16, f.blush.r, f.blush.g, f.blush.b);
    }

    DrawEye(out, f.eye, eyeLX, eyeY, blinkK, f.ink);
    DrawEye(out, f.eye, eyeRX, eyeY, blinkK, f.ink);

    // 嘴部开合度：Sleepy 周期性打哈欠
    float openK = 0.5f;
    if (f.eye == EyeStyle::SLEEPY)
    {
        const float k = (float) std::fmod((double) tMs, (double) anim.periodMs) / anim.periodMs;
        openK = 0.25f + 0.75f * (0.5f - 0.5f * std::cos(3.14159f * std::min(1.0f, k * 3.0f)));
    }
    DrawMouth(out, f.mouth, 120.0f, 170.0f, openK, f.ink);
}

void ExpressionRenderer::DrawEye(Canvas& c, EyeStyle style, float cx, float cy,
                                 float blinkK, const Rgb8& ink)
{
    const float rx = 21.0f, ry = 26.0f;
    switch (style)
    {
        case EyeStyle::OPEN:
            c.FillEllipse(cx, cy, rx, ry * blinkK, ink.r, ink.g, ink.b);
            // 高光
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
            const float s = 15.0f * (0.85f + 0.15f * blinkK);
            c.FillHeart(cx, cy, s, 205, 32, 68);
            c.FillHeart(cx - 4, cy - 4, 4.5f, 255, 120, 140);   // 内高光
            break;
        }

        case EyeStyle::ANGRY:
        {
            c.FillEllipse(cx, cy, rx * 0.9f, ry * 0.8f, ink.r, ink.g, ink.b);
            // 斜眉：内低外高
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
            c.DrawLine(cx - 28, cy, cx + 28, cy, 7, ink.r, ink.g, ink.b);
            break;

        case MouthStyle::OPEN_O:
        {
            const float r = 10.0f + 14.0f * openK;
            c.FillEllipse(cx, cy, r, r, ink.r, ink.g, ink.b);
            c.FillEllipse(cx, cy + r * 0.45f, r * 0.55f, r * 0.45f, 190, 70, 90); // 舌头
            break;
        }

        case MouthStyle::SAD:
            // 上弯弧 = 沮丧
            c.DrawArc(cx, cy + 18, 32, 215, 325, 8, ink.r, ink.g, ink.b);
            break;

        case MouthStyle::GRIN:
        {
            // 露齿笑：圆角矩形嘴 + 上下牙
            c.FillRect((int) cx - 34, (int) cy - 14, (int) cx + 34, (int) cy + 16,
                       ink.r, ink.g, ink.b);
            c.FillRect((int) cx - 30, (int) cy - 11, (int) cx + 30, (int) cy + 1, 255, 255, 255);
            c.FillRect((int) cx - 30, (int) cy + 3, (int) cx + 30, (int) cy + 13, 255, 255, 255);
            // 分齿线
            for (int i = -2; i <= 2; i++)
                c.DrawLine(cx + i * 12, cy - 11, cx + i * 12, cy + 13, 2, ink.r, ink.g, ink.b);
            break;
        }
    }
}
