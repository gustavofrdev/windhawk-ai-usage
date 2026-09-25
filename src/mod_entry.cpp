#pragma once
#include "pill_app.h"

namespace {

constexpr Gdiplus::ARGB kFallbackVendorColor = 0xFF9CA3AF;

// Reads the Windhawk settings page; empty or invalid fields fall back to the
// defaults so a half-filled settings page still shows a usable pill.
// Example: PillSettings settings = PillSettingsLoader().load();
class PillSettingsLoader {
public:
    PillSettings load() const {
        PillSettings defaults = defaultPillSettings();
        std::wstring command = readSetting(L"command");
        return {command.empty() ? defaults.command : command,
                std::clamp(Wh_GetIntSetting(L"refreshMinutes"), 1, 60),
                std::clamp(Wh_GetIntSetting(L"leftOffset"), 0, 2000),
                std::clamp(Wh_GetIntSetting(L"opacity"), 20, 100),
                readVendorStyles(defaults.vendorStyles)};
    }

private:
    template <typename... FormatArgs>
    static std::wstring readSetting(const wchar_t* settingName, FormatArgs... formatArgs) {
        PCWSTR rawValue = Wh_GetStringSetting(settingName, formatArgs...);
        std::wstring value = rawValue;
        Wh_FreeStringSetting(rawValue);
        return value;
    }

    static std::vector<VendorStyle> readVendorStyles(const std::vector<VendorStyle>& fallback) {
        std::vector<VendorStyle> styles;
        for (int index = 0;; ++index) {
            std::wstring vendorId = readSetting(L"vendors[%d].id", index);
            if (vendorId.empty()) {
                break;
            }
            styles.push_back({vendorId, readVendorColor(index)});
        }
        return styles.empty() ? fallback : styles;
    }

    static Gdiplus::ARGB readVendorColor(int index) {
        std::wstring hexColor = readSetting(L"vendors[%d].color", index);
        try {
            return parseHexColor(hexColor);
        } catch (const std::invalid_argument& error) {
            Wh_Log(L"vendors[%d].color: %S", index, error.what());
            return kFallbackVendorColor;
        }
    }
};

std::unique_ptr<PillApp> g_pillApp;

}  // namespace

BOOL Wh_ModInit() {
    g_pillApp = std::make_unique<PillApp>(PillSettingsLoader().load());
    if (!g_pillApp->start()) {
        Wh_Log(L"Another explorer.exe process already shows the pill");
    }
    return TRUE;
}

void Wh_ModUninit() {
    g_pillApp.reset();
}

void Wh_ModSettingsChanged() {
    Wh_ModUninit();
    Wh_ModInit();
}
