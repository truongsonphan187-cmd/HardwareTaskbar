#include "OverlayWidget.hpp"
#include <algorithm>
#include <shellapi.h>
#include <dwmapi.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

namespace {
    OverlayWidget* g_pWidget = nullptr;
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

bool OverlayWidget::IsWindowAbove(HWND hWndAbove, HWND hWndBelow) {
    if (!hWndAbove || !hWndBelow || hWndAbove == hWndBelow) return false;
    HWND hCur = GetWindow(hWndAbove, GW_HWNDNEXT);
    while (hCur) {
        if (hCur == hWndBelow) return true;
        hCur = GetWindow(hCur, GW_HWNDNEXT);
    }
    return false;
}

bool OverlayWidget::IsFullScreenActive() const {
    // 1. Shell API user notification state check (detects exclusive D3D fullscreen games and presentation mode)
    QUERY_USER_NOTIFICATION_STATE state;
    if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
        if (state == QUNS_BUSY ||
            state == QUNS_RUNNING_D3D_FULL_SCREEN ||
            state == QUNS_PRESENTATION_MODE) {
            return true;
        }
    }

    // 2. Check foreground window bounds against monitor rect (detects borderless windowed games, F11 browser, media players)
    HWND hWndFg = GetForegroundWindow();
    if (!hWndFg || hWndFg == m_hWnd) {
        return false;
    }

    if (hWndFg == GetDesktopWindow() || hWndFg == GetShellWindow()) {
        return false;
    }

    wchar_t className[256] = {};
    if (GetClassNameW(hWndFg, className, 256)) {
        if (wcscmp(className, L"Progman") == 0 ||
            wcscmp(className, L"WorkerW") == 0 ||
            wcscmp(className, L"Shell_TrayWnd") == 0 ||
            wcscmp(className, L"Shell_SecondaryTrayWnd") == 0 ||
            wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0) {
            return false;
        }
    }

    HWND hTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    HMONITOR hMonFg = MonitorFromWindow(hWndFg, MONITOR_DEFAULTTONULL);
    HMONITOR hMonTaskbar = MonitorFromWindow(hTaskbar ? hTaskbar : m_hWnd, MONITOR_DEFAULTTONEAREST);

    if (hMonFg && hMonFg == hMonTaskbar) {
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoW(hMonFg, &mi)) {
            RECT rcFg = {};
            if (GetWindowRect(hWndFg, &rcFg)) {
                // Window covers or exceeds the taskbar's monitor
                if (rcFg.left <= mi.rcMonitor.left &&
                    rcFg.top <= mi.rcMonitor.top &&
                    rcFg.right >= mi.rcMonitor.right &&
                    rcFg.bottom >= mi.rcMonitor.bottom) {

                    if (IsWindowVisible(hWndFg) && !IsIconic(hWndFg)) {
                        DWORD cloaked = 0;
                        DwmGetWindowAttribute(hWndFg, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
                        if (!cloaked) {
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

void OverlayWidget::CheckFullscreenState() {
    if (!m_hWnd || !IsWindow(m_hWnd)) {
        return;
    }

    bool isFS = IsFullScreenActive();
    if (isFS && !m_hiddenForFullscreen) {
        m_hiddenForFullscreen = true;
        ShowWindow(m_hWnd, SW_HIDE);
    } else if (!isFS && m_hiddenForFullscreen) {
        m_hiddenForFullscreen = false;
        ShowWindow(m_hWnd, SW_SHOWNOACTIVATE);
        Reposition();
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

    // Register overlay window class (no CS_HREDRAW / CS_VREDRAW, no background brush to prevent any background erase)
    const wchar_t CLASS_NAME[] = L"HardwareTaskbar_OverlayWidgetClass";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = 0;
    wc.lpfnWndProc = OverlayWidget::WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassExW(&wc);

    // Locate Shell_TrayWnd to set it as the owner window
    HWND hTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);

    // Create the overlay as an OWNED WS_POPUP window:
    // In Win32, passing hTaskbar as hWndParent for a WS_POPUP window sets Shell_TrayWnd as its owner.
    // The OS window manager strictly guarantees that an owned window stays ABOVE its owner in Z-order,
    // preventing the taskbar from hiding the overlay when the user clicks anywhere on the taskbar.
    m_hWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        CLASS_NAME,
        L"Hardware Taskbar Overlay",
        WS_POPUP,
        0, 0, 0, 0,
        hTaskbar, nullptr, hInstance, this
    );

    if (!m_hWnd) {
        return false;
    }

    if (hTaskbar) {
        SetWindowLongPtrW(m_hWnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(hTaskbar));
    }

    // Hook taskbar location changes, foreground focus, and window size changes
    m_hWinEventHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_OBJECT_LOCATIONCHANGE,
        nullptr,
        &OverlayWidget::WinEventProc,
        0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    // Initial render and display
    Render(true);
    ShowWindow(m_hWnd, SW_SHOWNOACTIVATE);

    // Immediately verify if fullscreen is already active (e.g. game already running)
    CheckFullscreenState();

    return true;
}

void OverlayWidget::Destroy() {
    if (m_hWinEventHook) {
        UnhookWinEvent(m_hWinEventHook);
        m_hWinEventHook = nullptr;
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

bool OverlayWidget::CalculateBounds(int& widgetX, int& widgetY, int& width, int& widgetH, RECT& rcTaskbar, RECT& rcNotify) {
    HWND hTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hTaskbar || !IsWindowVisible(hTaskbar)) {
        return false;
    }

    if (!GetWindowRect(hTaskbar, &rcTaskbar)) {
        return false;
    }

    UINT dpi = GetDpiForWindow(m_hWnd);
    if (dpi == 0) dpi = 96;
    float scale = static_cast<float>(dpi) / 96.0f;

    width = static_cast<int>(m_baseWidth * scale);
    int height = static_cast<int>(m_baseHeight * scale);

    int taskbarH = rcTaskbar.bottom - rcTaskbar.top;
    widgetH = std::min(height, taskbarH - 4);
    if (widgetH < 22) widgetH = 22;

    HWND hTrayNotify = FindWindowExW(hTaskbar, nullptr, L"TrayNotifyWnd", nullptr);
    bool hasNotify = false;
    if (hTrayNotify && IsWindowVisible(hTrayNotify)) {
        if (GetWindowRect(hTrayNotify, &rcNotify)) {
            hasNotify = true;
        }
    }

    widgetY = rcTaskbar.top + (taskbarH - widgetH) / 2;

    if (hasNotify && rcNotify.left > rcTaskbar.left) {
        widgetX = rcNotify.left - width - 8;
    } else {
        widgetX = rcTaskbar.right - width - 12;
    }

    if (widgetX < rcTaskbar.left + 8) {
        widgetX = rcTaskbar.left + 8;
    }

    return true;
}

void OverlayWidget::UpdateMetrics(const HardwareMetrics& metrics) {
    m_lastMetrics = metrics;
    CheckFullscreenState();
    if (m_hiddenForFullscreen) {
        return; // Skip rendering layered window while in fullscreen mode to save CPU/GPU cycles
    }
    // Periodic hardware stat refresh: pass positionChanged = false
    Render(false);
}

void OverlayWidget::Reposition() {
    CheckFullscreenState();
    if (m_hiddenForFullscreen) {
        return;
    }
    Render(true);
}

void OverlayWidget::CheckAndReposition() {
    CheckFullscreenState();
    if (m_hiddenForFullscreen) {
        return;
    }

    int widgetX = 0, widgetY = 0, width = 0, widgetH = 0;
    RECT rcTaskbar = {}, rcNotify = {};

    if (!CalculateBounds(widgetX, widgetY, width, widgetH, rcTaskbar, rcNotify)) {
        return;
    }

    HWND hTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (hTaskbar && m_hWnd) {
        // Ensure Shell_TrayWnd remains the registered owner (in case Explorer restarted)
        HWND hOwner = reinterpret_cast<HWND>(GetWindowLongPtrW(m_hWnd, GWLP_HWNDPARENT));
        if (hOwner != hTaskbar) {
            SetWindowLongPtrW(m_hWnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(hTaskbar));
        }
    }

    // Verify overlay is strictly above Shell_TrayWnd in Z-order
    bool needZOrderFix = (hTaskbar && !IsWindowAbove(m_hWnd, hTaskbar));

    // Check if taskbar coordinates or widget dock coordinates actually changed or Z-order needs self-healing
    bool posChanged = (!m_hasInitialPosition ||
                       needZOrderFix ||
                       widgetX != m_lastPos.x ||
                       widgetY != m_lastPos.y ||
                       width != m_lastSize.cx ||
                       widgetH != m_lastSize.cy);

    if (!posChanged) {
        return;
    }

    // Only reposition when bounds have genuinely changed or Z-order needs self-healing
    m_lastTaskbarRect = rcTaskbar;
    m_lastNotifyRect = rcNotify;
    Render(true);
}

void OverlayWidget::Render(bool positionChanged) {
    if (!m_hWnd || !IsWindow(m_hWnd) || !m_gdiplusInitialized || m_hiddenForFullscreen) {
        return;
    }

    int widgetX = 0, widgetY = 0, width = 0, widgetH = 0;
    RECT rcTaskbar = {}, rcNotify = {};
    if (!CalculateBounds(widgetX, widgetY, width, widgetH, rcTaskbar, rcNotify)) {
        return;
    }

    HWND hTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (hTaskbar && !IsWindowAbove(m_hWnd, hTaskbar)) {
        positionChanged = true;
    }

    // If initial position hasn't been set yet or coordinates moved, mark positionChanged
    if (!m_hasInitialPosition || widgetX != m_lastPos.x || widgetY != m_lastPos.y ||
        width != m_lastSize.cx || widgetH != m_lastSize.cy) {
        positionChanged = true;
    }

    // Double-buffered 32-bit PARGB in-memory canvas
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

        UINT dpi = GetDpiForWindow(m_hWnd);
        if (dpi == 0) dpi = 96;
        float scale = static_cast<float>(dpi) / 96.0f;

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

    POINT ptSrc = { 0, 0 };
    SIZE szDest = { width, widgetH };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    if (positionChanged) {
        // Only call SetWindowPos when actual coordinates have changed or Z-order is being restored
        m_explicitlyMoving = true;
        SetWindowPos(m_hWnd, HWND_TOPMOST, widgetX, widgetY, width, widgetH,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        m_explicitlyMoving = false;

        POINT ptDest = { widgetX, widgetY };
        UpdateLayeredWindow(m_hWnd, NULL, &ptDest, &szDest, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

        m_lastPos = ptDest;
        m_lastSize = szDest;
        m_lastTaskbarRect = rcTaskbar;
        m_lastNotifyRect = rcNotify;
        m_hasInitialPosition = true;
    } else {
        // Pass NULL for pptDst so DWM does not recalculate desktop window geometry on every tick
        UpdateLayeredWindow(m_hWnd, NULL, NULL, &szDest, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    }

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

LRESULT CALLBACK OverlayWidget::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        // Handle WM_NCHITTEST and immediately return HTTRANSPARENT
        case WM_NCHITTEST: {
            return HTTRANSPARENT;
        }

        // Handle WM_MOUSEACTIVATE and return MA_NOACTIVATE
        case WM_MOUSEACTIVATE: {
            return MA_NOACTIVATE;
        }

        // Handle WM_WINDOWPOSCHANGING: if window is not explicitly being moved by our code,
        // prevent unwanted Z-order changes UNLESS the overlay is detected behind Shell_TrayWnd
        case WM_WINDOWPOSCHANGING: {
            auto* wp = reinterpret_cast<WINDOWPOS*>(lParam);
            if (wp) {
                wp->flags |= SWP_NOACTIVATE;
                if (!g_pWidget || !g_pWidget->IsExplicitlyMoving()) {
                    HWND hTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
                    if (hTaskbar && !IsWindowAbove(hWnd, hTaskbar)) {
                        wp->hwndInsertAfter = HWND_TOPMOST;
                        wp->flags &= ~SWP_NOZORDER;
                    } else {
                        wp->flags |= SWP_NOZORDER;
                    }
                }
            }
            return 0;
        }

        // Handle WM_ERASEBKGND by returning 1 to prevent background erasing
        case WM_ERASEBKGND: {
            return 1;
        }

        // Absorb WM_PAINT completely since window is rendered exclusively via UpdateLayeredWindow
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
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
    if (!g_pWidget) return;

    if (event == EVENT_SYSTEM_FOREGROUND) {
        g_pWidget->CheckFullscreenState();
        HWND hTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (hwnd == hTaskbar) {
            g_pWidget->CheckAndReposition();
        }
    } else if (event == EVENT_OBJECT_LOCATIONCHANGE && idObject == OBJID_WINDOW && idChild == CHILDID_SELF) {
        HWND hTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
        HWND hNotify = hTaskbar ? FindWindowExW(hTaskbar, nullptr, L"TrayNotifyWnd", nullptr) : nullptr;
        if (hwnd == hTaskbar || hwnd == hNotify) {
            g_pWidget->CheckAndReposition();
        } else {
            // A window might have toggled fullscreen size (e.g. F11 or video player)
            g_pWidget->CheckFullscreenState();
        }
    }
}
