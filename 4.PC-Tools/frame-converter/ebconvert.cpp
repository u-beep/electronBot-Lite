// ============================================================================
// ebconvert - 图片/GIF -> ElectronBot 240x240 RGB888 帧流转换器
//
// 方向 2（表情/动画内容工坊）的输入端工具：
//   ebconvert in.gif                       -> in.ebanim（GIF 动画逐帧合成）
//   ebconvert in.png -o face.ebanim        -> 单帧动画
//   ebconvert in.gif --preview first.bmp   -> 额外导出首帧 BMP 预览
//   ebconvert in.jpg --cover --bg 0,0,0    -> 满屏裁剪，黑底
//
// * 解码用 Windows WIC：PNG/JPG/BMP/TIFF/GIF，零第三方依赖
// * GIF 动画：实现帧矩形偏移、透明混合、disposal 0/1/2/3 完整合成
// * 输出 .ebanim（emoji-studio/ebanim.h 格式，Studio / sample_virtual 可播放）
// * 缩放默认 contain（保比例 + --bg 底色居中）；--cover 为满屏裁剪
// ============================================================================

#include <windows.h>
#include <wincodec.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "emoji-studio/ebanim.h"
#include "common/eb_screen.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::ComPtr;

static const int OUT_W = 240;
static const int OUT_H = 240;

// ---------------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------------
static void Die(const char* _msg)
{
    fprintf(stderr, "[error] %s\n", _msg);
    exit(1);
}

static const bool g_trace = getenv("EBTRACE") != nullptr;
#define TRACE(_s) do { if (g_trace) fprintf(stderr, "[trace] %s\n", _s); } while (0)

static std::string WideToNarrow(const std::wstring& _w)
{
    int n = WideCharToMultiByte(CP_ACP, 0, _w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_ACP, 0, _w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

static bool QueryU32(IWICMetadataQueryReader* _qr, const wchar_t* _name, uint32_t* _out)
{
    if (!_qr)
        return false;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    if (FAILED(_qr->GetMetadataByName(_name, &pv)))
        return false;
    bool ok = false;
    if (pv.vt == VT_UI1) { *_out = pv.bVal; ok = true; }
    else if (pv.vt == VT_UI2) { *_out = pv.uiVal; ok = true; }
    else if (pv.vt == VT_UI4) { *_out = pv.ulVal; ok = true; }
    PropVariantClear(&pv);
    return ok;
}

// ---------------------------------------------------------------------------
// 写 24bit BMP 预览（自底向上 + BGR）
// ---------------------------------------------------------------------------
static bool SaveBmpPreview(const std::string& _path, const uint8_t* _rgb888)
{
    FILE* fp = nullptr;
    if (fopen_s(&fp, _path.c_str(), "wb") != 0 || !fp)
        return false;

    const int row = OUT_W * 3;
    const int pad = (4 - row % 4) % 4;
    const uint32_t bodySize = (uint32_t) (row + pad) * OUT_H;

    uint8_t header[54] = {};
    header[0] = 'B'; header[1] = 'M';
    *(uint32_t*) &header[2] = 54 + bodySize;
    *(uint32_t*) &header[10] = 54;
    *(uint32_t*) &header[14] = 40;
    *(int32_t*) &header[18] = OUT_W;
    *(int32_t*) &header[22] = OUT_H;
    *(uint16_t*) &header[26] = 1;
    *(uint16_t*) &header[28] = 24;
    *(uint32_t*) &header[34] = bodySize;
    fwrite(header, 1, 54, fp);

    std::vector<uint8_t> line(row + pad, 0);
    for (int y = OUT_H - 1; y >= 0; y--)
    {
        const uint8_t* src = _rgb888 + y * row;
        for (int x = 0; x < OUT_W; x++)
        {
            line[x * 3 + 0] = src[x * 3 + 2];
            line[x * 3 + 1] = src[x * 3 + 1];
            line[x * 3 + 2] = src[x * 3 + 0];
        }
        fwrite(line.data(), 1, row + pad, fp);
    }
    fclose(fp);
    return true;
}

// ---------------------------------------------------------------------------
// GIF GCE（图形控制扩展）直接解析
// 实测本机 WIC 不通过查询接口暴露 GIF GCE（delay/disposal/透明索引），
// 但 GIF 块结构非常简单，直接扫文件最可靠：
//   0x21 0xF9 len(=4) packed delay(u16) transIdx 0x00
//   packed: bit0 透明标志, bit2-4 disposal
// ---------------------------------------------------------------------------
struct GceData
{
    uint32_t delayCs = 0;    // 单位 1/100 秒
    uint32_t disposal = 0;
    int transIdx = -1;       // -1 = 无透明
    bool valid = false;
};

static bool ReadFileBytes(const std::wstring& _path, std::vector<uint8_t>* _out)
{
    HANDLE h = CreateFileW(_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 13)
    {
        CloseHandle(h);
        return false;
    }
    _out->resize((size_t) sz.QuadPart);
    DWORD rd = 0;
    const BOOL ok = ReadFile(h, _out->data(), (DWORD) _out->size(), &rd, nullptr);
    CloseHandle(h);
    return ok && rd == _out->size();
}

static std::vector<GceData> ParseGifGces(const std::vector<uint8_t>& _f)
{
    std::vector<GceData> frames;
    size_t i = 6;                              // 跳过 GIF89a
    const uint8_t lsdPacked = _f[i + 4];
    i += 7;                                    // 跳过逻辑屏幕描述符
    if (lsdPacked & 0x80)
        i += 3 * (2u << (lsdPacked & 7));      // 跳过全局色表

    GceData pending;
    bool hasPending = false;
    while (i < _f.size())
    {
        const uint8_t b = _f[i++];
        if (b == 0x21)                         // 扩展块
        {
            if (i >= _f.size())
                break;
            const uint8_t label = _f[i++];
            if (label == 0xF9 && i + 4 < _f.size() && _f[i] == 4)
            {
                pending.valid = true;
                pending.disposal = (_f[i + 1] >> 2) & 7;
                pending.delayCs = _f[i + 2] | ((uint32_t) _f[i + 3] << 8);
                pending.transIdx = (_f[i + 1] & 1) ? (int) _f[i + 4] : -1;
                hasPending = true;
            }
            while (i < _f.size() && _f[i] != 0) // 跳过子块
                i += 1 + _f[i];
            i++;
        }
        else if (b == 0x2C)                    // 图像描述符
        {
            if (i + 9 > _f.size())
                break;
            const uint8_t ipacked = _f[i + 8];
            i += 9;
            GceData g;
            g.valid = hasPending;
            if (hasPending)
                g = pending;
            frames.push_back(g);
            hasPending = false;
            if (ipacked & 0x80)                // 局部色表
                i += 3 * (2u << (ipacked & 7));
            if (i < _f.size())
                i++;                           // LZW 最小码长
            while (i < _f.size() && _f[i] != 0)
                i += 1 + _f[i];
            i++;
        }
        else if (b == 0x3B)                    // 结束
            break;
    }
    return frames;
}

// ---------------------------------------------------------------------------
// 解码单帧为 32bpp BGRA 缓冲
// GIF 帧原生为 8bppIndexed：WIC 把调色板中透明项的 alpha 置 0（GCE 已被
// 解码器消化），据此还原逐像素透明度；GCE 的 delay/disposal 元数据要在
// 像素解码之后才查询得到（WIC 懒解析）。
// ---------------------------------------------------------------------------
static bool DecodeFrameBgra(IWICImagingFactory* _fac, IWICBitmapFrameDecode* _frame,
                            int _gceTransIdx,
                            std::vector<uint8_t>* _out, uint32_t* _w, uint32_t* _h)
{
    _frame->GetSize(_w, _h);
    if (*_w == 0 || *_h == 0)
        return false;

    WICPixelFormatGUID pf;
    _frame->GetPixelFormat(&pf);

    if (pf == GUID_WICPixelFormat8bppIndexed)   // GIF / PNG-8 路径
    {
        std::vector<uint8_t> idx((size_t) (*_w) * (*_h));
        if (FAILED(_frame->CopyPixels(nullptr, *_w, (UINT) idx.size(), idx.data())))
            return false;

        // 取活动调色板（GIF：有局部表用局部表，否则全局表）
        std::vector<WICColor> colors(256, 0xFF000000);
        ComPtr<IWICPalette> pal;
        if (SUCCEEDED(_fac->CreatePalette(&pal)) &&
            SUCCEEDED(_frame->CopyPalette(pal.Get())))
        {
            UINT count = 0;
            pal->GetColors(256, colors.data(), &count);
            if (g_trace)
            {
                fprintf(stderr, "[pal] count=%u:", count);
                for (UINT i = 0; i < count && i < 8; i++)
                    fprintf(stderr, " %08X", colors[i]);
                fprintf(stderr, "\n");
            }
        }

        _out->resize((size_t) (*_w) * (*_h) * 4);
        for (size_t i = 0; i < idx.size(); i++)
        {
            const uint8_t index = idx[i];
            const WICColor c = colors[index];
            uint8_t* p = _out->data() + i * 4;
            p[0] = (uint8_t) (c);         // B (WICColor = 0xAARRGGBB)
            p[1] = (uint8_t) (c >> 8);    // G
            p[2] = (uint8_t) (c >> 16);   // R
            p[3] = (uint8_t) (c >> 24);   // WIC 已把透明项 alpha 置 0
            if (_gceTransIdx >= 0 && (int) index == _gceTransIdx)
                p[3] = 0;                 // GCE 显式透明优先
        }
        return true;
    }

    // 通用路径（PNG-24 / JPG / 32bpp 等直接交给 WIC 转换）
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(_fac->CreateFormatConverter(&conv)))
        return false;
    if (FAILED(conv->Initialize(_frame, GUID_WICPixelFormat32bppBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom)))
        return false;

    _out->resize((size_t) (*_w) * (*_h) * 4);
    return SUCCEEDED(conv->CopyPixels(nullptr, *_w * 4,
                                      (UINT) _out->size(), _out->data()));
}

// ---------------------------------------------------------------------------
// 合成画布（BGRA） -> 缩放居中/裁剪到 240x240 RGB888
// ---------------------------------------------------------------------------
static bool ScaleCanvasToFrame(IWICImagingFactory* _fac,
                               const std::vector<uint8_t>& _canvasBgra,
                               uint32_t _cw, uint32_t _ch,
                               bool _cover, const uint8_t _bg[3],
                               uint8_t* _outRgb888)
{
    int dstW, dstH;
    if (_cover)
    {
        const double s = fmax((double) OUT_W / _cw, (double) OUT_H / _ch);
        dstW = (int) ceil(_cw * s);
        dstH = (int) ceil(_ch * s);
    }
    else
    {
        const double s = fmin((double) OUT_W / _cw, (double) OUT_H / _ch);
        dstW = (int) fmax(1.0, floor(_cw * s));
        dstH = (int) fmax(1.0, floor(_ch * s));
    }

    ComPtr<IWICBitmap> bmp;
    if (FAILED(_fac->CreateBitmapFromMemory(_cw, _ch, GUID_WICPixelFormat32bppBGRA,
                                            _cw * 4, (UINT) _canvasBgra.size(),
                                            (WICInProcPointer) _canvasBgra.data(), &bmp)))
        return false;

    // 极端缩小分两步，避免 Fant 一次缩太狠丢行
    ComPtr<IWICBitmapSource> src = bmp;
    if (_cw / (double) dstW > 4.0 || _ch / (double) dstH > 4.0)
    {
        ComPtr<IWICBitmapScaler> half;
        if (FAILED(_fac->CreateBitmapScaler(&half)))
            return false;
        if (FAILED(half->Initialize(src.Get(), _cw / 2 + 1, _ch / 2 + 1,
                                    WICBitmapInterpolationModeLinear)))
            return false;
        src = half;
    }

    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(_fac->CreateBitmapScaler(&scaler)))
        return false;
    if (FAILED(scaler->Initialize(src.Get(), dstW, dstH, WICBitmapInterpolationModeFant)))
        return false;

    std::vector<uint8_t> scaled((size_t) dstW * dstH * 4);
    if (FAILED(scaler->CopyPixels(nullptr, dstW * 4, (UINT) scaled.size(), scaled.data())))
        return false;

    // 背景填充 + 居中贴图（BGRA -> RGB888，透明像素按 alpha 落到背景色）
    for (int i = 0; i < OUT_W * OUT_H; i++)
    {
        _outRgb888[i * 3 + 0] = _bg[0];
        _outRgb888[i * 3 + 1] = _bg[1];
        _outRgb888[i * 3 + 2] = _bg[2];
    }
    const int ox = (OUT_W - dstW) / 2, oy = (OUT_H - dstH) / 2;
    for (int y = 0; y < dstH; y++)
    {
        const int dy = oy + y;
        if (dy < 0 || dy >= OUT_H)
            continue;
        for (int x = 0; x < dstW; x++)
        {
            const int dx = ox + x;
            if (dx < 0 || dx >= OUT_W)
                continue;
            const uint8_t* p = &scaled[((size_t) y * dstW + x) * 4];
            const uint8_t a = p[3];
            uint8_t* d = _outRgb888 + ((size_t) dy * OUT_W + dx) * 3;
            if (a >= 250)
            {
                d[0] = p[2]; d[1] = p[1]; d[2] = p[0];
            }
            else if (a > 0)
            {
                d[0] = (uint8_t) ((p[2] * a + _bg[0] * (255 - a)) >> 8);
                d[1] = (uint8_t) ((p[1] * a + _bg[1] * (255 - a)) >> 8);
                d[2] = (uint8_t) ((p[0] * a + _bg[2] * (255 - a)) >> 8);
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
static void FillRectBgra(std::vector<uint8_t>* _c, uint32_t _cw, uint32_t _ch,
                         uint32_t _x0, uint32_t _y0, uint32_t _x1, uint32_t _y1,
                         const uint8_t _bg[3])
{
    for (uint32_t y = _y0; y < _y1 && y < _ch; y++)
        for (uint32_t x = _x0; x < _x1 && x < _cw; x++)
        {
            uint8_t* p = _c->data() + ((size_t) y * _cw + x) * 4;
            p[0] = _bg[2]; p[1] = _bg[1]; p[2] = _bg[0]; p[3] = 255;
        }
}

// ---------------------------------------------------------------------------
int main()
{
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argvW || argc < 2)
    {
        printf("ebconvert - 图片/GIF -> 240x240 RGB888 帧流\n"
               "用法: ebconvert <input> [-o out.ebanim] [--preview out.bmp]\n"
               "      [--bg R,G,B] [--cover]\n"
               "支持输入: PNG / JPG / BMP / TIFF / GIF(动画)\n");
        return 0;
    }

    std::wstring inPath = argvW[1];
    std::wstring outPath, previewPath;
    uint8_t bg[3] = { 0, 0, 0 };
    bool cover = false;

    for (int i = 2; i < argc; i++)
    {
        if (!wcscmp(argvW[i], L"-o") && i + 1 < argc) outPath = argvW[++i];
        else if (!wcscmp(argvW[i], L"--preview") && i + 1 < argc) previewPath = argvW[++i];
        else if (!wcscmp(argvW[i], L"--cover")) cover = true;
        else if (!wcscmp(argvW[i], L"--bg") && i + 1 < argc)
        {
            int r, g, b;
            if (swscanf_s(argvW[++i], L"%d,%d,%d", &r, &g, &b) == 3)
            {
                bg[0] = (uint8_t) r; bg[1] = (uint8_t) g; bg[2] = (uint8_t) b;
            }
        }
    }

    // 默认输出名：输入名换 .ebanim 后缀
    if (outPath.empty())
    {
        outPath = inPath;
        const size_t dot = outPath.find_last_of(L'.');
        const size_t sep = outPath.find_last_of(L"/\\");
        if (dot != std::wstring::npos && (sep == std::wstring::npos || dot > sep))
            outPath.resize(dot);
        outPath += L".ebanim";
    }

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ComPtr<IWICImagingFactory> fac;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&fac))))
        Die("WIC factory 创建失败");

    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(fac->CreateDecoderFromFilename(inPath.c_str(), nullptr, GENERIC_READ,
                                              WICDecodeMetadataCacheOnDemand, &dec)))
        Die("无法打开输入文件");

    UINT frameCount = 0;
    dec->GetFrameCount(&frameCount);
    if (frameCount == 0)
        Die("文件中没有图像帧");

    // GIF 逻辑画布尺寸（/logscrdesc），查不到则退回第一帧尺寸
    uint32_t canvasW = 0, canvasH = 0;
    {
        ComPtr<IWICMetadataQueryReader> qr;
        if (SUCCEEDED(dec->GetMetadataQueryReader(&qr)))
        {
            QueryU32(qr.Get(), L"/logscrdesc/width", &canvasW);
            QueryU32(qr.Get(), L"/logscrdesc/height", &canvasH);
        }
    }

    // GIF GCE 直解（WIC 不暴露这些元数据）
    std::vector<uint8_t> fileBytes;
    std::vector<GceData> gces;
    if (ReadFileBytes(inPath, &fileBytes))
        gces = ParseGifGces(fileBytes);
    if (g_trace)
        fprintf(stderr, "[gce] parsed %u GCE entries\n", (unsigned) gces.size());

    EbAnim anim;
    std::vector<uint8_t> canvas;   // GIF 合成画布（BGRA）
    std::vector<uint8_t> saved;    // disposal 3：绘制前快照
    uint8_t firstFrameBuf[EB_FRAME_SIZE] = {};
    uint32_t prevDisposal = 0, prevX0 = 0, prevY0 = 0, prevX1 = 0, prevY1 = 0;
    bool firstFrame = true;

    for (UINT f = 0; f < frameCount; f++)
    {
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(dec->GetFrame(f, &frame)))
            Die("解码帧失败");

        // 1) 解码本帧像素（透明索引来自 GCE 直解，调色板 alpha 兜底）
        std::vector<uint8_t> framePix;
        uint32_t fw = 0, fh = 0;
        TRACE("decode frame");
        const GceData& gce = f < gces.size() ? gces[f] : GceData{};
        if (!DecodeFrameBgra(fac.Get(), frame.Get(),
                             gce.valid ? gce.transIdx : -1, &framePix, &fw, &fh))
            Die("帧解码失败");
        TRACE("frame decoded");

        // 2) 读帧元数据：矩形走 WIC（实测可用），GCE 走文件直解
        uint32_t fx = 0, fy = 0, disposal = 0, delayMs = 100;
        ComPtr<IWICMetadataQueryReader> qr;
        frame->GetMetadataQueryReader(&qr);
        const bool haveRect = QueryU32(qr.Get(), L"/imgdesc/left", &fx)
                           && QueryU32(qr.Get(), L"/imgdesc/top", &fy);
        uint32_t mdW = 0, mdH = 0;
        if (QueryU32(qr.Get(), L"/imgdesc/width", &mdW) && mdW) fw = mdW;
        if (QueryU32(qr.Get(), L"/imgdesc/height", &mdH) && mdH) fh = mdH;
        if (gce.valid)
        {
            disposal = gce.disposal;
            delayMs = gce.delayCs * 10;
        }
        if (delayMs < 20)
            delayMs = 20;   // 屏幕刷新上限 ~50fps
        if (g_trace)
            fprintf(stderr, "[meta] rect=%d(%u,%u %ux%u) delay=%ums disposal=%u gce=%d\n",
                    (int) haveRect, fx, fy, fw, fh, delayMs, disposal,
                    gce.valid ? 1 : 0);

        // 3) 应用上一帧的 disposal 语义
        if (!firstFrame)
        {
            if (prevDisposal == 2)
                FillRectBgra(&canvas, canvasW, canvasH, prevX0, prevY0, prevX1, prevY1, bg);
            else if (prevDisposal == 3)
                canvas = saved;
        }

        // 4) 初始化合成画布
        if (firstFrame)
        {
            if (!haveRect || canvasW == 0 || canvasH == 0)
            {
                canvasW = fw;
                canvasH = fh;
            }
            canvas.assign((size_t) canvasW * canvasH * 4, 0);
            FillRectBgra(&canvas, canvasW, canvasH, 0, 0, canvasW, canvasH, bg);
        }

        // 5) 快照当前画布（供本帧 disposal 3 还原），再混合本帧（src-over）
        saved = canvas;
        prevX0 = fx; prevY0 = fy;
        prevX1 = fx + fw; prevY1 = fy + fh;
        for (uint32_t y = 0; y < fh; y++)
        {
            const uint32_t cy = fy + y;
            if (cy >= canvasH)
                break;
            for (uint32_t x = 0; x < fw; x++)
            {
                const uint32_t cx = fx + x;
                if (cx >= canvasW)
                    break;
                const uint8_t* s = &framePix[((size_t) y * fw + x) * 4];
                uint8_t* d = &canvas[((size_t) cy * canvasW + cx) * 4];
                const uint8_t a = s[3];
                if (a >= 250)
                {
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
                }
                else if (a > 0)
                {
                    d[0] = (uint8_t) ((s[0] * a + d[0] * (255 - a)) >> 8);
                    d[1] = (uint8_t) ((s[1] * a + d[1] * (255 - a)) >> 8);
                    d[2] = (uint8_t) ((s[2] * a + d[2] * (255 - a)) >> 8);
                    d[3] = 255;
                }
            }
        }

        // 6) 缩放输出本帧
        TRACE("scale frame");
        AnimFrame af;
        af.durationMs = (uint16_t) delayMs;
        if (!ScaleCanvasToFrame(fac.Get(), canvas, canvasW, canvasH, cover, bg, af.pixels))
            Die("缩放输出失败");
        if (firstFrame)
            memcpy(firstFrameBuf, af.pixels, EB_FRAME_SIZE);
        anim.frames.push_back(af);
        firstFrame = false;
        prevDisposal = disposal;
    }

    // ---- 导出 ----
    TRACE("export");
    const std::string outNarrow = WideToNarrow(outPath);
    if (!anim.Save(outNarrow))
        Die("写入 .ebanim 失败");
    TRACE("ebanim saved");

    if (!previewPath.empty())
    {
        const std::string pv = WideToNarrow(previewPath);
        if (SaveBmpPreview(pv, firstFrameBuf))
            fprintf(stderr, "[preview] %s\n", pv.c_str());
        TRACE("preview saved");
    }

    fprintf(stderr, "[done] %u 帧 -> %s  (%.1fs, %.1fMB)\n",
            (unsigned) anim.frames.size(), outNarrow.c_str(),
            anim.TotalDurationMs() / 1000.0f,
            anim.frames.size() * (EB_FRAME_SIZE + 2) / 1048576.0);
    TRACE("before cleanup");    // 先释放所有 COM 对象，再卸载 COM 套间（顺序反了会在退出时崩溃）
    dec.Reset();
    fac.Reset();
    LocalFree(argvW);
    CoUninitialize();
    TRACE("cleanup done");
    return 0;
}
