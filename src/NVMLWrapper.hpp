#pragma once

#include "Common.hpp"
#include <string>

class NVMLWrapper {
public:
    NVMLWrapper();
    ~NVMLWrapper();

    // Non-copyable, non-movable RAII wrapper
    NVMLWrapper(const NVMLWrapper&) = delete;
    NVMLWrapper& operator=(const NVMLWrapper&) = delete;

    bool Initialize();
    void Shutdown();

    bool IsAvailable() const { return m_initialized; }
    const std::wstring& GetDeviceName() const { return m_deviceName; }

    bool QueryMetrics(GpuMetrics& metrics);

private:
    HMODULE m_hModule = nullptr;
    bool m_initialized = false;
    void* m_deviceHandle = nullptr;
    std::wstring m_deviceName;

    // NVML internal enums & structs
    enum nvmlReturn_t {
        NVML_SUCCESS = 0
    };

    struct nvmlUtilization_t {
        unsigned int gpu;
        unsigned int memory;
    };

    struct nvmlMemory_t {
        unsigned long long total;
        unsigned long long free;
        unsigned long long used;
    };

    // Function pointer types
    using nvmlInit_t = int (*)();
    using nvmlShutdown_t = int (*)();
    using nvmlDeviceGetHandleByIndex_t = int (*)(unsigned int index, void** device);
    using nvmlDeviceGetUtilizationRates_t = int (*)(void* device, nvmlUtilization_t* utilization);
    using nvmlDeviceGetTemperature_t = int (*)(void* device, int sensorType, unsigned int* temp);
    using nvmlDeviceGetName_t = int (*)(void* device, char* name, unsigned int length);
    using nvmlDeviceGetMemoryInfo_t = int (*)(void* device, nvmlMemory_t* memory);

    // Dynamic entry points
    nvmlInit_t m_pfnInit = nullptr;
    nvmlShutdown_t m_pfnShutdown = nullptr;
    nvmlDeviceGetHandleByIndex_t m_pfnGetHandle = nullptr;
    nvmlDeviceGetUtilizationRates_t m_pfnGetUtilization = nullptr;
    nvmlDeviceGetTemperature_t m_pfnGetTemperature = nullptr;
    nvmlDeviceGetName_t m_pfnGetName = nullptr;
    nvmlDeviceGetMemoryInfo_t m_pfnGetMemoryInfo = nullptr;
};
