#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <unknwn.h> // Provides IStream definition required by GdiPlus
#include <shellapi.h>
#include <commctrl.h>
#include <cstdint>
#include <string>

// Custom Window Messages
constexpr UINT WM_APP_METRICS_UPDATE  = WM_APP + 101;
constexpr UINT WM_APP_TRAYNOTIFY      = WM_APP + 102;
constexpr UINT WM_APP_REPOSITION      = WM_APP + 103;

// System Tray Icon Identifiers
constexpr UINT ID_TRAY_CPU = 101;
constexpr UINT ID_TRAY_RAM = 102;
constexpr UINT ID_TRAY_GPU = 103;
constexpr UINT ID_TRAY_APP = 104;

// Context Menu Command Identifiers
constexpr UINT ID_MENU_HEADER       = 2000;
constexpr UINT ID_MENU_INTERVAL_1S  = 2001;
constexpr UINT ID_MENU_INTERVAL_2S  = 2002;
constexpr UINT ID_MENU_INTERVAL_5S  = 2003;
constexpr UINT ID_MENU_CLICKTHROUGH = 2004;
constexpr UINT ID_MENU_TASKMGR      = 2005;
constexpr UINT ID_MENU_STARTUP      = 2006;
constexpr UINT ID_MENU_EXIT         = 2007;

// Metric Classification for Visual Styling
enum class MetricType {
    CPU,
    RAM,
    GPU
};

// Telemetry Data Transfer Objects
struct CpuMetrics {
    uint32_t usagePercent = 0;
    std::wstring cpuName;
    uint32_t logicalCores = 0;
};

struct RamMetrics {
    uint32_t usagePercent = 0;
    double usedGb = 0.0;
    double totalGb = 0.0;
    uint64_t usedBytes = 0;
    uint64_t totalBytes = 0;
};

struct GpuMetrics {
    bool isAvailable = false;
    std::wstring deviceName;
    uint32_t utilizationPercent = 0;
    uint32_t temperatureC = 0;
    double vramUsedGb = 0.0;
    double vramTotalGb = 0.0;
};

struct HardwareMetrics {
    CpuMetrics cpu;
    RamMetrics ram;
    GpuMetrics gpu;
};
