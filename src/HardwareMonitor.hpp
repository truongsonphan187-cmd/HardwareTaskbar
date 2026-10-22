#pragma once

#include "Common.hpp"
#include "NVMLWrapper.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <pdh.h>
#include <pdhmsg.h>

class HardwareMonitor {
public:
    HardwareMonitor();
    ~HardwareMonitor();

    // Non-copyable
    HardwareMonitor(const HardwareMonitor&) = delete;
    HardwareMonitor& operator=(const HardwareMonitor&) = delete;

    bool Start(HWND hWndTarget, uint32_t intervalMs = 1000);
    void Stop();

    void SetInterval(uint32_t intervalMs);
    uint32_t GetInterval() const { return m_intervalMs.load(); }

    bool QueryCurrentMetrics(HardwareMetrics& metrics);

private:
    void WorkerLoop();

    // CPU Telemetry helpers
    void InitCpuMonitor();
    uint32_t QueryCpuUsage();
    void CleanupCpuMonitor();

    // RAM Telemetry helper
    void QueryRamUsage(RamMetrics& metrics);

    HWND m_hWndTarget = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<uint32_t> m_intervalMs{1000};
    std::thread m_workerThread;
    std::mutex m_cvMutex;
    std::condition_variable m_cv;

    // CPU Telemetry members
    std::wstring m_cpuName;
    uint32_t m_cpuCores = 0;
    PDH_HQUERY m_pdhQuery = nullptr;
    PDH_HCOUNTER m_pdhCounter = nullptr;
    bool m_pdhInitialized = false;

    // Fallback GetSystemTimes state
    ULONGLONG m_prevIdleTime = 0;
    ULONGLONG m_prevKernelTime = 0;
    ULONGLONG m_prevUserTime = 0;
    bool m_hasPrevTimes = false;

    // GPU Telemetry
    NVMLWrapper m_nvml;
};
