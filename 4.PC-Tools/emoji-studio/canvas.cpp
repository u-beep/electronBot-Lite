// ============================================================================
// 文件：canvas.cpp —— 软件画布的实现（所有图形都是"算出来"再填像素的）
//
// 新手导读——软件绘图的核心思想：
//   屏幕是一块 240x240 的像素数组，画任何图形 = 逐个判断
//   "这个像素在不在图形内部"，在就涂色。
//   圆 = 到圆心距离 <= 半径；椭圆 = 拉伸过的圆；
//   弧线 = 圆周上取很多点，每个点画个小圆（"点成线"）。
// ============================================================================

#include "canvas.h"
#include <algorithm>

// Clear()：整块画布涂成一个颜色（r/g/b 各 0-255）
void Canvas::Clear(uint8_t r, uint8_t g, uint8_t b)
{
    // px 是一维数组但按"行优先"存：第 i 个像素占 px[i*3] ~ px[i*3+2] 三个字节
    for (int i = 0; i < EB_LCD_W * EB_LCD_H; i++)
    {
        px[i * 3] = r;         // 红
        px[i * 3 + 1] = g;     // 绿
        px[i * 3 + 2] = b;     // 蓝
    }
}

// SetPixel()：涂一个像素（最基本的绘图操作，其他函数都基于它）
void Canvas::SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    // 越界的像素直接丢弃（不崩溃）——调用方就不用写边界检查了
    if (x < 0 || x >= EB_LCD_W || y < 0 || y >= EB_LCD_H)
        return;
    // 一维下标换算：第 y 行第 x 列的像素从第 (y*宽+x) 个像素开始
    uint8_t* p = &px[(y * EB_LCD_W + x) * 3];
    p[0] = r;
    p[1] = g;
    p[2] = b;
}

// FillRect()：填充矩形（左上角 x0,y0 到右下角 x1,y1，含边界）
void Canvas::FillRect(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b)
{
    for (int y = y0; y <= y1; y++)            // 逐行
        for (int x = x0; x <= x1; x++)        // 逐列
            SetPixel(x, y, r, g, b);
}

// ---------------------------------------------------------------------------
// FillCircle()：填充圆。cx/cy=圆心，radius=半径
// 原理：只扫描圆的外接方形区域，逐像素算"到圆心的距离"
// ---------------------------------------------------------------------------
void Canvas::FillCircle(float cx, float cy, float radius, uint8_t r, uint8_t g, uint8_t b)
{
    // 只扫圆的外接矩形（+2 像素余量），不扫全屏——省 90% 以上的计算量
    const int x0 = (int) (cx - radius - 2), x1 = (int) (cx + radius + 2);
    const int y0 = (int) (cy - radius - 2), y1 = (int) (cy + radius + 2);
    for (int y = y0; y <= y1; y++)
    {
        for (int x = x0; x <= x1; x++)
        {
            const float dx = x - cx, dy = y - cy;     // 像素相对圆心的偏移
            const float d = std::sqrt(dx * dx + dy * dy);   // 欧氏距离（勾股定理）
            if (d <= radius - 1.0f)
            {
                SetPixel(x, y, r, g, b);              // 圆内部：直接涂
            }
            else if (d < radius)   // 边缘软过渡（简易抗锯齿）
            {
                // 240p 小屏直接覆盖式混合即可，成本最低
                SetPixel(x, y, r, g, b);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// FillEllipse()：填充椭圆。rx/ry = 横/纵半径
// 眨眼动画就这么做：眼睛的 ry 从 1 渐变到 0.1（见 expression.cpp）
// ---------------------------------------------------------------------------
void Canvas::FillEllipse(float cx, float cy, float rx, float ry,
                         uint8_t r, uint8_t g, uint8_t b)
{
    if (ry < 0.5f) ry = 0.5f;                 // 防止除以 0（闭眼时 ry 趋近 0）
    const int x0 = (int) (cx - rx - 2), x1 = (int) (cx + rx + 2);
    const int y0 = (int) (cy - ry - 2), y1 = (int) (cy + ry + 2);
    for (int y = y0; y <= y1; y++)
    {
        for (int x = x0; x <= x1; x++)
        {
            // 把偏移量除以半径（归一化），椭圆方程就变回了圆方程
            const float dx = (x - cx) / rx, dy = (y - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f)    // 单位圆内部
                SetPixel(x, y, r, g, b);
        }
    }
}

// ---------------------------------------------------------------------------
// DrawArc()：画弧线（比如微笑的嘴巴）。角度单位是"度"
// 原理：把弧"打碎"成足够多的小段，每段画一个小圆点——点连起来就是线
// ---------------------------------------------------------------------------
void Canvas::DrawArc(float cx, float cy, float radius, float a0deg, float a1deg,
                     float w, uint8_t r, uint8_t g, uint8_t b)
{
    const float a0 = a0deg * 3.14159265f / 180.0f;    // 度 -> 弧度（三角函数用弧度）
    const float a1 = a1deg * 3.14159265f / 180.0f;
    // 采样点数：弧越长/半径越大，需要的点越多（+8 保证最短弧也有点）
    const float steps = (std::fabs(a1 - a0) * std::max(radius, 1.0f)) + 8.0f;
    for (int s = 0; s <= (int) steps; s++)
    {
        const float t = a0 + (a1 - a0) * s / steps;   // 当前采样点的角度
        const float px_ = cx + radius * std::cos(t);  // 极坐标 -> 直角坐标
        const float py_ = cy + radius * std::sin(t);
        FillCircle(px_, py_, w * 0.5f, r, g, b);      // 线宽 w = 小圆的直径
    }
}

// ---------------------------------------------------------------------------
// DrawLine()：画粗直线。原理同上——在直线上密集取点画小圆
// ---------------------------------------------------------------------------
void Canvas::DrawLine(float x0, float y0, float x1, float y1,
                      float w, uint8_t r, uint8_t g, uint8_t b)
{
    const float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    const int steps = (int) (len * 2.0f) + 2;         // 每 0.5 像素取一个点
    for (int s = 0; s <= steps; s++)
    {
        const float t = (float) s / steps;            // 0..1 的进度比例
        // 线性插值：t=0 在起点，t=1 在终点，中间按比例
        FillCircle(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, w * 0.5f, r, g, b);
    }
}

// ---------------------------------------------------------------------------
// FillHeart()：填充心形（Love 表情的眼睛）
// 用的是著名的"心形隐式方程"——满足不等式的点都涂色，形状自然出现
// ---------------------------------------------------------------------------
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
            const float nx = (x - cx) / scale;        // 换算到方程的坐标系
            const float ny = -(y - cy) / scale;       // 负号：屏幕 y 向下，方程 y 向上
            const float q = nx * nx + ny * ny - 1.0f;
            if (q * q * q - nx * nx * ny * ny * ny <= 0.0f)   // 在心形内部
                SetPixel(x, y, r, g, b);
        }
    }
}

// BlitTo()：把画布整体拷贝到外部缓冲（通常是喂给 robot.SetImageSrc）
void Canvas::BlitTo(uint8_t* dst) const
{
    for (int i = 0; i < EB_LCD_W * EB_LCD_H * 3; i++)
        dst[i] = px[i];       // 逐字节拷贝（格式相同，直接搬）
}
