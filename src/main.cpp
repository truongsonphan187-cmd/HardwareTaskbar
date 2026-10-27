#include "Common.hpp"
#include "HardwareMonitor.hpp"
#include "OverlayWidget.hpp"
#include <memory>

namespace {
    HardwareMonitor g_hwMonitor;
    OverlayWidget g_overlayWidget;
    UINT g_wmTaskbarCreated = 0;
    HWND g_hMainWnd = nullptr;
    NOTIFYICONDATAW g_nid = {};
    bool g_trayAdded = false;

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

    void ShowContextMenu(HWND hWnd, POINT pt) {
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return;

        constexpr UINT POS_END = static_cast<UINT>(-1);
        uint32_t currentInterval = g_hwMonitor.GetInterval();

        InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_STRING | MF_DISABLED, ID_MENU_HEADER, L"Hardware Taskbar Widget");
        InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

        HMENU hIntervalSub = CreatePopupMenu();
        InsertMenuW(hIntervalSub, POS_END, MF_BYPOSITION | MF_STRING | (currentInterval == 1000 ? MF_CHECKED : MF_UNCHECKED),
                    ID_MENU_INTERVAL_1S, L"1 Second (Fast)");
        InsertMenuW(hIntervalSub, POS_END, MF_BYPOSITION | MF_STRING | (currentInterval == 2000 ? MF_CHECKED : MF_UNCHECKED),
                    ID_MENU_INTERVAL_2S, L"2 Seconds (Balanced)");
        InsertMenuW(hIntervalSub, POS_END, MF_BYPOSITION | MF_STRING | (currentInterval == 5000 ? MF_CHECKED : MF_UNCHECKED),
                    ID_MENU_INTERVAL_5S, L"5 Seconds (Low Power)");
        InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(hIntervalSub), L"Polling Interval");

        InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_STRING, ID_MENU_TASKMGR, L"Open Task Manager");
        InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_STRING | (IsAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED),
                    ID_MENU_STARTUP, L"Start with Windows");

        InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_STRING, ID_MENU_EXIT, L"Exit");

        SetForegroundWindow(hWnd);
        TrackPopupMenuEx(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, hWnd, nullptr);
        PostMessageW(hWnd, WM_NULL, 0, 0);

        DestroyMenu(hMenu);
    }

    void SetupTrayIcon(HWND hWnd) {
        g_nid = {};
        g_nid.cbSize = sizeof(NOTIFYICONDATAW);
        g_nid.hWnd = hWnd;
        g_nid.uID = ID_TRAY_APP;
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        g_nid.uCallbackMessage = WM_APP_TRAYNOTIFY;
        g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wcscpy_s(g_nid.szTip, L"Hardware Taskbar Monitor");

        if (Shell_NotifyIconW(NIM_ADD, &g_nid)) {
            g_trayAdded = true;
            g_nid.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
        }
    }

    void RemoveTrayIcon() {
        if (g_trayAdded) {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            g_trayAdded = false;
        }
    }
}

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == g_wmTaskbarCreated && g_wmTaskbarCreated != 0) {
        SetupTrayIcon(hWnd);
        g_overlayWidget.Reposition();
        return 0;
    }

    switch (message) {
        case WM_CREATE: {
            g_hMainWnd = hWnd;
            g_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

            SetupTrayIcon(hWnd);

            HINSTANCE hInstance = reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance;
            g_overlayWidget.Initialize(hInstance, hWnd);

            // Fetch initial snapshot synchronously
            HardwareMetrics initialMetrics;
            g_hwMonitor.QueryCurrentMetrics(initialMetrics);
            g_overlayWidget.UpdateMetrics(initialMetrics);

            // Start background worker thread (1000ms default)
            g_hwMonitor.Start(hWnd, 1000);

            // Trim working set to keep footprint lean (< 10 MB)
            SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
            return 0;
        }

        case WM_APP_METRICS_UPDATE: {
            auto* p = reinterpret_cast<HardwareMetrics*>(lParam);
            if (p) {
                g_overlayWidget.UpdateMetrics(*p);

                // Update tray tooltip with live summary
                wchar_t tipBuf[128] = {0};
                std::wstring cpuTip = L"CPU: " + std::to_wstring(p->cpu.usagePercent) + L"%";
                std::wstring ramTip = L"RAM: " + std::to_wstring(p->ram.usagePercent) + L"%";
                std::wstring gpuTip = L"GPU: ";
                if (p->gpu.isAvailable) {
                    gpuTip += std::to_wstring(p->gpu.utilizationPercent) + L"%";
                    if (p->gpu.temperatureC > 0) {
                        gpuTip += L" (" + std::to_wstring(p->gpu.temperatureC) + L"°C)";
                    }
                } else {
                    gpuTip += L"--";
                }
                swprintf_s(tipBuf, L"%s | %s | %s", cpuTip.c_str(), ramTip.c_str(), gpuTip.c_str());
                wcscpy_s(g_nid.szTip, tipBuf);
                Shell_NotifyIconW(NIM_MODIFY, &g_nid);

                delete p;

                // Periodically trim memory every 60 ticks
                static uint32_t s_ticks = 0;
                if (++s_ticks >= 60) {
                    s_ticks = 0;
                    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
                }
            }
            return 0;
        }

        case WM_APP_TRAYNOTIFY: {
            switch (LOWORD(lParam)) {
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU: {
                    POINT pt;
                    GetCursorPos(&pt);
                    ShowContextMenu(hWnd, pt);
                    break;
                }
                case WM_LBUTTONDBLCLK: {
                    ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
                    break;
                }
            }
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_MENU_INTERVAL_1S:
                    g_hwMonitor.SetInterval(1000);
                    break;
                case ID_MENU_INTERVAL_2S:
                    g_hwMonitor.SetInterval(2000);
                    break;
                case ID_MENU_INTERVAL_5S:
                    g_hwMonitor.SetInterval(5000);
                    break;
                case ID_MENU_TASKMGR:
                    ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
                    break;
                case ID_MENU_STARTUP:
                    ToggleAutoStart();
                    break;
                case ID_MENU_EXIT:
                    DestroyWindow(hWnd);
                    break;
            }
            return 0;
        }

        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE: {
            g_overlayWidget.Reposition();
            return 0;
        }

        case WM_DESTROY: {
            g_hwMonitor.Stop();
            g_overlayWidget.Destroy();
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;
        }

        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPWSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // Ensure thread is attached to the active interactive desktop
    HWINSTA hWinSta = OpenWindowStationW(L"winsta0", FALSE, MAXIMUM_ALLOWED);
    if (hWinSta) {
        SetProcessWindowStation(hWinSta);
        HDESK hDesk = OpenDesktopW(L"default", 0, FALSE, MAXIMUM_ALLOWED);
        if (hDesk) {
            SetThreadDesktop(hDesk);
        }
    }

    // Single instance mutex
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\HardwareTaskbar_Instance_Mutex_98D2E1");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr,
                    L"Hardware Taskbar Monitor is already active on the Taskbar.",
                    L"Hardware Taskbar Monitor",
                    MB_OK | MB_ICONINFORMATION);
        if (hMutex) {
            CloseHandle(hMutex);
        }
        return 0;
    }

    const wchar_t CLASS_NAME[] = L"HardwareTaskbar_HiddenControlClass";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassExW(&wc)) {
        if (hMutex) {
            CloseHandle(hMutex);
        }
        return 1;
    }

    HWND hWnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Hardware Taskbar Controller",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        0, 0,
        nullptr, nullptr,
        hInstance, nullptr
    );

    if (!hWnd) {
        if (hMutex) {
            CloseHandle(hMutex);
        }
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hMutex) {
        CloseHandle(hMutex);
    }

    return static_cast<int>(msg.wParam);
}
