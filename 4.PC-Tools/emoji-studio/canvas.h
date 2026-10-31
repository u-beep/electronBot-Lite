#ifndef EB_CANVAS_H
#define EB_CANVAS_H

// 240x240 RGB888 画布：GC9A01 圆屏像素格式（与 SetImageSrc 输入一致）。
// 纯软件实现，无任何外部依赖。角度约定：屏幕坐标系 y 向下，
// 0deg = +x 方向，90deg = +y（向下）。

#include <cstdint>
#include <cmath>

#include "common/eb_screen.h"   // EB_LCD_W/H = 240（单一来源）

struct Rgb8
{
    uint8_t r, g, b;
};

class Canvas
{
public:
    uint8_t px[EB_LCD_W * EB_LCD_H * 3] = {};

    void Clear(uint8_t r, uint8_t g, uint8_t b);

    void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);

    void FillRect(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b);

    // 圆形（含边缘软过渡，简单抗锯齿）
    void FillCircle(float cx, float cy, float radius, uint8_t r, uint8_t g, uint8_t b);

    // 椭圆（scaleY 用于眨眼动画：1=睁眼, 0.1=闭眼）
    void FillEllipse(float cx, float cy, float rx, float ry, uint8_t r, uint8_t g, uint8_t b);

    // 粗弧线：极角 [a0deg, a1deg]，线宽 w
    void DrawArc(float cx, float cy, float radius, float a0deg, float a1deg,
                 float w, uint8_t r, uint8_t g, uint8_t b);

    // 粗直线
    void DrawLine(float x0, float y0, float x1, float y1,
                  float w, uint8_t r, uint8_t g, uint8_t b);

    // 心形填充（scale 为半尺寸）
    void FillHeart(float cx, float cy, float scale, uint8_t r, uint8_t g, uint8_t b);

    // 复制到外部 RGB888 缓冲（喂给 SetImageSrc）
    void BlitTo(uint8_t* dst) const;
};

#endif // EB_CANVAS_H
