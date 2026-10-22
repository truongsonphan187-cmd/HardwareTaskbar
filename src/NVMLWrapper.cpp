#include "NVMLWrapper.hpp"
#include <vector>

NVMLWrapper::NVMLWrapper() = default;

NVMLWrapper::~NVMLWrapper() {
    Shutdown();
}

bool NVMLWrapper::Initialize() {
    if (m_initialized) {
        return true;
    }

    // Try loading nvml.dll from standard system paths first
    m_hModule = LoadLibraryW(L"nvml.dll");
    if (!m_hModule) {
        // Fallback to default NVIDIA NVSMI installation folder
        m_hModule = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    }

    if (!m_hModule) {
        return false;
    }

    // Resolve function addresses
    m_pfnInit = reinterpret_cast<nvmlInit_t>(GetProcAddress(m_hModule, "nvmlInit_v2"));
    if (!m_pfnInit) {
        m_pfnInit = reinterpret_cast<nvmlInit_t>(GetProcAddress(m_hModule, "nvmlInit"));
    }

    m_pfnShutdown = reinterpret_cast<nvmlShutdown_t>(GetProcAddress(m_hModule, "nvmlShutdown"));

    m_pfnGetHandle = reinterpret_cast<nvmlDeviceGetHandleByIndex_t>(GetProcAddress(m_hModule, "nvmlDeviceGetHandleByIndex_v2"));
    if (!m_pfnGetHandle) {
        m_pfnGetHandle = reinterpret_cast<nvmlDeviceGetHandleByIndex_t>(GetProcAddress(m_hModule, "nvmlDeviceGetHandleByIndex"));
    }

    m_pfnGetUtilization = reinterpret_cast<nvmlDeviceGetUtilizationRates_t>(GetProcAddress(m_hModule, "nvmlDeviceGetUtilizationRates"));
    m_pfnGetTemperature = reinterpret_cast<nvmlDeviceGetTemperature_t>(GetProcAddress(m_hModule, "nvmlDeviceGetTemperature"));
    m_pfnGetName = reinterpret_cast<nvmlDeviceGetName_t>(GetProcAddress(m_hModule, "nvmlDeviceGetName"));
    m_pfnGetMemoryInfo = reinterpret_cast<nvmlDeviceGetMemoryInfo_t>(GetProcAddress(m_hModule, "nvmlDeviceGetMemoryInfo"));

    if (!m_pfnInit || !m_pfnShutdown || !m_pfnGetHandle || !m_pfnGetUtilization || !m_pfnGetTemperature) {
        Shutdown();
        return false;
    }

    // Initialize NVML library
    if (m_pfnInit() != NVML_SUCCESS) {
        Shutdown();
        return false;
    }

    // Acquire primary GPU device handle (index 0)
    if (m_pfnGetHandle(0, &m_deviceHandle) != NVML_SUCCESS || !m_deviceHandle) {
        Shutdown();
        return false;
    }

    // Retrieve device name
    if (m_pfnGetName) {
        char nameBuf[128] = {0};
        if (m_pfnGetName(m_deviceHandle, nameBuf, sizeof(nameBuf)) == NVML_SUCCESS) {
            int wideLen = MultiByteToWideChar(CP_UTF8, 0, nameBuf, -1, nullptr, 0);
            if (wideLen > 0) {
                std::wstring name(wideLen - 1, 0);
                MultiByteToWideChar(CP_UTF8, 0, nameBuf, -1, &name[0], wideLen);
                m_deviceName = std::move(name);
            }
        }
    }

    if (m_deviceName.empty()) {
        m_deviceName = L"NVIDIA GPU";
    }

    m_initialized = true;
    return true;
}

void NVMLWrapper::Shutdown() {
    if (m_initialized && m_pfnShutdown) {
        m_pfnShutdown();
        m_initialized = false;
    }
    m_deviceHandle = nullptr;
    m_deviceName.clear();

    if (m_hModule) {
        FreeLibrary(m_hModule);
        m_hModule = nullptr;
    }

    m_pfnInit = nullptr;
    m_pfnShutdown = nullptr;
    m_pfnGetHandle = nullptr;
    m_pfnGetUtilization = nullptr;
    m_pfnGetTemperature = nullptr;
    m_pfnGetName = nullptr;
    m_pfnGetMemoryInfo = nullptr;
}

bool NVMLWrapper::QueryMetrics(GpuMetrics& metrics) {
    if (!m_initialized || !m_deviceHandle) {
        metrics.isAvailable = false;
        metrics.deviceName = L"GPU: Not Detected";
        metrics.utilizationPercent = 0;
        metrics.temperatureC = 0;
        metrics.vramUsedGb = 0.0;
        metrics.vramTotalGb = 0.0;
        return false;
    }

    nvmlUtilization_t util = {0, 0};
    if (m_pfnGetUtilization(m_deviceHandle, &util) != NVML_SUCCESS) {
        metrics.isAvailable = false;
        return false;
    }

    unsigned int temp = 0;
    if (m_pfnGetTemperature(m_deviceHandle, 0, &temp) != NVML_SUCCESS) {
        temp = 0;
    }

    double usedGb = 0.0;
    double totalGb = 0.0;
    if (m_pfnGetMemoryInfo) {
        nvmlMemory_t mem = {0, 0, 0};
        if (m_pfnGetMemoryInfo(m_deviceHandle, &mem) == NVML_SUCCESS) {
            usedGb = static_cast<double>(mem.used) / (1024.0 * 1024.0 * 1024.0);
            totalGb = static_cast<double>(mem.total) / (1024.0 * 1024.0 * 1024.0);
        }
    }

    metrics.isAvailable = true;
    metrics.deviceName = m_deviceName;
    metrics.utilizationPercent = util.gpu;
    metrics.temperatureC = temp;
    metrics.vramUsedGb = usedGb;
    metrics.vramTotalGb = totalGb;

    return true;
}
