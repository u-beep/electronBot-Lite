// ============================================================================
// ElectronBot Studio - 桌面机器人纯软件上位机（虚拟机器人模式）
//
// * 左侧：虚拟 GC9A01 圆屏实时预览（30fps 表情动画）
// * 右侧：6 关节滑块控制（范围与固件 Bsp/robot.h 一致）
// * 表情切换、舵机使能、FPS 状态
// * 后端为 VirtualElectronBot 模拟器 —— 未来可无缝切换真机 ElectronLowLevel
//   （把 VirtualElectronBot 换成 ElectronLowLevel 并链接 2.SDK 即可）
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "../simulator/eb_backend.h"
#include "../emoji-studio/expression.h"
#include "../emoji-studio/canvas.h"

#pragma comment(lib, "comctl32.lib")

// ---- 布局常量 ----
static constexpr int WIN_W = 960, WIN_H = 470;
static constexpr int LCD_CX = 160, LCD_CY = 225, LCD_R = 135;
static constexpr int PANEL_X = 530;   // 右侧控制面板起始 x

// ---- 控件 ID ----
enum CtlId
{
    IDC_SLIDER_BASE = 100,   // 100..105 六个关节
    IDC_BTN_EXPR = 200,      // 200..204 表情按钮
    IDC_CHK_ENABLE = 300,
};

struct App
{
    RobotBackend robot;
    ExpressionRenderer expr;
    int currentAnim = 0;
    float animTime = 0;
    bool servoEnable = false;
    float sliderAngles[6] = {0, 0, 0, 0, 0, 0};

    HWND sliders[6] = {};
    HWND sliderLabels[6] = {};
    HWND chkEnable = nullptr;
    wchar_t statusText[96] = L"FPS: --   Sync: --";   // 由 PaintScreen 绘制（双缓冲，不闪）

    // 渲染缓冲
    Canvas canvas;
    uint8_t screenBgr[EB_FRAME_SIZE];
    float servo[6], setPts[6];

    // FPS 统计
    DWORD fpsLastTick = 0;
    int fpsCounter = 0;
    int fpsValue = 0;
};

static App g_app;

static const std::vector<ExpressionAnim>& Anims() { return ExpressionRenderer::Builtin(); }

// ---------------------------------------------------------------------------
static void SyncSliderLabel(int j)
{
    wchar_t buf[64];
    swprintf_s(buf, L"%-12S %6.1f°",
               RobotBackend::JOINT_NAME[j], g_app.sliderAngles[j]);
    SetWindowTextW(g_app.sliderLabels[j], buf);
}

static void CreateControls(HWND hwnd, HINSTANCE hInst)
{
    // ---- 关节滑块（右侧两列 x 3 行）----
    for (int j = 0; j < 6; j++)
    {
        const int col = j / 3;
        const int row = j % 3;
        const int x = PANEL_X + col * 215;
        const int y = 60 + row * 110;

        g_app.sliderLabels[j] = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            x, y, 205, 20, hwnd, nullptr, hInst, nullptr);

        g_app.sliders[j] = CreateWindowW(TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
            x, y + 22, 205, 40, hwnd, (HMENU) (INT_PTR) (IDC_SLIDER_BASE + j), hInst, nullptr);

        // 映射：trackbar 0..1000 -> 关节 [min,max]
        SendMessageW(g_app.sliders[j], TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
        const float lo = RobotBackend::JOINT_MIN[j];
        const float hi = RobotBackend::JOINT_MAX[j];
        const float mid = (lo + hi) * 0.5f;
        g_app.sliderAngles[j] = mid;
        const int pos = (int) ((mid - lo) / (hi - lo) * 1000);
        SendMessageW(g_app.sliders[j], TBM_SETPOS, TRUE, pos);
        SyncSliderLabel(j);
    }

    // ---- 表情按钮（顶部一行）----
    const char* names[5] = { "Idle", "Happy", "Love", "Angry", "Sleepy" };
    for (int i = 0; i < 5; i++)
    {
        int wname = MultiByteToWideChar(CP_UTF8, 0, names[i], -1, nullptr, 0);
        std::wstring w(wname, 0);
        MultiByteToWideChar(CP_UTF8, 0, names[i], -1, &w[0], wname);
        CreateWindowW(L"BUTTON", w.c_str(),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            PANEL_X + i * 82, 8, 78, 34, hwnd, (HMENU) (INT_PTR) (IDC_BTN_EXPR + i), hInst, nullptr);
    }

    // ---- 舵机使能勾选框 ----
    g_app.chkEnable = CreateWindowW(L"BUTTON", L"Servo Enable",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            PANEL_X, 400, 130, 26, hwnd, (HMENU) IDC_CHK_ENABLE, hInst, nullptr);

    // 状态文字（FPS/Sync）改由 PaintScreen 直接绘制，不用 STATIC 控件，
    // 避免每秒 SetWindowText 触发的擦除+重绘闪烁。
}

// ---------------------------------------------------------------------------
static void Tick(void)
{
    const DWORD now = GetTickCount();
    static DWORD lastTick = 0;
    if (lastTick == 0) lastTick = now;
    const float dtMs = (float) (now - lastTick);
    lastTick = now;

    // 1) 表情动画渲染
    g_app.animTime += dtMs;
    g_app.expr.RenderFrame(Anims()[g_app.currentAnim], g_app.animTime, g_app.canvas);

    // 2) 喂给虚拟机器人 + 同步
    g_app.robot.SetImageSrc(g_app.canvas.px);
    g_app.robot.SetJointAngles(
        g_app.sliderAngles[0], g_app.sliderAngles[1], g_app.sliderAngles[2],
        g_app.sliderAngles[3], g_app.sliderAngles[4], g_app.sliderAngles[5],
        g_app.servoEnable);
    g_app.robot.Sync();

    // 3) FPS 统计
    g_app.fpsCounter++;
    if (now - g_app.fpsLastTick >= 1000)
    {
        g_app.fpsValue = g_app.fpsCounter;
        g_app.fpsCounter = 0;
        g_app.fpsLastTick = now;

        float servos[6], sets[6];
        g_app.robot.GetServoAngles(servos);
        g_app.robot.GetJointSetPoints(sets);
        wchar_t buf[96];
        swprintf_s(buf, L"FPS: %d   Sync: %u   Mode: %S%s", g_app.fpsValue,
                   g_app.robot.timeStamp, EB_BACKEND_MODE_NAME,
                   g_app.servoEnable ? L"" : L"   [舵机未使能，拖滑块只改设定值]");
        wcscpy_s(g_app.statusText, buf);
    }
    // 使能状态切换时立即刷新提示（不等 1s 周期）
    else if (g_app.statusText[0] && (wcsstr(g_app.statusText, L"[舵机未使能") != nullptr) != g_app.servoEnable)
    {
        wchar_t buf[96];
        swprintf_s(buf, L"FPS: %d   Sync: %u   Mode: %S%s", g_app.fpsValue,
                   g_app.robot.timeStamp, EB_BACKEND_MODE_NAME,
                   g_app.servoEnable ? L"" : L"   [舵机未使能，拖滑块只改设定值]");
        wcscpy_s(g_app.statusText, buf);
    }
}

// ---------------------------------------------------------------------------
static void PaintScreen(HWND hwnd, HDC hdc)
{
    // 背景
    RECT full = { 0, 0, WIN_W, WIN_H };
    HBRUSH bg = CreateSolidBrush(RGB(245, 246, 248));
    FillRect(hdc, &full, bg);
    DeleteObject(bg);

    // 标题
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 96, 100));
    HFONT bigFont = CreateFontW(22, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldF = SelectObject(hdc, bigFont);
    TextOutW(hdc, 12, 10, L"ElectronBot Studio", 19);
    SelectObject(hdc, oldF);
    DeleteObject(bigFont);

    // 屏幕外圈
    HBRUSH ring = CreateSolidBrush(RGB(55, 58, 64));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(55, 58, 64));
    HGDIOBJ oldB = SelectObject(hdc, ring), oldP = SelectObject(hdc, pen);
    Ellipse(hdc, LCD_CX - LCD_R - 6, LCD_CY - LCD_R - 6,
            LCD_CX + LCD_R + 6, LCD_CY + LCD_R + 6);
    DeleteObject(SelectObject(hdc, oldB));
    DeleteObject(SelectObject(hdc, oldP));

    // 虚拟显存 -> BGR
    {
        uint8_t tmp[EB_FRAME_SIZE];
        g_app.robot.GetVirtualScreen(tmp);
        for (uint32_t i = 0; i < EB_FRAME_SIZE; i += 3)
        {
            g_app.screenBgr[i] = tmp[i + 2];
            g_app.screenBgr[i + 1] = tmp[i + 1];
            g_app.screenBgr[i + 2] = tmp[i];
        }
    }

    // 圆形剪裁贴图
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = EB_SCREEN_WIDTH;
    bmi.bmiHeader.biHeight = -EB_SCREEN_HEIGHT;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    HRGN clip = CreateEllipticRgn(LCD_CX - LCD_R, LCD_CY - LCD_R,
                                   LCD_CX + LCD_R, LCD_CY + LCD_R);
    SelectClipRgn(hdc, clip);
    StretchDIBits(hdc, LCD_CX - LCD_R, LCD_CY - LCD_R, 2 * LCD_R, 2 * LCD_R,
                  0, 0, EB_SCREEN_WIDTH, EB_SCREEN_HEIGHT,
                  g_app.screenBgr, &bmi, DIB_RGB_COLORS, SRCCOPY);
    SelectClipRgn(hdc, nullptr);
    DeleteObject(clip);

    // ---- Robot View：机器人正面简视图，随关节反馈角实时动作 ----
    {
        const int px0 = 330, py0 = 50, px1 = 520, py1 = 462;
        RECT pv = { px0, py0, px1, py1 };
        HBRUSH panelBg = CreateSolidBrush(RGB(236, 239, 243));
        FillRect(hdc, &pv, panelBg);
        DeleteObject(panelBg);

        SetTextColor(hdc, RGB(0, 96, 100));
        HFONT capFont = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ capOld = SelectObject(hdc, capFont);
        TextOutW(hdc, px0 + 10, py0 + 6, L"Robot View", 10);
        SelectObject(hdc, capOld);
        DeleteObject(capFont);

        const float yaw   = g_app.servo[0];   // 头偏航 -15..15
        const float lRoll = g_app.servo[1], lPitch = g_app.servo[2];
        const float rRoll = g_app.servo[3], rPitch = g_app.servo[4];
        const float bodyY = g_app.servo[5];   // 腰 -90..90

        const int cx = (px0 + px1) / 2;
        const float sh = bodyY * 0.35f;       // 腰部剪切（上身偏移）
        const float hx = yaw * 2.0f;          // 头部水平偏移
        const COLORREF ink = RGB(55, 58, 64);

        // 地面阴影
        HBRUSH shBrush = CreateSolidBrush(RGB(216, 220, 225));
        HPEN nullPen = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ osb = SelectObject(hdc, shBrush), osn = SelectObject(hdc, nullPen);
        Ellipse(hdc, cx - 55, 412, cx + 55, 432);
        SelectObject(hdc, osb); SelectObject(hdc, osn);
        DeleteObject(shBrush); DeleteObject(nullPen);

        // 手臂（画在躯干后面）：肩部锚点，pitch 0=下垂 180=举直，roll 外张
        HPEN armPen = CreatePen(PS_SOLID, 8, ink);
        HGDIOBJ oa = SelectObject(hdc, armPen);
        const int shY = 208;
        const int slx = (int) (cx + sh - 34), srx = (int) (cx + sh + 34);
        {
            const float aL = (lPitch + lRoll) * 3.14159265f / 180.0f;
            MoveToEx(hdc, slx, shY, nullptr);
            LineTo(hdc, (int) (slx - sinf(aL) * 82), (int) (shY + cosf(aL) * 82));
            const float aR = (rPitch + rRoll) * 3.14159265f / 180.0f;
            MoveToEx(hdc, srx, shY, nullptr);
            LineTo(hdc, (int) (srx + sinf(aR) * 82), (int) (shY + cosf(aR) * 82));
        }
        SelectObject(hdc, oa);
        DeleteObject(armPen);

        // 躯干（梯形，随腰剪切）
        HPEN bodyPen = CreatePen(PS_SOLID, 3, ink);
        HBRUSH bodyFill = CreateSolidBrush(RGB(252, 252, 254));
        HGDIOBJ obp = SelectObject(hdc, bodyPen), obf = SelectObject(hdc, bodyFill);
        POINT torso[4] = {
            { (int) (cx - 30 + sh), 195 }, { (int) (cx + 30 + sh), 195 },
            { cx + 30, 345 }, { cx - 30, 345 } };
        Polygon(hdc, torso, 4);

        // 脖子 + 头
        const int hcx = (int) (cx + sh * 0.9f + hx), hcy = 138;
        MoveToEx(hdc, (int) (cx + sh * 0.9f), 178, nullptr);
        LineTo(hdc, hcx, hcy + 30);
        Ellipse(hdc, hcx - 40, hcy - 40, hcx + 40, hcy + 40);

        // 眼睛（随视线微移）
        HPEN nullPen2 = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ on2 = SelectObject(hdc, nullPen2);
        const int exo = (int) (hx * 0.4f);
        Ellipse(hdc, hcx - 18 + exo - 5, hcy - 8, hcx - 18 + exo + 5, hcy + 2);
        Ellipse(hdc, hcx + 18 + exo - 5, hcy - 8, hcx + 18 + exo + 5, hcy + 2);
        SelectObject(hdc, on2);
        DeleteObject(nullPen2);

        SelectObject(hdc, obp);                          // 恢复默认画笔
        SelectObject(hdc, GetStockObject(NULL_BRUSH));   // 摘下填充刷再删除
        DeleteObject(bodyPen); DeleteObject(bodyFill);
    }

    // 关节实时角度小字（屏幕下方）
    g_app.robot.GetServoAngles(g_app.servo);
    g_app.robot.GetJointSetPoints(g_app.setPts);
    HFONT sFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
    oldF = SelectObject(hdc, sFont);
    for (int j = 0; j < 6; j++)
    {
        wchar_t buf[64];
        swprintf_s(buf, L"J%d %-12S %6.1f -> %6.1f", j + 1,
                   RobotBackend::JOINT_NAME[j], g_app.setPts[j], g_app.servo[j]);
        SetTextColor(hdc, RGB(90, 90, 90));
        TextOutW(hdc, 12, 375 + j * 15, buf, (int) wcslen(buf));
    }

    // 状态文字（FPS / Sync / Mode）—— 在双缓冲里画，无闪烁
    SetTextColor(hdc, RGB(60, 60, 60));
    TextOutW(hdc, 670, 406, g_app.statusText, (int) wcslen(g_app.statusText));

    SelectObject(hdc, oldF);
    DeleteObject(sFont);
}

// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
        case WM_CREATE:
            CreateControls(hwnd, ((LPCREATESTRUCTW) lp)->hInstance);
            g_app.robot.Connect();
            SetTimer(hwnd, 1, 33, nullptr);
            return 0;

        case WM_TIMER:
            Tick();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_HSCROLL:
        {
            for (int j = 0; j < 6; j++)
            {
                if ((HWND) lp == g_app.sliders[j])
                {
                    const int pos = (int) SendMessageW(g_app.sliders[j], TBM_GETPOS, 0, 0);
                    const float lo = RobotBackend::JOINT_MIN[j];
                    const float hi = RobotBackend::JOINT_MAX[j];
                    g_app.sliderAngles[j] = lo + (hi - lo) * pos / 1000.0f;
                    SyncSliderLabel(j);
                }
            }
            return 0;
        }

        case WM_COMMAND:
        {
            const int id = LOWORD(wp);
            if (id >= IDC_BTN_EXPR && id < IDC_BTN_EXPR + 5)
            {
                g_app.currentAnim = id - IDC_BTN_EXPR;
                g_app.animTime = 0;
            }
            else if (id == IDC_CHK_ENABLE)
            {
                g_app.servoEnable = SendMessageW(g_app.chkEnable, BM_GETCHECK, 0, 0) == BST_CHECKED;
            }
            return 0;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // 双缓冲：全部内容先画进内存 DC，再一次 BitBlt 到屏幕。
            // 直接画屏幕 DC 会在每帧重绘时产生可见的中间状态（右侧面板闪烁）。
            RECT rc;
            GetClientRect(hwnd, &rc);
            const int cw = rc.right, ch = rc.bottom;
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, cw, ch);
            HGDIOBJ oldBmp = SelectObject(mem, bmp);
            PaintScreen(hwnd, mem);
            BitBlt(hdc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldBmp);
            DeleteObject(bmp);
            DeleteDC(mem);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            g_app.robot.SetJointAngles(0, 0, 0, 0, 0, 0, false);
            g_app.robot.Sync();
            g_app.robot.Disconnect();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH) (COLOR_BTNFACE + 1);
    wc.lpszClassName = L"EBStudioMain";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"EBStudioMain", L"ElectronBot Studio (Virtual)",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, WIN_H,
                                nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int) msg.wParam;
}
