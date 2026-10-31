// ============================================================================
// 文件：win32_preview.cpp —— 模拟器的独立预览窗口
//
// 功能：开一个小窗口，左边画"虚拟圆屏"（240x240 显存内容），
//       右边画 6 个关节仪表（绿条=舵机实际角度，白线=设定角度）。
//
// 新手导读——Win32 GUI 的固定套路（所有 Windows 桌面程序都长这样）：
//   1. 注册窗口类（RegisterClassW）：告诉系统"我的窗口长什么样、消息给谁处理"
//   2. 创建窗口（CreateWindowExW）并显示
//   3. 消息循环（GetMessageW/DispatchMessageW）：Windows 不停地把
//      "发生了什么"（鼠标移动、需要重绘、定时器到点…）作为"消息"投给窗口
//   4. 窗口过程函数（WndProc）：用 switch 处理各种消息
//   5. 重绘在 WM_PAINT 消息里做
// ============================================================================

#include "win32_preview.h"

// Win32 编程惯例：启用 Unicode 版本的 API（处理中文/宽字符）
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <string>
#include <vector>

// 匿名 namespace：里面的东西只在本文件可见（相当于"文件私有"）
namespace
{
constexpr int WIN_W = 470;          // 圆屏区 300 + 仪表区 170
constexpr int WIN_H = 310;
constexpr int LCD_CX = 150, LCD_CY = 155, LCD_R = 128;   // 圆屏中心/半径

// ---- 全局状态（窗口过程是普通函数，拿不到对象指针，所以用全局变量传数据） ----
VirtualElectronBot* g_robot = nullptr;       // 指向被预览的模拟器对象
std::string g_title;                         // 窗口标题

uint8_t g_screenBgr[EB_FRAME_SIZE];  // 显存快照（GDI 需要 BGR 序，见下方转换函数）
float g_servo[6], g_setPts[6];       // 舵机实际角 / 设定角（仪表用）
CRITICAL_SECTION g_cs;               // 临界区：Windows 版的"互斥锁"（保护 g_screenBgr）

// RGB888 -> BGR 序转换：GDI 的位图按"蓝绿红"存，我们的显存是"红绿蓝"
// 每像素 3 字节，把第 0、2 字节交换即可
void CopyFrameRgbToBgr(const uint8_t* src, uint8_t* dst)
{
    for (uint32_t i = 0; i < EB_FRAME_SIZE; i += 3)   // 每次跳 3 字节 = 一个像素
    {
        dst[i]     = src[i + 2];    // B <- 原来的蓝
        dst[i + 1] = src[i + 1];    // G 不动
        dst[i + 2] = src[i];        // R <- 原来的红
    }
}

// ---------------------------------------------------------------------------
// DrawJointGauges()：在右侧画 6 个关节仪表（名称 + 数值 + 轨道 + 双指针）
// ---------------------------------------------------------------------------
void DrawJointGauges(HDC hdc, const RECT& rc)
{
    const int trackW = rc.right - rc.left - 10;       // 轨道宽度
    const int rowH = (rc.bottom - rc.top) / 6;        // 每个关节占的高度

    // 创建 15px 的 Segoe UI 字体并"选入"设备上下文（之后的文字都用它画）
    HFONT font = CreateFontW(15, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, font);        // 记住旧字体，画完好还原

    for (int j = 0; j < 6; j++)
    {
        const int y = rc.top + j * rowH;              // 本行的顶部 y 坐标
        const int cy = y + rowH / 2;                  // 本行的中心 y

        // 名称 + 数值（%+.1f = 带正负号保留 1 位小数）
        wchar_t label[48];
        swprintf_s(label, L"%S  %+.1f/%+.1f",
                   VirtualElectronBot::JOINT_NAME[j], g_servo[j], g_setPts[j]);
        SetBkMode(hdc, TRANSPARENT);                  // 文字背景透明（不遮轨道）
        SetTextColor(hdc, RGB(30, 30, 30));
        TextOutW(hdc, rc.left + 4, y + 2, label, (int) wcslen(label));

        // 轨道（浅灰长条）
        const int trackY = cy + 8;
        RECT bar = { rc.left + 4, trackY, rc.left + 4 + trackW, trackY + 6 };
        FillRect(hdc, &bar, (HBRUSH) GetStockObject(LTGRAY_BRUSH));
        // GetStockObject：拿系统预置的画笔/画刷（不用手动创建销毁）

        const float lo = VirtualElectronBot::JOINT_MIN[j];   // 本关节角度范围
        const float hi = VirtualElectronBot::JOINT_MAX[j];
        const float span = hi - lo;

        // 设定值游标（细白线）：角度 -> 轨道上的像素位置
        int sx = bar.left + (int) ((g_setPts[j] - lo) / span * trackW);
        RECT setBar = { sx - 1, trackY - 2, sx + 1, trackY + 8 };
        FillRect(hdc, &setBar, (HBRUSH) (COLOR_WINDOW + 1));

        // 舵机实际角度指针（青绿色粗条，主指示）
        int px = bar.left + (int) ((g_servo[j] - lo) / span * trackW);
        RECT posBar = { px - 3, trackY - 4, px + 3, trackY + 10 };
        HBRUSH br = CreateSolidBrush(RGB(0, 150, 136));
        FillRect(hdc, &posBar, br);
        DeleteObject(br);                             // 自己创建的 GDI 对象要删
    }

    SelectObject(hdc, oldFont);                       // 还原旧字体
    DeleteObject(font);                               // 删掉自建字体
}

// ---------------------------------------------------------------------------
// PreviewWndProc()：窗口过程——系统把所有消息发到这里
// ---------------------------------------------------------------------------
LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
        case WM_CREATE:                       // 窗口刚创建
            SetTimer(hwnd, 1, 33, nullptr);   // 定时器：每 33ms 触发一次 ≈ 30fps
            return 0;

        case WM_TIMER:                        // 定时器到点：抓最新数据
            if (g_robot)
            {
                uint8_t tmp[EB_FRAME_SIZE];
                g_robot->GetVirtualScreen(tmp);           // 从模拟器抓显存快照
                g_robot->GetServoAngles(g_servo);
                g_robot->GetJointSetPoints(g_setPts);
                EnterCriticalSection(&g_cs);              // 进临界区（加锁）
                CopyFrameRgbToBgr(tmp, g_screenBgr);      // 转成 GDI 要的 BGR
                LeaveCriticalSection(&g_cs);              // 出临界区（解锁）
            }
            InvalidateRect(hwnd, nullptr, FALSE);         // 请求重绘整个窗口
            return 0;

        case WM_PAINT:                        // 需要重绘（系统或自己请求的）
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);  // 获取"画布"（设备上下文）

            // 背景板（桌面灰色）
            RECT full = { 0, 0, WIN_W, WIN_H };
            HBRUSH bg = CreateSolidBrush(RGB(238, 240, 243));
            FillRect(hdc, &full, bg);
            DeleteObject(bg);

            // ---- 虚拟圆屏 ----
            {
                // 屏幕外圈（模拟边框）：先画一个大灰圆
                HBRUSH ring = CreateSolidBrush(RGB(60, 60, 60));
                HBRUSH old = (HBRUSH) SelectObject(hdc, ring);
                HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 60, 60));
                HPEN oldPen = (HPEN) SelectObject(hdc, pen);
                Ellipse(hdc, LCD_CX - LCD_R - 6, LCD_CY - LCD_R - 6,
                        LCD_CX + LCD_R + 6, LCD_CY + LCD_R + 6);
                DeleteObject(SelectObject(hdc, old));     // 还原并删除
                DeleteObject(SelectObject(hdc, oldPen));

                // 圆形剪裁区域内贴 240x240 位图（RGB888 -> BGR 后 StretchDIBits）
                EnterCriticalSection(&g_cs);
                BITMAPINFO bmi = {};                      // 描述位图格式的结构体
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = EB_SCREEN_WIDTH;
                bmi.bmiHeader.biHeight = -EB_SCREEN_HEIGHT;  // 负数 = 从上往下存
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 24;            // 24 位 = RGB888
                bmi.bmiHeader.biCompression = BI_RGB;

                // 剪裁区域：椭圆之外的地方画了也会被裁掉
                HRGN clip = CreateEllipticRgn(LCD_CX - LCD_R, LCD_CY - LCD_R,
                                               LCD_CX + LCD_R, LCD_CY + LCD_R);
                SelectClipRgn(hdc, clip);
                // StretchDIBits：把内存位图拉伸贴到窗口（源 240x240 -> 目标 256x256）
                StretchDIBits(hdc,
                              LCD_CX - LCD_R, LCD_CY - LCD_R, 2 * LCD_R, 2 * LCD_R,
                              0, 0, EB_SCREEN_WIDTH, EB_SCREEN_HEIGHT,
                              g_screenBgr, &bmi, DIB_RGB_COLORS, SRCCOPY);
                SelectClipRgn(hdc, nullptr);              // 取消剪裁
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

            EndPaint(hwnd, &ps);              // 释放画布（必须与 BeginPaint 配对）
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;   // 返回 1 = "我自己画背景了"，避免系统先擦一遍造成闪烁

        case WM_DESTROY:                      // 窗口被关闭
            KillTimer(hwnd, 1);               // 停掉定时器
            PostQuitMessage(0);               // 让消息循环退出
            return 0;
    }
    // 没处理的消息交给系统默认处理（必须写，否则窗口行为异常）
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// PreviewThread()：预览窗口跑在独立线程里（不卡调用者的主循环）
// ---------------------------------------------------------------------------
DWORD WINAPI PreviewThread(LPVOID)
{
    InitializeCriticalSection(&g_cs);         // 初始化临界区

    // 1) 注册窗口类
    WNDCLASSW wc = {};
    wc.lpfnWndProc = PreviewWndProc;          // 消息处理函数
    wc.hInstance = GetModuleHandleW(nullptr); // 当前程序实例
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) (COLOR_BTNFACE + 1);
    wc.lpszClassName = L"EBPreviewWnd";
    RegisterClassW(&wc);

    // 2) 标题从 UTF-8（窄字符）转成 UTF-16（宽字符），供 CreateWindowW 使用
    int wTitle = MultiByteToWideChar(CP_UTF8, 0, g_title.c_str(), -1, nullptr, 0);
    std::wstring title(wTitle, 0);
    MultiByteToWideChar(CP_UTF8, 0, g_title.c_str(), -1, &title[0], wTitle);

    // 3) 创建并显示窗口
    HWND hwnd = CreateWindowExW(0, L"EBPreviewWnd", title.c_str(),
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, WIN_H,
                                nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // 4) 消息循环：窗口活着就一直转
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);               // 翻译键盘消息（惯例写法）
        DispatchMessageW(&msg);               // 投递给窗口过程
    }

    DeleteCriticalSection(&g_cs);
    return 0;
}
} // namespace

// ---------------------------------------------------------------------------
// RobotPreviewWindow::Open()：对外唯一入口——传模拟器对象和标题即可
// ---------------------------------------------------------------------------
void RobotPreviewWindow::Open(VirtualElectronBot* _robot, const char* _title)
{
    g_robot = _robot;                         // 全局指针交给窗口线程使用
    g_title = _title;
    CreateThread(nullptr, 0, PreviewThread, nullptr, 0, nullptr);   // 起 Win32 原生线程
}
