#pragma once
#include "brand_logo_paths.h"

namespace {

// Opcodes written by tools/svg_to_logo.py into the float arrays.
constexpr int kLogoMove = 0;
constexpr int kLogoLine = 1;
constexpr int kLogoCubic = 2;
constexpr int kLogoClose = 3;
constexpr float kLogoViewBox = 100.0f;

struct BrandLogoShape {
    const wchar_t* vendorId;
    const float* commands;
    size_t commandCount;
};

constexpr BrandLogoShape kBrandLogos[] = {
    {L"anthropic", kAnthropicLogo, std::size(kAnthropicLogo)},
    {L"openai", kOpenaiLogo, std::size(kOpenaiLogo)},
};

// Returns the logo drawn before a vendor, or nullptr when there is none.
// Example: if (const BrandLogoShape* logo = findBrandLogo(L"anthropic")) { ... }
const BrandLogoShape* findBrandLogo(const std::wstring& vendorId) {
    for (const BrandLogoShape& logo : kBrandLogos) {
        if (vendorId == logo.vendorId) {
            return &logo;
        }
    }
    return nullptr;
}

// Replays a generated logo into a GDI+ path scaled into a square box.
// Example: LogoPathBuilder(RectF(x, y, 16, 16), path).build(*findBrandLogo(L"openai"));
class LogoPathBuilder {
public:
    LogoPathBuilder(Gdiplus::RectF box, Gdiplus::GraphicsPath& path) : box_(box), path_(path) {}

    void build(const BrandLogoShape& logo) {
        size_t index = 0;
        while (index < logo.commandCount) {
            index = appendCommand(logo.commands, index);
        }
    }

private:
    size_t appendCommand(const float* commands, size_t index) {
        switch (static_cast<int>(commands[index])) {
            case kLogoMove:
                path_.StartFigure();
                current_ = pointAt(commands, index + 1);
                return index + 3;
            case kLogoLine:
                return appendLine(commands, index);
            case kLogoCubic:
                return appendCubic(commands, index);
            case kLogoClose:
                path_.CloseFigure();
                return index + 1;
            default:
                throw std::invalid_argument("Logo opcode " + std::to_string(commands[index]) +
                                            " at index " + std::to_string(index) +
                                            " rejected; expected 0-3 from tools/svg_to_logo.py");
        }
    }

    size_t appendLine(const float* commands, size_t index) {
        Gdiplus::PointF next = pointAt(commands, index + 1);
        path_.AddLine(current_, next);
        current_ = next;
        return index + 3;
    }

    size_t appendCubic(const float* commands, size_t index) {
        Gdiplus::PointF next = pointAt(commands, index + 5);
        path_.AddBezier(current_, pointAt(commands, index + 1), pointAt(commands, index + 3), next);
        current_ = next;
        return index + 7;
    }

    Gdiplus::PointF pointAt(const float* commands, size_t index) const {
        return {box_.X + commands[index] * box_.Width / kLogoViewBox,
                box_.Y + commands[index + 1] * box_.Height / kLogoViewBox};
    }

    Gdiplus::RectF box_;
    Gdiplus::GraphicsPath& path_;
    Gdiplus::PointF current_;
};

}  // namespace
