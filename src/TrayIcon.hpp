#pragma once

#include "Common.hpp"
#include "IconRenderer.hpp"
#include <string>

namespace SystemUtils {
    bool IsAutoStartEnabled();
    void ToggleAutoStart();
}

class TrayIconManager {
public:
    TrayIconManager();
    ~TrayIconManager();

    // Non-copyable
    TrayIconManager(const TrayIconManager&) = delete;
    TrayIconManager& operator=(const TrayIconManager&) = delete;

    bool Initialize(HWND hWnd);
    void RemoveAll();
    void ReAddAll();

    void UpdateCpu(uint32_t usage, const std::wstring& tooltip);
    void UpdateRam(uint32_t usage, const std::wstring& tooltip);
    void UpdateGpu(uint32_t usage, bool isAvailable, const std::wstring& tooltip);

    void ShowContextMenu(POINT pt, uint32_t currentIntervalMs);

private:
    void UpdateSingleIcon(UINT iconId, MetricType type, uint32_t value, bool isAvailable,
                          const std::wstring& tooltip, HICON& currentIcon, bool& isAdded);

    HWND m_hWnd = nullptr;
    IconRenderer m_renderer;

    HICON m_hCpuIcon = nullptr;
    HICON m_hRamIcon = nullptr;
    HICON m_hGpuIcon = nullptr;

    bool m_cpuAdded = false;
    bool m_ramAdded = false;
    bool m_gpuAdded = false;

    // Last known state for explorer restarts
    uint32_t m_lastCpuUsage = 0;
    std::wstring m_lastCpuTip;

    uint32_t m_lastRamUsage = 0;
    std::wstring m_lastRamTip;

    uint32_t m_lastGpuUsage = 0;
    bool m_lastGpuAvailable = false;
    std::wstring m_lastGpuTip;
};
