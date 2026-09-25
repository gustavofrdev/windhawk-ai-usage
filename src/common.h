#pragma once
#include "mod_metadata.cpp"

namespace {

namespace json = winrt::Windows::Data::Json;

constexpr int kSessionWindowSecs = 18000;
constexpr int kWeeklyWindowSecs = 604800;
constexpr int kUnknownPercent = -1;
constexpr long long kUnknownResetAt = 0;
constexpr long long kInvalidUtcOffset = LLONG_MIN;
constexpr long long kUnixEpochAsFileTimeSeconds = 11644473600LL;
constexpr int kWarnPercent = 90;
constexpr DWORD kCommandTimeoutMs = 30000;
constexpr size_t kMaxCommandOutput = 1 << 20;
constexpr UINT kSnapshotChangedMsg = WM_APP + 1;
constexpr wchar_t kWindowClassName[] = L"AiUsagePillWindow";
constexpr wchar_t kInstanceMutexName[] = L"Local\\AiUsagePillInstance";

// Reset times are Unix seconds; kUnknownResetAt when ai-usagebar gave none.
struct VendorUsage {
    std::wstring vendorId;
    int sessionPercent = kUnknownPercent;
    int weeklyPercent = kUnknownPercent;
    long long sessionResetAt = kUnknownResetAt;
    long long weeklyResetAt = kUnknownResetAt;
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
    long long sessionResetAt;
    long long weeklyResetAt;
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

PillRow buildPillRow(const VendorStyle& style, const VendorUsage* usage) {
    if (usage == nullptr) {
        return {style.vendorId, style.color, kUnknownPercent, kUnknownPercent, kUnknownResetAt,
                kUnknownResetAt};
    }
    return {style.vendorId,       style.color,          usage->sessionPercent,
            usage->weeklyPercent, usage->sessionResetAt, usage->weeklyResetAt};
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
        rows.push_back(buildPillRow(style, match == usages.end() ? nullptr : &*match));
    }
    return rows;
}

long long fileTimeToUnixSeconds(FILETIME fileTime) {
    ULARGE_INTEGER ticks{{fileTime.dwLowDateTime, fileTime.dwHighDateTime}};
    return static_cast<long long>(ticks.QuadPart / 10000000ULL) - kUnixEpochAsFileTimeSeconds;
}

long long currentUnixSeconds() {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    return fileTimeToUnixSeconds(now);
}

// Reads what follows the seconds of an ISO 8601 time: optional fraction, then
// "Z" or "+hh:mm" / "-hh:mm". Returns the offset from UTC in seconds.
long long parseUtcOffsetSeconds(const std::wstring& suffix) {
    size_t zoneStart = suffix.starts_with(L".") ? suffix.find_first_not_of(L"0123456789", 1) : 0;
    std::wstring zone = zoneStart == std::wstring::npos ? L"" : suffix.substr(zoneStart);
    if (zone == L"Z") {
        return 0;
    }
    wchar_t sign = 0;
    int hours = 0;
    int minutes = 0;
    bool matched = swscanf(zone.c_str(), L"%lc%2d:%2d", &sign, &hours, &minutes) == 3;
    if (!matched || (sign != L'+' && sign != L'-')) {
        return kInvalidUtcOffset;
    }
    long long seconds = hours * 3600LL + minutes * 60LL;
    return sign == L'-' ? -seconds : seconds;
}

// Reads an ISO 8601 instant, as ai-usagebar writes in reset_at, into Unix
// seconds; anything unreadable becomes kUnknownResetAt.
// Example: parseIsoUtcSeconds(L"2026-09-25T23:10:00.49Z") == 1790377800
long long parseIsoUtcSeconds(const std::wstring& isoText) {
    SYSTEMTIME fields{};
    int consumed = 0;
    int assigned = swscanf(isoText.c_str(), L"%4hu-%2hu-%2huT%2hu:%2hu:%2hu%n", &fields.wYear,
                           &fields.wMonth, &fields.wDay, &fields.wHour, &fields.wMinute,
                           &fields.wSecond, &consumed);
    FILETIME fileTime{};
    if (assigned != 6 || !SystemTimeToFileTime(&fields, &fileTime)) {
        return kUnknownResetAt;
    }
    long long offsetSeconds = parseUtcOffsetSeconds(isoText.substr(consumed));
    if (offsetSeconds == kInvalidUtcOffset) {
        return kUnknownResetAt;
    }
    return fileTimeToUnixSeconds(fileTime) - offsetSeconds;
}

std::wstring twoDigits(long long value) {
    return (value < 10 ? L"0" : L"") + std::to_wstring(value);
}

// Time left until a limit resets, floored: minutes under an hour, hours and
// minutes under a day, whole days beyond that. Blank when unknown.
// Example: formatResetCountdown(now + 2 * 3600 + 600, now) == L"↻ 2h10"
std::wstring formatResetCountdown(long long resetAt, long long now) {
    if (resetAt == kUnknownResetAt) {
        return L"";
    }
    long long minutesLeft = std::max(0LL, resetAt - now) / 60;
    if (minutesLeft < 60) {
        return L"↻ " + std::to_wstring(minutesLeft) + L"m";
    }
    if (minutesLeft < 24 * 60) {
        return L"↻ " + std::to_wstring(minutesLeft / 60) + L"h" + twoDigits(minutesLeft % 60);
    }
    return L"↻ " + std::to_wstring(minutesLeft / (24 * 60)) + L"d";
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
