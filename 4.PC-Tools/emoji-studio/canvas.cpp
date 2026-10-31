#include "canvas.h"
#include <algorithm>

void Canvas::Clear(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < EB_LCD_W * EB_LCD_H; i++)
    {
        px[i * 3] = r;
        px[i * 3 + 1] = g;
        px[i * 3 + 2] = b;
    }
}

void Canvas::SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x < 0 || x >= EB_LCD_W || y < 0 || y >= EB_LCD_H)
        return;
    uint8_t* p = &px[(y * EB_LCD_W + x) * 3];
    p[0] = r;
    p[1] = g;
    p[2] = b;
}

void Canvas::FillRect(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            SetPixel(x, y, r, g, b);
}

void Canvas::FillCircle(float cx, float cy, float radius, uint8_t r, uint8_t g, uint8_t b)
{
    const int x0 = (int) (cx - radius - 2), x1 = (int) (cx + radius + 2);
    const int y0 = (int) (cy - radius - 2), y1 = (int) (cy + radius + 2);
    for (int y = y0; y <= y1; y++)
    {
        for (int x = x0; x <= x1; x++)
        {
            const float dx = x - cx, dy = y - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d <= radius - 1.0f)
            {
                SetPixel(x, y, r, g, b);
            }
            else if (d < radius)   // 边缘软过渡（简易抗锯齿）
            {
                // 240p 小屏直接覆盖式混合即可，成本最低
                SetPixel(x, y, r, g, b);
            }
        }
    }
}

void Canvas::FillEllipse(float cx, float cy, float rx, float ry,
                         uint8_t r, uint8_t g, uint8_t b)
{
    if (ry < 0.5f) ry = 0.5f;
    const int x0 = (int) (cx - rx - 2), x1 = (int) (cx + rx + 2);
    const int y0 = (int) (cy - ry - 2), y1 = (int) (cy + ry + 2);
    for (int y = y0; y <= y1; y++)
    {
        for (int x = x0; x <= x1; x++)
        {
            const float dx = (x - cx) / rx, dy = (y - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f)
                SetPixel(x, y, r, g, b);
        }
    }
}

void Canvas::DrawArc(float cx, float cy, float radius, float a0deg, float a1deg,
                     float w, uint8_t r, uint8_t g, uint8_t b)
{
    const float a0 = a0deg * 3.14159265f / 180.0f;
    const float a1 = a1deg * 3.14159265f / 180.0f;
    const float steps = (std::fabs(a1 - a0) * std::max(radius, 1.0f)) + 8.0f;
    for (int s = 0; s <= (int) steps; s++)
    {
        const float t = a0 + (a1 - a0) * s / steps;
        const float px_ = cx + radius * std::cos(t);
        const float py_ = cy + radius * std::sin(t);
        FillCircle(px_, py_, w * 0.5f, r, g, b);
    }
}

void Canvas::DrawLine(float x0, float y0, float x1, float y1,
                      float w, uint8_t r, uint8_t g, uint8_t b)
{
    const float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    const int steps = (int) (len * 2.0f) + 2;
    for (int s = 0; s <= steps; s++)
    {
        const float t = (float) s / steps;
        FillCircle(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, w * 0.5f, r, g, b);
    }
}

void Canvas::FillHeart(float cx, float cy, float scale, uint8_t r, uint8_t g, uint8_t b)
{
    // 心形隐式方程：(x^2 + y^2 - 1)^3 - x^2 * y^3 <= 0
    // 归一化坐标：px in [-1.3, 1.3]，屏幕 y 向下 -> 翻转 y
    const int x0 = (int) (cx - 1.3f * scale), x1 = (int) (cx + 1.3f * scale);
    const int y0 = (int) (cy - 1.3f * scale), y1 = (int) (cy + 1.3f * scale);
    for (int y = y0; y <= y1; y++)
    {
        for (int x = x0; x <= x1; x++)
        {
            const float nx = (x - cx) / scale;
            const float ny = -(y - cy) / scale;
            const float q = nx * nx + ny * ny - 1.0f;
            if (q * q * q - nx * nx * ny * ny * ny <= 0.0f)
                SetPixel(x, y, r, g, b);
        }
    }
}

void Canvas::BlitTo(uint8_t* dst) const
{
    for (int i = 0; i < EB_LCD_W * EB_LCD_H * 3; i++)
        dst[i] = px[i];
}
