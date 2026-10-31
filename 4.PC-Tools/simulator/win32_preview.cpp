#include "win32_preview.h"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <string>
#include <vector>

namespace
{
constexpr int WIN_W = 470;          // 圆屏区 300 + 仪表区 170
constexpr int WIN_H = 310;
constexpr int LCD_CX = 150, LCD_CY = 155, LCD_R = 128;   // 圆屏中心/半径

VirtualElectronBot* g_robot = nullptr;
std::string g_title;

uint8_t g_screenBgr[EB_FRAME_SIZE];  // GDI 需要 BGR 序
float g_servo[6], g_setPts[6];
CRITICAL_SECTION g_cs;

void CopyFrameRgbToBgr(const uint8_t* src, uint8_t* dst)
{
    for (uint32_t i = 0; i < EB_FRAME_SIZE; i += 3)
    {
        dst[i]     = src[i + 2];
        dst[i + 1] = src[i + 1];
        dst[i + 2] = src[i];
    }
}

void DrawJointGauges(HDC hdc, const RECT& rc)
{
    const int trackW = rc.right - rc.left - 10;
    const int rowH = (rc.bottom - rc.top) / 6;

    HFONT font = CreateFontW(15, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, font);

    for (int j = 0; j < 6; j++)
    {
        const int y = rc.top + j * rowH;
        const int cy = y + rowH / 2;

        // 名称 + 数值
        wchar_t label[48];
        swprintf_s(label, L"%S  %+.1f/%+.1f",
                   VirtualElectronBot::JOINT_NAME[j], g_servo[j], g_setPts[j]);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(30, 30, 30));
        TextOutW(hdc, rc.left + 4, y + 2, label, (int) wcslen(label));

        // 轨道
        const int trackY = cy + 8;
        RECT bar = { rc.left + 4, trackY, rc.left + 4 + trackW, trackY + 6 };
        FillRect(hdc, &bar, (HBRUSH) GetStockObject(LTGRAY_BRUSH));

        const float lo = VirtualElectronBot::JOINT_MIN[j];
        const float hi = VirtualElectronBot::JOINT_MAX[j];
        const float span = hi - lo;

        // 设定值游标（细白线）
        int sx = bar.left + (int) ((g_setPts[j] - lo) / span * trackW);
        RECT setBar = { sx - 1, trackY - 2, sx + 1, trackY + 8 };
        FillRect(hdc, &setBar, (HBRUSH) (COLOR_WINDOW + 1));

        // 舵机实际角度指针（主指示）
        int px = bar.left + (int) ((g_servo[j] - lo) / span * trackW);
        RECT posBar = { px - 3, trackY - 4, px + 3, trackY + 10 };
        HBRUSH br = CreateSolidBrush(RGB(0, 150, 136));
        FillRect(hdc, &posBar, br);
        DeleteObject(br);
    }

    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
        case WM_CREATE:
            SetTimer(hwnd, 1, 33, nullptr);   // ~30fps
            return 0;

        case WM_TIMER:
            if (g_robot)
            {
                uint8_t tmp[EB_FRAME_SIZE];
                g_robot->GetVirtualScreen(tmp);
                g_robot->GetServoAngles(g_servo);
                g_robot->GetJointSetPoints(g_setPts);
                EnterCriticalSection(&g_cs);
                CopyFrameRgbToBgr(tmp, g_screenBgr);
                LeaveCriticalSection(&g_cs);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // 背景板（桌面灰色）
            RECT full = { 0, 0, WIN_W, WIN_H };
            HBRUSH bg = CreateSolidBrush(RGB(238, 240, 243));
            FillRect(hdc, &full, bg);
            DeleteObject(bg);

            // ---- 虚拟圆屏 ----
            {
                // 屏幕外圈（模拟边框）
                HBRUSH ring = CreateSolidBrush(RGB(60, 60, 60));
                HBRUSH old = (HBRUSH) SelectObject(hdc, ring);
                HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 60, 60));
                HPEN oldPen = (HPEN) SelectObject(hdc, pen);
                Ellipse(hdc, LCD_CX - LCD_R - 6, LCD_CY - LCD_R - 6,
                        LCD_CX + LCD_R + 6, LCD_CY + LCD_R + 6);
                DeleteObject(SelectObject(hdc, old));
                DeleteObject(SelectObject(hdc, oldPen));

                // 圆形剪裁区域内贴 240x240 位图（RGB888 -> BGR 后 StretchDIBits）
                EnterCriticalSection(&g_cs);
                BITMAPINFO bmi = {};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = EB_SCREEN_WIDTH;
                bmi.bmiHeader.biHeight = -EB_SCREEN_HEIGHT;  // top-down
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 24;
                bmi.bmiHeader.biCompression = BI_RGB;

                HRGN clip = CreateEllipticRgn(LCD_CX - LCD_R, LCD_CY - LCD_R,
                                               LCD_CX + LCD_R, LCD_CY + LCD_R);
                SelectClipRgn(hdc, clip);
                StretchDIBits(hdc,
                              LCD_CX - LCD_R, LCD_CY - LCD_R, 2 * LCD_R, 2 * LCD_R,
                              0, 0, EB_SCREEN_WIDTH, EB_SCREEN_HEIGHT,
                              g_screenBgr, &bmi, DIB_RGB_COLORS, SRCCOPY);
                SelectClipRgn(hdc, nullptr);
                DeleteObject(clip);
                LeaveCriticalSection(&g_cs);
            }

            // ---- 关节仪表 ----
            RECT gaugeRc = { 300, 10, WIN_W - 4, WIN_H - 10 };
            DrawJointGauges(hdc, gaugeRc);

            // 标题小字
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(120, 120, 120));
            TextOutA(hdc, 8, 4, "GC9A01 240x240 (virtual)", 25);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;   // 避免闪烁

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

DWORD WINAPI PreviewThread(LPVOID)
{
    InitializeCriticalSection(&g_cs);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = PreviewWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) (COLOR_BTNFACE + 1);
    wc.lpszClassName = L"EBPreviewWnd";
    RegisterClassW(&wc);

    int wTitle = MultiByteToWideChar(CP_UTF8, 0, g_title.c_str(), -1, nullptr, 0);
    std::wstring title(wTitle, 0);
    MultiByteToWideChar(CP_UTF8, 0, g_title.c_str(), -1, &title[0], wTitle);

    HWND hwnd = CreateWindowExW(0, L"EBPreviewWnd", title.c_str(),
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, WIN_H,
                                nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteCriticalSection(&g_cs);
    return 0;
}
} // namespace

void RobotPreviewWindow::Open(VirtualElectronBot* _robot, const char* _title)
{
    g_robot = _robot;
    g_title = _title;
    CreateThread(nullptr, 0, PreviewThread, nullptr, 0, nullptr);
}
