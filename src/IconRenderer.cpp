#include "IconRenderer.hpp"
#include <string>

#pragma comment(lib, "gdiplus.lib")

IconRenderer::IconRenderer() {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr) == Gdiplus::Ok) {
        m_gdiplusInitialized = true;
    }
}

IconRenderer::~IconRenderer() {
    if (m_gdiplusInitialized) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusInitialized = false;
    }
}

int IconRenderer::GetSmallIconSize() const {
    int cx = GetSystemMetrics(SM_CXSMICON);
    return (cx > 0) ? cx : 16;
}

HICON IconRenderer::CreateIconForMetric(MetricType type, uint32_t value, bool isAvailable, int targetSize) {
    if (!m_gdiplusInitialized) {
        return nullptr;
    }

    if (targetSize <= 0) {
        targetSize = GetSmallIconSize();
    }

    // Prepare text representation
    std::wstring text;
    if (!isAvailable) {
        text = L"--";
    } else {
        if (value > 100) {
            value = 100;
        }
        text = std::to_wstring(value);
    }

    // Determine color scheme based on hardware type and load state
    Gdiplus::Color accentColor;
    if (!isAvailable) {
        accentColor = Gdiplus::Color(255, 107, 114, 128); // Slate gray for disabled
    } else if (value >= 90) {
        accentColor = Gdiplus::Color(255, 239, 68, 68);   // Warning Crimson
    } else {
        switch (type) {
            case MetricType::CPU:
                accentColor = Gdiplus::Color(255, 0, 210, 255);   // Cyan / Electric Blue
                break;
            case MetricType::RAM:
                accentColor = Gdiplus::Color(255, 245, 158, 11);  // Amber Gold
                break;
            case MetricType::GPU:
                accentColor = Gdiplus::Color(255, 118, 185, 0);   // NVIDIA Green
                break;
        }
    }

    // Base background color: Deep slate #0E1219 (96% opaque for smooth subpixel blend)
    Gdiplus::Color bgColor(245, 14, 18, 25);

    // Create 32-bit ARGB in-memory bitmap
    Gdiplus::Bitmap bmp(targetSize, targetSize, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        // Calculate corner radius proportional to icon size
        int radius = 3;
        if (targetSize >= 32) {
            radius = 6;
        } else if (targetSize >= 24) {
            radius = 4;
        } else if (targetSize >= 20) {
            radius = 3;
        }

        // Build rounded rectangle path
        Gdiplus::GraphicsPath path;
        Gdiplus::REAL r = static_cast<Gdiplus::REAL>(radius);
        Gdiplus::REAL s = static_cast<Gdiplus::REAL>(targetSize);

        path.AddArc(0.5f, 0.5f, r * 2.0f, r * 2.0f, 180.0f, 90.0f);
        path.AddArc(s - 1.0f - r * 2.0f, 0.5f, r * 2.0f, r * 2.0f, 270.0f, 90.0f);
        path.AddArc(s - 1.0f - r * 2.0f, s - 1.0f - r * 2.0f, r * 2.0f, r * 2.0f, 0.0f, 90.0f);
        path.AddArc(0.5f, s - 1.0f - r * 2.0f, r * 2.0f, r * 2.0f, 90.0f, 90.0f);
        path.CloseFigure();

        // Fill background chip & stroke accent outline
        Gdiplus::SolidBrush bgBrush(bgColor);
        g.FillPath(&bgBrush, &path);

        Gdiplus::Pen borderPen(accentColor, 1.0f);
        g.DrawPath(&borderPen, &path);

        // Dynamically size font depending on character count and canvas dimensions
        size_t len = text.length();
        Gdiplus::REAL fontSize = 9.0f;
        if (targetSize <= 16) {
            fontSize = (len >= 3) ? 6.5f : 9.0f;
        } else if (targetSize <= 20) {
            fontSize = (len >= 3) ? 8.0f : 11.0f;
        } else if (targetSize <= 24) {
            fontSize = (len >= 3) ? 9.5f : 13.0f;
        } else { // 32x32 or greater
            fontSize = (len >= 3) ? 12.0f : 16.0f;
        }

        Gdiplus::Font font(L"Segoe UI", fontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));

        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        // Slight vertical optical alignment adjustment for compact glyphs
        Gdiplus::REAL yOffset = (targetSize <= 16) ? -0.5f : 0.0f;
        Gdiplus::RectF layoutRect(0.0f, yOffset, s, s);

        g.DrawString(text.c_str(), -1, &font, layoutRect, &sf, &textBrush);
    }

    HICON hIcon = nullptr;
    if (bmp.GetHICON(&hIcon) != Gdiplus::Ok) {
        return nullptr;
    }

    return hIcon;
}
