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

    void SetClickThrough(bool enabled);
    bool IsClickThrough() const { return m_clickThrough; }

    HWND GetHwnd() const { return m_hWnd; }
    void ShowContextMenu(POINT pt, uint32_t currentIntervalMs);

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void CALLBACK WinEventProc(HWINEVENTHOOK hHook, DWORD event, HWND hwnd,
                                     LONG idObject, LONG idChild,
                                     DWORD idEventThread, DWORD dwmsEventTime);

    void Render();
    void SetupTooltip();
    void UpdateTooltipText();

    HINSTANCE m_hInstance = nullptr;
    HWND m_hWnd = nullptr;
    HWND m_hWndTooltip = nullptr;
    HWND m_hWndNotifyTarget = nullptr;
    HWINEVENTHOOK m_hWinEventHook = nullptr;

    ULONG_PTR m_gdiplusToken = 0;
    bool m_gdiplusInitialized = false;

    bool m_clickThrough = false;
    int m_baseWidth = 240;
    int m_baseHeight = 28;

    HardwareMetrics m_lastMetrics;
    RECT m_lastTaskbarRect = {0, 0, 0, 0};
    RECT m_lastNotifyRect = {0, 0, 0, 0};
    POINT m_lastPos = {0, 0};
    SIZE m_lastSize = {0, 0};
};
