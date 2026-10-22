#include "TrayIcon.hpp"

namespace SystemUtils {
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

TrayIconManager::TrayIconManager() = default;

TrayIconManager::~TrayIconManager() {
    RemoveAll();
}

bool TrayIconManager::Initialize(HWND hWnd) {
    m_hWnd = hWnd;
    return true;
}

void TrayIconManager::UpdateSingleIcon(UINT iconId, MetricType type, uint32_t value, bool isAvailable,
                                      const std::wstring& tooltip, HICON& currentIcon, bool& isAdded) {
    HICON hNewIcon = m_renderer.CreateIconForMetric(type, value, isAvailable);
    if (!hNewIcon) {
        return;
    }

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = m_hWnd;
    nid.uID = iconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_TRAYNOTIFY;
    nid.hIcon = hNewIcon;
    wcsncpy_s(nid.szTip, tooltip.c_str(), _TRUNCATE);

    if (!isAdded) {
        if (Shell_NotifyIconW(NIM_ADD, &nid)) {
            isAdded = true;
            nid.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &nid);
        }
    } else {
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    // Clean up previous HICON to guarantee zero Win32 handle leaks
    if (currentIcon) {
        DestroyIcon(currentIcon);
    }
    currentIcon = hNewIcon;
}

void TrayIconManager::UpdateCpu(uint32_t usage, const std::wstring& tooltip) {
    m_lastCpuUsage = usage;
    m_lastCpuTip = tooltip;
    UpdateSingleIcon(ID_TRAY_CPU, MetricType::CPU, usage, true, tooltip, m_hCpuIcon, m_cpuAdded);
}

void TrayIconManager::UpdateRam(uint32_t usage, const std::wstring& tooltip) {
    m_lastRamUsage = usage;
    m_lastRamTip = tooltip;
    UpdateSingleIcon(ID_TRAY_RAM, MetricType::RAM, usage, true, tooltip, m_hRamIcon, m_ramAdded);
}

void TrayIconManager::UpdateGpu(uint32_t usage, bool isAvailable, const std::wstring& tooltip) {
    m_lastGpuUsage = usage;
    m_lastGpuAvailable = isAvailable;
    m_lastGpuTip = tooltip;
    UpdateSingleIcon(ID_TRAY_GPU, MetricType::GPU, usage, isAvailable, tooltip, m_hGpuIcon, m_gpuAdded);
}

void TrayIconManager::ReAddAll() {
    m_cpuAdded = false;
    m_ramAdded = false;
    m_gpuAdded = false;

    if (!m_lastCpuTip.empty()) {
        UpdateCpu(m_lastCpuUsage, m_lastCpuTip);
    }
    if (!m_lastRamTip.empty()) {
        UpdateRam(m_lastRamUsage, m_lastRamTip);
    }
    if (!m_lastGpuTip.empty()) {
        UpdateGpu(m_lastGpuUsage, m_lastGpuAvailable, m_lastGpuTip);
    }
}

void TrayIconManager::RemoveAll() {
    auto removeIcon = [this](UINT iconId, HICON& icon, bool& isAdded) {
        if (isAdded) {
            NOTIFYICONDATAW nid = {};
            nid.cbSize = sizeof(NOTIFYICONDATAW);
            nid.hWnd = m_hWnd;
            nid.uID = iconId;
            Shell_NotifyIconW(NIM_DELETE, &nid);
            isAdded = false;
        }
        if (icon) {
            DestroyIcon(icon);
            icon = nullptr;
        }
    };

    removeIcon(ID_TRAY_CPU, m_hCpuIcon, m_cpuAdded);
    removeIcon(ID_TRAY_RAM, m_hRamIcon, m_ramAdded);
    removeIcon(ID_TRAY_GPU, m_hGpuIcon, m_gpuAdded);
}

void TrayIconManager::ShowContextMenu(POINT pt, uint32_t currentIntervalMs) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) {
        return;
    }

    constexpr UINT POS_END = static_cast<UINT>(-1);

    // Header Item (Disabled label)
    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_STRING | MF_DISABLED, ID_MENU_HEADER, L"Hardware Taskbar Monitor");
    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

    // Update Interval Submenu
    HMENU hIntervalSub = CreatePopupMenu();
    InsertMenuW(hIntervalSub, POS_END, MF_BYPOSITION | MF_STRING | (currentIntervalMs == 1000 ? MF_CHECKED : MF_UNCHECKED),
                ID_MENU_INTERVAL_1S, L"1 Second (Fast)");
    InsertMenuW(hIntervalSub, POS_END, MF_BYPOSITION | MF_STRING | (currentIntervalMs == 2000 ? MF_CHECKED : MF_UNCHECKED),
                ID_MENU_INTERVAL_2S, L"2 Seconds (Balanced)");
    InsertMenuW(hIntervalSub, POS_END, MF_BYPOSITION | MF_STRING | (currentIntervalMs == 5000 ? MF_CHECKED : MF_UNCHECKED),
                ID_MENU_INTERVAL_5S, L"5 Seconds (Low Power)");
    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(hIntervalSub), L"Polling Interval");

    // Utilities
    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_STRING, ID_MENU_TASKMGR, L"Open Task Manager");
    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_STRING | (SystemUtils::IsAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED),
                ID_MENU_STARTUP, L"Start with Windows");

    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(hMenu, POS_END, MF_BYPOSITION | MF_STRING, ID_MENU_EXIT, L"Exit");

    // Standard Win32 popup menu trick to ensure menu dismisses on outside click
    SetForegroundWindow(m_hWnd);
    TrackPopupMenuEx(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, m_hWnd, nullptr);
    PostMessageW(m_hWnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}
