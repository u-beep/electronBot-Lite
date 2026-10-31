#include "ebanim.h"
#include "expression.h"
#include "canvas.h"
#include <cstdio>
#include <cstring>

EbAnim EbAnim::Capture(ExpressionRenderer& renderer,
                       const ExpressionAnim& anim,
                       int framesToCapture, int fps)
{
    EbAnim result;
    result.frames.resize(framesToCapture);
    Canvas c;
    const float stepMs = 1000.0f / fps;
    for (int i = 0; i < framesToCapture; i++)
    {
        renderer.RenderFrame(anim, i * stepMs, c);
        c.BlitTo(result.frames[i].pixels);
        result.frames[i].durationMs = (uint16_t) stepMs;
    }
    return result;
}

bool EbAnim::Save(const std::string& path) const
{
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "wb") != 0 || !fp)
        return false;

    const char magic[8] = { 'E', 'B', 'A', 'N', 'I', 'M', '1', '\0' };
    fwrite(magic, 1, 8, fp);
    uint32_t n = (uint32_t) frames.size();
    fwrite(&n, 4, 1, fp);
    uint16_t w = 240, h = 240;
    fwrite(&w, 2, 1, fp);
    fwrite(&h, 2, 1, fp);

    for (const auto& f : frames)
    {
        fwrite(&f.durationMs, 2, 1, fp);
        fwrite(f.pixels, 1, sizeof(f.pixels), fp);
    }
    fclose(fp);
    return true;
}

EbAnim EbAnim::Load(const std::string& path)
{
    EbAnim result;
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "rb") != 0 || !fp)
        return result;

    char magic[8] = {};
    if (fread(magic, 1, 8, fp) != 8 || memcmp(magic, "EBANIM1", 7) != 0)
    {
        fclose(fp);
        return result;
    }

    uint32_t n = 0;
    uint16_t w = 0, h = 0;
    if (fread(&n, 4, 1, fp) != 1 || fread(&w, 2, 1, fp) != 1 ||
        fread(&h, 2, 1, fp) != 1 || w != 240 || h != 240 || n == 0 || n > 10000)
    {
        fclose(fp);
        return result;
    }

    result.frames.resize(n);
    for (uint32_t i = 0; i < n; i++)
    {
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

uint32_t EbAnim::TotalDurationMs() const
{
    uint32_t total = 0;
    for (const auto& f : frames)
        total += f.durationMs;
    return total;
}
