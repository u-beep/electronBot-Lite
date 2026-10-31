// ============================================================================
// 文件：ebanim.cpp —— .ebanim 动画文件格式的实现
//
// 新手导读——自己设计一个二进制文件格式只需要三件事：
//   1. 文件头：魔法数字（标识格式）+ 元信息（帧数/宽高）
//   2. 数据体：逐帧写 [帧时长][像素数据]
//   3. 读取时逐步校验（魔法对不对？宽高对不对？数字合理吗？）
// 全部是小端字节序（x86/ARM 都原生支持，跨平台读这一种就够了）
// ============================================================================

#include "ebanim.h"
#include "expression.h"
#include "canvas.h"
#include <cstdio>     // fopen/fwrite/fread（C 语言文件操作）
#include <cstring>    // memcmp（比较内存内容）

// ---------------------------------------------------------------------------
// Capture()：把一个表情动画"拍"成帧序列（像录屏一样）
// 原理：按固定时间间隔渲染 t = 0, 33, 66... 毫秒的画面，逐帧存下来
// ---------------------------------------------------------------------------
EbAnim EbAnim::Capture(ExpressionRenderer& renderer,
                       const ExpressionAnim& anim,
                       int framesToCapture, int fps)
{
    EbAnim result;
    result.frames.resize(framesToCapture);        // 预分配帧数组
    Canvas c;                                     // 复用同一块画布（省内存）
    const float stepMs = 1000.0f / fps;           // 帧间隔：30fps -> 33.3ms
    for (int i = 0; i < framesToCapture; i++)
    {
        renderer.RenderFrame(anim, i * stepMs, c);    // 渲染第 i 帧时刻的画面
        c.BlitTo(result.frames[i].pixels);            // 画布像素拷进帧结构
        result.frames[i].durationMs = (uint16_t) stepMs;   // 记录本帧该显示多久
    }
    return result;
}

// ---------------------------------------------------------------------------
// Save()：把动画写入 .ebanim 文件
// 文件布局：[8B 魔法"EBANIM1\0"][4B 帧数][2B 宽][2B 高] + N x [2B 时长][172800B 像素]
// ---------------------------------------------------------------------------
bool EbAnim::Save(const std::string& path) const
{
    FILE* fp = nullptr;
    // fopen_s 是 fopen 的安全版（MSVC 要求）；"wb" = 二进制写模式
    if (fopen_s(&fp, path.c_str(), "wb") != 0 || !fp)
        return false;                              // 文件打不开（路径错/被占用）

    const char magic[8] = { 'E', 'B', 'A', 'N', 'I', 'M', '1', '\0' };
    fwrite(magic, 1, 8, fp);                       // 魔法数字：读文件时验证格式用
    uint32_t n = (uint32_t) frames.size();
    fwrite(&n, 4, 1, fp);                          // 帧数（4 字节整数直接写内存）
    uint16_t w = 240, h = 240;
    fwrite(&w, 2, 1, fp);                          // 宽
    fwrite(&h, 2, 1, fp);                          // 高

    for (const auto& f : frames)                   // 逐帧写数据体
    {
        fwrite(&f.durationMs, 2, 1, fp);           // 本帧显示时长
        fwrite(f.pixels, 1, sizeof(f.pixels), fp); // 172800 字节像素
    }
    fclose(fp);                                    // 关文件（必须，否则数据可能没落盘）
    return true;
}

// ---------------------------------------------------------------------------
// Load()：读取 .ebanim 文件（Save 的逆过程，每步都做校验防坏文件）
// ---------------------------------------------------------------------------
EbAnim EbAnim::Load(const std::string& path)
{
    EbAnim result;
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "rb") != 0 || !fp)   // "rb" = 二进制读
        return result;                             // 返回空动画

    char magic[8] = {};
    // 先读魔法数字并比对——不是本格式的文件直接拒绝
    if (fread(magic, 1, 8, fp) != 8 || memcmp(magic, "EBANIM1", 7) != 0)
    {
        fclose(fp);
        return result;
    }

    uint32_t n = 0;
    uint16_t w = 0, h = 0;
    // 帧数/宽高都要合理：宽高必须 240，帧数 1..10000（防恶意超大数字撑爆内存）
    if (fread(&n, 4, 1, fp) != 1 || fread(&w, 2, 1, fp) != 1 ||
        fread(&h, 2, 1, fp) != 1 || w != 240 || h != 240 || n == 0 || n > 10000)
    {
        fclose(fp);
        return result;
    }

    result.frames.resize(n);
    for (uint32_t i = 0; i < n; i++)
    {
        // 逐帧读；任何一帧读不满（文件被截断）就整体作废
        if (fread(&result.frames[i].durationMs, 2, 1, fp) != 1 ||
            fread(result.frames[i].pixels, 1, 240 * 240 * 3, fp) != 240 * 240 * 3)
        {
            result.frames.clear();
            break;
        }
    }
    fclose(fp);
    return result;
}

// TotalDurationMs()：所有帧时长加起来 = 整个动画播完一圈要多少毫秒
uint32_t EbAnim::TotalDurationMs() const
{
    uint32_t total = 0;
    for (const auto& f : frames)                   // const auto& ：只读遍历，不拷贝
        total += f.durationMs;
    return total;
}
