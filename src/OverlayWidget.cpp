#include "OverlayWidget.hpp"
#include <algorithm>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")

namespace {
    OverlayWidget* g_pWidget = nullptr;

    bool IsAutoStartEnabled() {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD type = 0;
            LONG res = RegQueryValueExW(hKey, L"HardwareTaskbar", nullptr, &type, nullptr, nullptr);
            RegCloseKey(hKey);
            return (res == ERROR_SUCCESS);
        }
        return false;
    }

    void ToggleAutoStart() {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            if (IsAutoStartEnabled()) {
                RegDeleteValueW(hKey, L"HardwareTaskbar");
            } else {
                wchar_t exePath[MAX_PATH] = {0};
                if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
                    std::wstring cmd = L"\"" + std::wstring(exePath) + L"\"";
                    RegSetValueExW(hKey, L"HardwareTaskbar", 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(cmd.c_str()),
                                  static_cast<DWORD>((cmd.length() + 1) * sizeof(wchar_t)));
                }
            }
            RegCloseKey(hKey);
        }
    }
}

OverlayWidget::OverlayWidget() {
    g_pWidget = this;
}

OverlayWidget::~OverlayWidget() {
    Destroy();
    if (g_pWidget == this) {
        g_pWidget = nullptr;
    }
}

bool OverlayWidget::Initialize(HINSTANCE hInstance, HWND hWndNotifyTarget) {
    m_hInstance = hInstance;
    m_hWndNotifyTarget = hWndNotifyTarget;

    // Attach to interactive desktop station to ensure Shell_TrayWnd visibility
    HWINSTA hWinSta = OpenWindowStationW(L"winsta0", FALSE, MAXIMUM_ALLOWED);
    if (hWinSta) {
        SetProcessWindowStation(hWinSta);
        HDESK hDesk = OpenDesktopW(L"default", 0, FALSE, MAXIMUM_ALLOWED);
        if (hDesk) {
            SetThreadDesktop(hDesk);
        }
    }

    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr) == Gdiplus::Ok) {
        m_gdiplusInitialized = true;
    }

    // Register overlay window class
    const wchar_t CLASS_NAME[] = L"HardwareTaskbar_OverlayWidgetClass";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = OverlayWidget::WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassExW(&wc);

    // Create layered, transparent, frameless, topmost popup window
    m_hWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        CLASS_NAME,
        L"Hardware Taskbar Overlay",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hWnd) {
        return false;
    }

    // Initialize Common Controls for Tooltips
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    SetupTooltip();

    // Hook taskbar location changes to synchronize repositioning
    m_hWinEventHook = SetWinEventHook(
        EVENT_OBJECT_LOCATIONCHANGE,
        EVENT_OBJECT_LOCATIONCHANGE,
        nullptr,
        &OverlayWidget::WinEventProc,
        0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    // Initial render and display
    Render();
    ShowWindow(m_hWnd, SW_SHOWNOACTIVATE);

    return true;
}

void OverlayWidget::Destroy() {
    if (m_hWinEventHook) {
        UnhookWinEvent(m_hWinEventHook);
        m_hWinEventHook = nullptr;
    }

    if (m_hWndTooltip && IsWindow(m_hWndTooltip)) {
        DestroyWindow(m_hWndTooltip);
        m_hWndTooltip = nullptr;
    }

    if (m_hWnd && IsWindow(m_hWnd)) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }

    if (m_gdiplusInitialized) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusInitialized = false;
    }
}

void OverlayWidget::SetupTooltip() {
    m_hWndTooltip = CreateWindowExW(
        WS_EX_TOPMOST,
        TOOLTIPS_CLASSW,
        nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        m_hWnd, nullptr, m_hInstance, nullptr
    );

    if (m_hWndTooltip) {
        SetWindowPos(m_hWndTooltip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        TOOLINFOW ti = {};
        ti.cbSize = sizeof(TOOLINFOW);
        ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
        ti.hwnd = m_hWnd;
        ti.uId = reinterpret_cast<UINT_PTR>(m_hWnd);
        ti.lpszText = const_cast<LPWSTR>(L"Hardware Taskbar Monitor");

        SendMessageW(m_hWndTooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        SendMessageW(m_hWndTooltip, TTM_SETMAXTIPWIDTH, 0, 450);
        SendMessageW(m_hWndTooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 200);
        SendMessageW(m_hWndTooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 8000);
    }
}

void OverlayWidget::UpdateTooltipText() {
    if (!m_hWndTooltip || !IsWindow(m_hWndTooltip)) {
        return;
    }

    wchar_t tipBuf[512] = {0};
    if (m_lastMetrics.gpu.isAvailable) {
        if (m_lastMetrics.gpu.vramTotalGb > 0.0) {
            swprintf_s(tipBuf,
                L"CPU: %u%% | %s (%u Cores)\n"
                L"RAM: %.1f / %.1f GB (%u%%)\n"
                L"GPU: %u%% | %u°C | %s\nVRAM: %.1f / %.1f GB",
                m_lastMetrics.cpu.usagePercent, m_lastMetrics.cpu.cpuName.c_str(), m_lastMetrics.cpu.logicalCores,
                m_lastMetrics.ram.usedGb, m_lastMetrics.ram.totalGb, m_lastMetrics.ram.usagePercent,
                m_lastMetrics.gpu.utilizationPercent, m_lastMetrics.gpu.temperatureC,
                m_lastMetrics.gpu.deviceName.c_str(), m_lastMetrics.gpu.vramUsedGb, m_lastMetrics.gpu.vramTotalGb
            );
        } else {
            swprintf_s(tipBuf,
                L"CPU: %u%% | %s (%u Cores)\n"
                L"RAM: %.1f / %.1f GB (%u%%)\n"
                L"GPU: %u%% | %u°C | %s",
                m_lastMetrics.cpu.usagePercent, m_lastMetrics.cpu.cpuName.c_str(), m_lastMetrics.cpu.logicalCores,
                m_lastMetrics.ram.usedGb, m_lastMetrics.ram.totalGb, m_lastMetrics.ram.usagePercent,
                m_lastMetrics.gpu.utilizationPercent, m_lastMetrics.gpu.temperatureC,
                m_lastMetrics.gpu.deviceName.c_str()
            );
        }
    } else {
        swprintf_s(tipBuf,
            L"CPU: %u%% | %s (%u Cores)\n"
            L"RAM: %.1f / %.1f GB (%u%%)\n"
            L"GPU: Driver Not Detected / NVML Unavailable",
            m_lastMetrics.cpu.usagePercent, m_lastMetrics.cpu.cpuName.c_str(), m_lastMetrics.cpu.logicalCores,
            m_lastMetrics.ram.usedGb, m_lastMetrics.ram.totalGb, m_lastMetrics.ram.usagePercent
        );
    }

    TOOLINFOW ti = {};
    ti.cbSize = sizeof(TOOLINFOW);
    ti.hwnd = m_hWnd;
    ti.uId = reinterpret_cast<UINT_PTR>(m_hWnd);
    ti.lpszText = tipBuf;

    SendMessageW(m_hWndTooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
}

void OverlayWidget::UpdateMetrics(const HardwareMetrics& metrics) {
    m_lastMetrics = metrics;
    Render();
}

void OverlayWidget::Reposition() {
    Render();
}

void OverlayWidget::SetClickThrough(bool enabled) {
    m_clickThrough = enabled;
}

void OverlayWidget::Render() {
    if (!m_hWnd || !IsWindow(m_hWnd) || !m_gdiplusInitialized) {
        return;
    }

    // Determine current DPI scaling
    UINT dpi = GetDpiForWindow(m_hWnd);
    if (dpi == 0) dpi = 96;
    float scale = static_cast<float>(dpi) / 96.0f;

    int width = static_cast<int>(m_baseWidth * scale);
    int height = static_cast<int>(m_baseHeight * scale);

    // Locate Windows Taskbar and System Tray
    HWND hTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hTaskbar || !IsWindowVisible(hTaskbar)) {
        return;
    }

    RECT rcTaskbar = {};
    if (!GetWindowRect(hTaskbar, &rcTaskbar)) {
        return;
    }

    int taskbarH = rcTaskbar.bottom - rcTaskbar.top;
    int widgetH = std::min(height, taskbarH - 4);
    if (widgetH < 22) widgetH = 22;

    HWND hTrayNotify = FindWindowExW(hTaskbar, nullptr, L"TrayNotifyWnd", nullptr);
    RECT rcNotify = {};
    bool hasNotify = false;
    if (hTrayNotify && IsWindowVisible(hTrayNotify)) {
        if (GetWindowRect(hTrayNotify, &rcNotify)) {
            hasNotify = true;
        }
    }

    // Compute dock position directly left of the System Tray icons
    int widgetX = 0;
    int widgetY = rcTaskbar.top + (taskbarH - widgetH) / 2;

    if (hasNotify && rcNotify.left > rcTaskbar.left) {
        widgetX = rcNotify.left - width - 8;
    } else {
        widgetX = rcTaskbar.right - width - 12;
    }

    if (widgetX < rcTaskbar.left + 8) {
        widgetX = rcTaskbar.left + 8;
    }

    // Create 32-bit PARGB in-memory canvas
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -widgetH; // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    if (!hBmp) {
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hBmp);

    {
        Gdiplus::Bitmap bmp(width, widgetH, width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(pvBits));
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));

        // 1. Fluent semi-transparent dark pill background (#141720 with ~85% opacity)
        Gdiplus::REAL r = 7.0f * scale;
        if (r > widgetH / 2.0f) r = widgetH / 2.0f;
        Gdiplus::REAL w = static_cast<Gdiplus::REAL>(width);
        Gdiplus::REAL h = static_cast<Gdiplus::REAL>(widgetH);

        Gdiplus::GraphicsPath path;
        path.AddArc(0.5f, 0.5f, r * 2.0f, r * 2.0f, 180.0f, 90.0f);
        path.AddArc(w - 1.0f - r * 2.0f, 0.5f, r * 2.0f, r * 2.0f, 270.0f, 90.0f);
        path.AddArc(w - 1.0f - r * 2.0f, h - 1.0f - r * 2.0f, r * 2.0f, r * 2.0f, 0.0f, 90.0f);
        path.AddArc(0.5f, h - 1.0f - r * 2.0f, r * 2.0f, r * 2.0f, 90.0f, 90.0f);
        path.CloseFigure();

        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(220, 20, 23, 32));
        g.FillPath(&bgBrush, &path);

        // Subtle Fluent border (#FFFFFF with 15% opacity)
        Gdiplus::Pen borderPen(Gdiplus::Color(38, 255, 255, 255), 1.0f);
        g.DrawPath(&borderPen, &path);

        // 2. Typography & Brushes
        Gdiplus::REAL fontValPx = (widgetH <= 24) ? (10.0f * scale) : (11.5f * scale);
        Gdiplus::Font fontVal(L"Segoe UI", fontValPx, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font fontSep(L"Segoe UI", 10.0f * scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

        Gdiplus::SolidBrush sepBrush(Gdiplus::Color(70, 255, 255, 255));
        Gdiplus::SolidBrush cpuBrush(Gdiplus::Color(255, 255, 140, 0));   // Orange #FF8C00
        Gdiplus::SolidBrush ramBrush(Gdiplus::Color(255, 59, 130, 246));   // Blue #3B82F6
        Gdiplus::SolidBrush gpuBrush(m_lastMetrics.gpu.isAvailable ? Gdiplus::Color(255, 16, 185, 129) : Gdiplus::Color(200, 160, 170, 185)); // Emerald #10B981

        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        float segW = static_cast<float>(width) / 3.0f;

        // Draw CPU Segment
        std::wstring cpuText = L"CPU " + std::to_wstring(m_lastMetrics.cpu.usagePercent) + L"%";
        Gdiplus::RectF rectCpu(2.0f, 0.0f, segW - 2.0f, h);
        g.DrawString(cpuText.c_str(), -1, &fontVal, rectCpu, &sf, &cpuBrush);

        // Separator 1
        Gdiplus::RectF rectSep1(segW - 5.0f, 0.0f, 10.0f, h);
        g.DrawString(L"|", -1, &fontSep, rectSep1, &sf, &sepBrush);

        // Draw RAM Segment
        std::wstring ramText = L"RAM " + std::to_wstring(m_lastMetrics.ram.usagePercent) + L"%";
        Gdiplus::RectF rectRam(segW + 2.0f, 0.0f, segW - 4.0f, h);
        g.DrawString(ramText.c_str(), -1, &fontVal, rectRam, &sf, &ramBrush);

        // Separator 2
        Gdiplus::RectF rectSep2(segW * 2.0f - 5.0f, 0.0f, 10.0f, h);
        g.DrawString(L"|", -1, &fontSep, rectSep2, &sf, &sepBrush);

        // Draw GPU Segment
        std::wstring gpuText;
        if (m_lastMetrics.gpu.isAvailable) {
            gpuText = L"GPU " + std::to_wstring(m_lastMetrics.gpu.utilizationPercent) + L"%";
        } else {
            gpuText = L"GPU --";
        }
        Gdiplus::RectF rectGpu(segW * 2.0f + 2.0f, 0.0f, segW - 2.0f, h);
        g.DrawString(gpuText.c_str(), -1, &fontVal, rectGpu, &sf, &gpuBrush);
    }

    // 3. Atomically update position and alpha surface
    POINT ptDest = { widgetX, widgetY };
    SIZE szDest = { width, widgetH };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    UpdateLayeredWindow(m_hWnd, hdcScreen, &ptDest, &szDest, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    m_lastPos = ptDest;
    m_lastSize = szDest;
    m_lastTaskbarRect = rcTaskbar;
    m_lastNotifyRect = rcNotify;

    // Clean up GDI handles immediately
    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    UpdateTooltipText();
}

void OverlayWidget::ShowContextMenu(POINT pt, uint32_t currentIntervalMs) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) {
        return;
    }

    constexpr UINT POS_END = static_cast<UINT>(-1);

    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_STRING | MF_DISABLED, ID_MENU_HEADER, L"Hardware Taskbar Widget");
    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

    HMENU hIntervalSub = CreatePopupMenu();
    InsertMenuW(hIntervalSub, POS_END, MF_BYPOSITION | MF_STRING | (currentIntervalMs == 1000 ? MF_CHECKED : MF_UNCHECKED),
                ID_MENU_INTERVAL_1S, L"1 Second (Fast)");
    InsertMenuW(hIntervalSub, POS_END, MF_BYPOSITION | MF_STRING | (currentIntervalMs == 2000 ? MF_CHECKED : MF_UNCHECKED),
                ID_MENU_INTERVAL_2S, L"2 Seconds (Balanced)");
    InsertMenuW(hIntervalSub, POS_END, MF_BYPOSITION | MF_STRING | (currentIntervalMs == 5000 ? MF_CHECKED : MF_UNCHECKED),
                ID_MENU_INTERVAL_5S, L"5 Seconds (Low Power)");
    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(hIntervalSub), L"Polling Interval");

    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_STRING | (m_clickThrough ? MF_CHECKED : MF_UNCHECKED),
                ID_MENU_CLICKTHROUGH, L"Click-Through Mode");
    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_STRING, ID_MENU_TASKMGR, L"Open Task Manager");
    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_STRING | (IsAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED),
                ID_MENU_STARTUP, L"Start with Windows");

    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_STRING, ID_MENU_EXIT, L"Exit");

    SetForegroundWindow(m_hWnd);
    TrackPopupMenuEx(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, m_hWnd, nullptr);
    PostMessageW(m_hWnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

LRESULT CALLBACK OverlayWidget::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCHITTEST: {
            if (g_pWidget && g_pWidget->m_clickThrough) {
                return HTTRANSPARENT;
            }
            return HTCLIENT;
        }

        case WM_RBUTTONUP: {
            if (g_pWidget) {
                POINT pt;
                GetCursorPos(&pt);
                // Forward command to notify target or show menu directly
                if (g_pWidget->m_hWndNotifyTarget) {
                    PostMessageW(g_pWidget->m_hWndNotifyTarget, WM_APP_TRAYNOTIFY, 0, WM_RBUTTONUP);
                } else {
                    g_pWidget->ShowContextMenu(pt, 1000);
                }
            }
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }

        case WM_COMMAND: {
            if (g_pWidget && g_pWidget->m_hWndNotifyTarget) {
                SendMessageW(g_pWidget->m_hWndNotifyTarget, WM_COMMAND, wParam, lParam);
            }
            return 0;
        }

        case WM_DESTROY: {
            return 0;
        }

        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
}

void CALLBACK OverlayWidget::WinEventProc(HWINEVENTHOOK /*hHook*/, DWORD event, HWND hwnd,
                                        LONG idObject, LONG idChild,
                                        DWORD /*idEventThread*/, DWORD /*dwmsEventTime*/) {
    if (event == EVENT_OBJECT_LOCATIONCHANGE && idObject == OBJID_WINDOW && idChild == CHILDID_SELF) {
        HWND hTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
        HWND hNotify = hTaskbar ? FindWindowExW(hTaskbar, nullptr, L"TrayNotifyWnd", nullptr) : nullptr;
        if (hwnd == hTaskbar || hwnd == hNotify) {
            if (g_pWidget) {
                g_pWidget->Reposition();
            }
        }
    }
}
