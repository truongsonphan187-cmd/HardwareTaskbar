#pragma once

#include "Common.hpp"
#include <gdiplus.h>

class IconRenderer {
public:
    IconRenderer();
    ~IconRenderer();

    // Non-copyable
    IconRenderer(const IconRenderer&) = delete;
    IconRenderer& operator=(const IconRenderer&) = delete;

    // Creates a dynamic GDI+ 32-bit ARGB HICON for the specified metric
    HICON CreateIconForMetric(MetricType type, uint32_t value, bool isAvailable = true, int targetSize = 0);

private:
    ULONG_PTR m_gdiplusToken = 0;
    bool m_gdiplusInitialized = false;

    // Helper to get system small icon size
    int GetSmallIconSize() const;
};
