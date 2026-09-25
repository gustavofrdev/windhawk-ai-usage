#pragma once
#include "mod_metadata.cpp"

namespace {

namespace json = winrt::Windows::Data::Json;

constexpr int kSessionWindowSecs = 18000;
constexpr int kWeeklyWindowSecs = 604800;
constexpr int kUnknownPercent = -1;
constexpr int kWarnPercent = 90;
constexpr DWORD kCommandTimeoutMs = 30000;
constexpr size_t kMaxCommandOutput = 1 << 20;
constexpr UINT kSnapshotChangedMsg = WM_APP + 1;
constexpr wchar_t kWindowClassName[] = L"AiUsagePillWindow";
constexpr wchar_t kInstanceMutexName[] = L"Local\\AiUsagePillInstance";

struct VendorUsage {
    std::wstring vendorId;
    int sessionPercent = kUnknownPercent;
    int weeklyPercent = kUnknownPercent;
};

struct VendorStyle {
    std::wstring vendorId;
    Gdiplus::ARGB color;
};

struct PillRow {
    std::wstring vendorId;
    Gdiplus::ARGB color;
    int sessionPercent;
    int weeklyPercent;
};

struct PillSettings {
    std::wstring command;
    int refreshMinutes;
    int leftOffset;
    int opacityPercent;
    std::vector<VendorStyle> vendorStyles;
};

PillSettings defaultPillSettings() {
    return {L"wsl.exe -e sh -lc \"ai-usagebar usage --json\"", 5, 12, 100,
            {{L"anthropic", 0xFFD97757}, {L"openai", 0xFF10A37F}}};
}

std::wstring utf8ToWide(const std::string& utf8Text) {
    int length = MultiByteToWideChar(CP_UTF8, 0, utf8Text.data(),
                                     static_cast<int>(utf8Text.size()), nullptr, 0);
    std::wstring wideText(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Text.data(), static_cast<int>(utf8Text.size()),
                        wideText.data(), length);
    return wideText;
}

std::string wideToUtf8(const std::wstring& wideText) {
    int length = WideCharToMultiByte(CP_UTF8, 0, wideText.data(), static_cast<int>(wideText.size()),
                                     nullptr, 0, nullptr, nullptr);
    std::string utf8Text(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideText.data(), static_cast<int>(wideText.size()),
                        utf8Text.data(), length, nullptr, nullptr);
    return utf8Text;
}

// Parses "#RRGGBB" (or "RRGGBB") into an opaque GDI+ ARGB.
// Example: parseHexColor(L"#D97757") == 0xFFD97757
Gdiplus::ARGB parseHexColor(const std::wstring& hexColor) {
    std::wstring digits = hexColor.starts_with(L"#") ? hexColor.substr(1) : hexColor;
    bool isHex = digits.size() == 6 && digits.find_first_not_of(L"0123456789abcdefABCDEF") ==
                                           std::wstring::npos;
    if (!isHex) {
        throw std::invalid_argument("Color '" + wideToUtf8(hexColor) +
                                    "' rejected; expected #RRGGBB, e.g. #D97757");
    }
    return 0xFF000000 | static_cast<Gdiplus::ARGB>(std::stoul(digits, nullptr, 16));
}

int clampPercent(double rawPercent) {
    if (std::isnan(rawPercent) || rawPercent < 0) {
        return kUnknownPercent;
    }
    return static_cast<int>(std::lround(std::min(rawPercent, 100.0)));
}

// Pairs each configured vendor (in settings order) with its latest usage.
// Example: buildPillRows(settings.vendorStyles, snapshot.usages)
std::vector<PillRow> buildPillRows(const std::vector<VendorStyle>& styles,
                                   const std::vector<VendorUsage>& usages) {
    std::vector<PillRow> rows;
    for (const VendorStyle& style : styles) {
        auto match = std::find_if(usages.begin(), usages.end(), [&](const VendorUsage& usage) {
            return usage.vendorId == style.vendorId;
        });
        bool found = match != usages.end();
        rows.push_back({style.vendorId, style.color, found ? match->sessionPercent : kUnknownPercent,
                        found ? match->weeklyPercent : kUnknownPercent});
    }
    return rows;
}

class UniqueHandle {
public:
    explicit UniqueHandle(HANDLE handle = nullptr) : handle_(handle) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    HANDLE get() const { return handle_; }

    void reset(HANDLE handle = nullptr) {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_;
};

[[noreturn]] void throwLastError(const std::string& operation, const std::wstring& context) {
    throw std::runtime_error(operation + " failed with Win32 error " +
                             std::to_string(GetLastError()) + " for '" + wideToUtf8(context) + "'");
}

}  // namespace
