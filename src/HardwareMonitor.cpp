#include "HardwareMonitor.hpp"
#include <cwctype>

#pragma comment(lib, "pdh.lib")

HardwareMonitor::HardwareMonitor() = default;

HardwareMonitor::~HardwareMonitor() {
    Stop();
    CleanupCpuMonitor();
}

bool HardwareMonitor::Start(HWND hWndTarget, uint32_t intervalMs) {
    if (m_running.load()) {
        return true;
    }

    m_hWndTarget = hWndTarget;
    m_intervalMs.store(intervalMs);

    // Initialize telemetry subsystems
    InitCpuMonitor();
    m_nvml.Initialize();

    m_running.store(true);
    m_workerThread = std::thread(&HardwareMonitor::WorkerLoop, this);

    return true;
}

void HardwareMonitor::Stop() {
    if (!m_running.load()) {
        return;
    }

    m_running.store(false);
    m_cv.notify_all();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    m_nvml.Shutdown();
}

void HardwareMonitor::SetInterval(uint32_t intervalMs) {
    if (intervalMs < 200) {
        intervalMs = 200;
    }
    m_intervalMs.store(intervalMs);
    m_cv.notify_all(); // Wake up worker to immediately adopt new interval
}

void HardwareMonitor::InitCpuMonitor() {
    // 1. Retrieve CPU Name from Windows Registry
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[256] = {0};
        DWORD bufSize = sizeof(buf);
        if (RegQueryValueExW(hKey, L"ProcessorNameString", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &bufSize) == ERROR_SUCCESS) {
            m_cpuName = buf;
            while (!m_cpuName.empty() && iswspace(m_cpuName.front())) m_cpuName.erase(m_cpuName.begin());
            while (!m_cpuName.empty() && iswspace(m_cpuName.back())) m_cpuName.pop_back();
        }
        RegCloseKey(hKey);
    }

    if (m_cpuName.empty()) {
        m_cpuName = L"CPU";
    }

    m_cpuCores = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);

    // 2. Initialize PDH query with English counters
    if (PdhOpenQueryW(nullptr, 0, &m_pdhQuery) == ERROR_SUCCESS) {
        PDH_STATUS status = PdhAddEnglishCounterW(m_pdhQuery, L"\\Processor(_Total)\\% Processor Time", 0, &m_pdhCounter);
        if (status != ERROR_SUCCESS) {
            status = PdhAddEnglishCounterW(m_pdhQuery, L"\\Processor Information(_Total)\\% Processor Time", 0, &m_pdhCounter);
        }

        if (status == ERROR_SUCCESS) {
            PdhCollectQueryData(m_pdhQuery);
            m_pdhInitialized = true;
        } else {
            PdhCloseQuery(m_pdhQuery);
            m_pdhQuery = nullptr;
            m_pdhCounter = nullptr;
            m_pdhInitialized = false;
        }
    }

    // 3. Prime GetSystemTimes for high-accuracy fallback
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        ULARGE_INTEGER idle, kernel, user;
        idle.LowPart = idleTime.dwLowDateTime; idle.HighPart = idleTime.dwHighDateTime;
        kernel.LowPart = kernelTime.dwLowDateTime; kernel.HighPart = kernelTime.dwHighDateTime;
        user.LowPart = userTime.dwLowDateTime; user.HighPart = userTime.dwHighDateTime;
        m_prevIdleTime = idle.QuadPart;
        m_prevKernelTime = kernel.QuadPart;
        m_prevUserTime = user.QuadPart;
        m_hasPrevTimes = true;
    }
}

void HardwareMonitor::CleanupCpuMonitor() {
    if (m_pdhQuery) {
        PdhCloseQuery(m_pdhQuery);
        m_pdhQuery = nullptr;
        m_pdhCounter = nullptr;
        m_pdhInitialized = false;
    }
}

uint32_t HardwareMonitor::QueryCpuUsage() {
    // Attempt PDH first if successfully initialized
    if (m_pdhInitialized && m_pdhQuery && m_pdhCounter) {
        if (PdhCollectQueryData(m_pdhQuery) == ERROR_SUCCESS) {
            PDH_FMT_COUNTERVALUE counterVal;
            if (PdhGetFormattedCounterValue(m_pdhCounter, PDH_FMT_DOUBLE, nullptr, &counterVal) == ERROR_SUCCESS) {
                double val = counterVal.doubleValue;
                if (val < 0.0) val = 0.0;
                if (val > 100.0) val = 100.0;
                return static_cast<uint32_t>(val + 0.5);
            }
        }
    }

    // High-performance fallback: GetSystemTimes delta calculation
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        ULARGE_INTEGER idle, kernel, user;
        idle.LowPart = idleTime.dwLowDateTime; idle.HighPart = idleTime.dwHighDateTime;
        kernel.LowPart = kernelTime.dwLowDateTime; kernel.HighPart = kernelTime.dwHighDateTime;
        user.LowPart = userTime.dwLowDateTime; user.HighPart = userTime.dwHighDateTime;

        if (m_hasPrevTimes) {
            ULONGLONG idleDelta = idle.QuadPart - m_prevIdleTime;
            ULONGLONG kernelDelta = kernel.QuadPart - m_prevKernelTime;
            ULONGLONG userDelta = user.QuadPart - m_prevUserTime;
            ULONGLONG totalSystem = kernelDelta + userDelta;

            m_prevIdleTime = idle.QuadPart;
            m_prevKernelTime = kernel.QuadPart;
            m_prevUserTime = user.QuadPart;

            if (totalSystem > 0) {
                ULONGLONG used = (totalSystem > idleDelta) ? (totalSystem - idleDelta) : 0;
                uint32_t percent = static_cast<uint32_t>((used * 100) / totalSystem);
                return (percent > 100) ? 100 : percent;
            }
        } else {
            m_prevIdleTime = idle.QuadPart;
            m_prevKernelTime = kernel.QuadPart;
            m_prevUserTime = user.QuadPart;
            m_hasPrevTimes = true;
        }
    }

    return 0;
}

void HardwareMonitor::QueryRamUsage(RamMetrics& metrics) {
    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        metrics.usagePercent = mem.dwMemoryLoad;
        metrics.totalBytes = mem.ullTotalPhys;
        metrics.usedBytes = mem.ullTotalPhys - mem.ullAvailPhys;
        metrics.totalGb = static_cast<double>(metrics.totalBytes) / (1024.0 * 1024.0 * 1024.0);
        metrics.usedGb = static_cast<double>(metrics.usedBytes) / (1024.0 * 1024.0 * 1024.0);
    }
}

bool HardwareMonitor::QueryCurrentMetrics(HardwareMetrics& metrics) {
    // 1. CPU
    metrics.cpu.usagePercent = QueryCpuUsage();
    metrics.cpu.cpuName = m_cpuName;
    metrics.cpu.logicalCores = m_cpuCores;

    // 2. RAM
    QueryRamUsage(metrics.ram);

    // 3. GPU
    m_nvml.QueryMetrics(metrics.gpu);

    return true;
}

void HardwareMonitor::WorkerLoop() {
    while (m_running.load()) {
        auto* pMetrics = new HardwareMetrics();
        QueryCurrentMetrics(*pMetrics);

        if (m_hWndTarget && IsWindow(m_hWndTarget)) {
            // Asynchronously post to main thread
            if (!PostMessageW(m_hWndTarget, WM_APP_METRICS_UPDATE, 0, reinterpret_cast<LPARAM>(pMetrics))) {
                delete pMetrics;
            }
        } else {
            delete pMetrics;
        }

        // Sleep interruptibly
        std::unique_lock<std::mutex> lock(m_cvMutex);
        m_cv.wait_for(lock, std::chrono::milliseconds(m_intervalMs.load()), [this] {
            return !m_running.load();
        });
    }
}
