#pragma once
#include "brand_logo.h"

namespace {

constexpr Gdiplus::ARGB kTrackColor = 0x38FFFFFF;
constexpr Gdiplus::ARGB kSessionTextColor = 0xF0FFFFFF;
constexpr Gdiplus::ARGB kWarnTextColor = 0xFFF87171;
constexpr Gdiplus::ARGB kWeeklyTextColor = 0xA0FFFFFF;
constexpr Gdiplus::ARGB kStaleDotColor = 0xFFF5A524;
constexpr BYTE kWeeklyBarAlpha = 0xB0;

// Pixel geometry of the pill for one DPI. Values are tuned at 96 DPI, where
// the Windows 11 taskbar is 48 px tall: vendors sit side by side in one strip.
// Example: SIZE size = PillLayout::forDpi(144).pillSize(2);
struct PillLayout {
    float scale, padding, logoSize, logoGap, barWidth, barGap, textWidth, sessionLineHeight,
        sessionBarHeight, weeklyLineHeight, weeklyBarHeight, vendorGap, resetWidth;

    static PillLayout forDpi(UINT dpi) {
        float s = static_cast<float>(dpi == 0 ? 96 : dpi) / 96.0f;
        return {s,      4 * s,  18 * s, 6 * s, 40 * s, 5 * s,
                48 * s, 13 * s, 5 * s,  11 * s, 3 * s, 12 * s, 40 * s};
    }

    float contentHeight() const { return sessionLineHeight + weeklyLineHeight; }

    float blockWidth() const {
        return logoSize + logoGap + barWidth + barGap + textWidth + resetWidth;
    }

    float blockLeft(size_t rowIndex) const {
        return padding + rowIndex * (blockWidth() + vendorGap);
    }

    SIZE pillSize(size_t rowCount) const {
        float gaps = rowCount > 0 ? (rowCount - 1) * vendorGap : 0;
        float width = padding * 2 + rowCount * blockWidth() + gaps;
        return {std::lround(width), std::lround(padding * 2 + contentHeight())};
    }
};

// GDI+ must be started on the thread that draws and shut down after the last
// GDI+ object is gone; declaring this first in a scope guarantees that order.
class GdiplusSession {
public:
    GdiplusSession() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&token_, &input, nullptr);
    }
    ~GdiplusSession() { Gdiplus::GdiplusShutdown(token_); }
    GdiplusSession(const GdiplusSession&) = delete;
    GdiplusSession& operator=(const GdiplusSession&) = delete;

private:
    ULONG_PTR token_ = 0;
};

// Top-down 32-bit DIB selected into a memory DC, as UpdateLayeredWindow expects.
// Example: DibCanvas canvas(120, 60); painter.paint(canvas, rows, false);
class DibCanvas {
public:
    DibCanvas(int width, int height) : width_(width), height_(height) {
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
        bitmap_ = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
        if (bitmap_ == nullptr) {
            throw std::runtime_error("CreateDIBSection rejected size " + std::to_string(width) +
                                     "x" + std::to_string(height) + "; expected positive size");
        }
        deviceContext_ = CreateCompatibleDC(nullptr);
        previousBitmap_ = SelectObject(deviceContext_, bitmap_);
    }

    ~DibCanvas() {
        SelectObject(deviceContext_, previousBitmap_);
        DeleteDC(deviceContext_);
        DeleteObject(bitmap_);
    }

    DibCanvas(const DibCanvas&) = delete;
    DibCanvas& operator=(const DibCanvas&) = delete;
    int width() const { return width_; }
    int height() const { return height_; }
    BYTE* bits() const { return static_cast<BYTE*>(bits_); }
    HDC deviceContext() const { return deviceContext_; }

private:
    int width_;
    int height_;
    void* bits_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HDC deviceContext_ = nullptr;
    HGDIOBJ previousBitmap_ = nullptr;
};

Gdiplus::ARGB withAlpha(Gdiplus::ARGB color, BYTE alpha) {
    return (color & 0x00FFFFFF) | (static_cast<Gdiplus::ARGB>(alpha) << 24);
}

std::wstring formatPercent(int percent) {
    return percent == kUnknownPercent ? L"–" : std::to_wstring(percent) + L"%";
}

void fillRoundedRect(Gdiplus::Graphics& graphics, Gdiplus::ARGB color, Gdiplus::RectF rect,
                     float radius) {
    float diameter = std::min({radius * 2, rect.Width, rect.Height});
    Gdiplus::GraphicsPath path;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270, 90);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0, 90);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
    Gdiplus::SolidBrush brush{Gdiplus::Color(color)};
    graphics.FillPath(&brush, &path);
}

// Draws the pill into a premultiplied ARGB canvas; knows nothing about windows.
// There is no background: the taskbar behind it already provides one.
// Example: PillPainter(PillLayout::forDpi(96), currentUnixSeconds()).paint(canvas, rows, false);
class PillPainter {
public:
    PillPainter(PillLayout layout, long long nowUnixSeconds)
        : layout_(layout), nowUnixSeconds_(nowUnixSeconds) {}

    void paint(DibCanvas& canvas, const std::vector<PillRow>& rows, bool stale) const {
        Gdiplus::Bitmap surface(canvas.width(), canvas.height(), canvas.width() * 4,
                                PixelFormat32bppPARGB, canvas.bits());
        Gdiplus::Graphics graphics(&surface);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            paintBlock(graphics, rows[rowIndex], layout_.blockLeft(rowIndex));
        }
        if (stale) {
            paintStaleDot(graphics, canvas);
        }
    }

private:
    void paintBlock(Gdiplus::Graphics& graphics, const PillRow& row, float left) const {
        float barsLeft = left + layout_.logoSize + layout_.logoGap;
        paintLogo(graphics, row, left, layout_.padding);
        paintSessionLine(graphics, row, barsLeft, layout_.padding);
        paintWeeklyLine(graphics, row, barsLeft, layout_.padding + layout_.sessionLineHeight);
    }

    void paintSessionLine(Gdiplus::Graphics& graphics, const PillRow& row, float barsLeft,
                          float top) const {
        float height = layout_.sessionLineHeight;
        bool nearLimit = row.sessionPercent >= kWarnPercent;
        paintBar(graphics, barsLeft, top, height, layout_.sessionBarHeight, row.sessionPercent,
                 row.color);
        paintText(graphics, formatPercent(row.sessionPercent), textLeft(barsLeft), top, height,
                  11 * layout_.scale, nearLimit ? kWarnTextColor : kSessionTextColor);
        paintResetText(graphics, row.sessionResetAt, barsLeft, top, height);
    }

    void paintWeeklyLine(Gdiplus::Graphics& graphics, const PillRow& row, float barsLeft,
                         float top) const {
        float height = layout_.weeklyLineHeight;
        paintBar(graphics, barsLeft, top, height, layout_.weeklyBarHeight, row.weeklyPercent,
                 withAlpha(row.color, kWeeklyBarAlpha));
        paintText(graphics, formatPercent(row.weeklyPercent) + L" sem", textLeft(barsLeft), top,
                  height, 9 * layout_.scale, kWeeklyTextColor);
        paintResetText(graphics, row.weeklyResetAt, barsLeft, top, height);
    }

    void paintResetText(Gdiplus::Graphics& graphics, long long resetAt, float barsLeft, float top,
                        float height) const {
        float resetLeft = textLeft(barsLeft) + layout_.textWidth;
        paintText(graphics, formatResetCountdown(resetAt, nowUnixSeconds_), resetLeft, top, height,
                  9 * layout_.scale, kWeeklyTextColor);
    }

    float textLeft(float barsLeft) const { return barsLeft + layout_.barWidth + layout_.barGap; }

    // Vendors without a bundled logo get a dot in their brand color instead.
    void paintLogo(Gdiplus::Graphics& graphics, const PillRow& row, float left, float top) const {
        float size = layout_.logoSize;
        Gdiplus::RectF box(left, top + (layout_.contentHeight() - size) / 2, size, size);
        Gdiplus::SolidBrush brush{Gdiplus::Color(row.color)};
        const BrandLogoShape* logo = findBrandLogo(row.vendorId);
        if (logo == nullptr) {
            box.Inflate(-size / 4, -size / 4);
            graphics.FillEllipse(&brush, box);
            return;
        }
        Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);
        LogoPathBuilder(box, path).build(*logo);
        graphics.FillPath(&brush, &path);
    }

    void paintBar(Gdiplus::Graphics& graphics, float left, float lineTop, float lineHeight,
                  float barHeight, int percent, Gdiplus::ARGB color) const {
        Gdiplus::RectF track(left, lineTop + (lineHeight - barHeight) / 2, layout_.barWidth,
                             barHeight);
        fillRoundedRect(graphics, kTrackColor, track, barHeight / 2);
        if (percent <= 0) {
            return;
        }
        Gdiplus::RectF fill = track;
        fill.Width = std::max(barHeight, track.Width * percent / 100.0f);
        fillRoundedRect(graphics, color, fill, barHeight / 2);
    }

    void paintText(Gdiplus::Graphics& graphics, const std::wstring& text, float left,
                   float lineTop, float lineHeight, float fontPixels, Gdiplus::ARGB color) const {
        Gdiplus::Font font(L"Segoe UI", fontPixels, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush brush{Gdiplus::Color(color)};
        Gdiplus::StringFormat format;
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        Gdiplus::RectF rect(left, lineTop, layout_.textWidth, lineHeight);
        graphics.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
    }

    void paintStaleDot(Gdiplus::Graphics& graphics, const DibCanvas& canvas) const {
        float diameter = 4 * layout_.scale;
        Gdiplus::SolidBrush brush{Gdiplus::Color(kStaleDotColor)};
        graphics.FillEllipse(&brush, canvas.width() - diameter - 2 * layout_.scale,
                             2 * layout_.scale, diameter, diameter);
    }

    PillLayout layout_;
    long long nowUnixSeconds_;
};

}  // namespace
