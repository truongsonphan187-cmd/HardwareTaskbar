#pragma once

#include "Common.hpp"
#include <gdiplus.h>

class OverlayWidget {
public:
    OverlayWidget();
    ~OverlayWidget();

    // Non-copyable
    OverlayWidget(const OverlayWidget&) = delete;
    OverlayWidget& operator=(const OverlayWidget&) = delete;

    bool Initialize(HINSTANCE hInstance, HWND hWndNotifyTarget);
    void Destroy();

    void UpdateMetrics(const HardwareMetrics& metrics);
    void Reposition();
    void CheckAndReposition();

    bool IsExplicitlyMoving() const { return m_explicitlyMoving; }
    HWND GetHwnd() const { return m_hWnd; }

    static bool IsWindowAbove(HWND hWndAbove, HWND hWndBelow);
    bool IsFullScreenActive() const;
    void CheckFullscreenState();

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void CALLBACK WinEventProc(HWINEVENTHOOK hHook, DWORD event, HWND hwnd,
                                     LONG idObject, LONG idChild,
                                     DWORD idEventThread, DWORD dwmsEventTime);

    bool CalculateBounds(int& widgetX, int& widgetY, int& width, int& widgetH, RECT& rcTaskbar, RECT& rcNotify);
    void Render(bool positionChanged);

    HINSTANCE m_hInstance = nullptr;
    HWND m_hWnd = nullptr;
    HWND m_hWndNotifyTarget = nullptr;
    HWINEVENTHOOK m_hWinEventHook = nullptr;

    ULONG_PTR m_gdiplusToken = 0;
    bool m_gdiplusInitialized = false;

    bool m_explicitlyMoving = false;
    bool m_hasInitialPosition = false;
    bool m_hiddenForFullscreen = false;
    int m_baseWidth = 240;
    int m_baseHeight = 28;

    HardwareMetrics m_lastMetrics;
    RECT m_lastTaskbarRect = {0, 0, 0, 0};
    RECT m_lastNotifyRect = {0, 0, 0, 0};
    POINT m_lastPos = {0, 0};
    SIZE m_lastSize = {0, 0};
};
