#pragma once
#include "common.h"

namespace {

// Turns `ai-usagebar usage --json` output into one VendorUsage per ready vendor.
// This is the only place that touches the WinRT JSON API.
// Example: std::vector<VendorUsage> usages = UsageReportParser().parse(jsonText);
class UsageReportParser {
public:
    std::vector<VendorUsage> parse(const std::string& utf8Json) const {
        json::JsonObject root{nullptr};
        if (!json::JsonObject::TryParse(utf8ToWide(utf8Json), root) || !root.HasKey(L"entries")) {
            throw std::runtime_error("ai-usagebar output rejected: expected a JSON object with an "
                                     "'entries' array, received: " + utf8Json.substr(0, 120));
        }
        std::vector<VendorUsage> usages;
        for (const json::IJsonValue& entryValue : root.GetNamedArray(L"entries")) {
            appendIfReady(entryValue, usages);
        }
        return usages;
    }

private:
    static void appendIfReady(const json::IJsonValue& entryValue, std::vector<VendorUsage>& usages) {
        if (entryValue.ValueType() != json::JsonValueType::Object) {
            return;
        }
        json::JsonObject entry = entryValue.GetObject();
        if (readString(entry, L"status") != L"ready") {
            return;
        }
        VendorUsage usage{readString(entry, L"id")};
        json::JsonArray metrics = entry.GetNamedArray(L"metrics", json::JsonArray());
        for (const json::IJsonValue& metricValue : metrics) {
            assignMetric(metricValue, usage);
        }
        usages.push_back(usage);
    }

    static void assignMetric(const json::IJsonValue& metricValue, VendorUsage& usage) {
        if (metricValue.ValueType() != json::JsonValueType::Object) {
            return;
        }
        json::JsonObject metric = metricValue.GetObject();
        double windowSecs = readNumber(metric, L"window_secs");
        if (std::isnan(windowSecs)) {
            return;
        }
        int percent = clampPercent(readNumber(metric, L"percent"));
        long long resetAt = parseIsoUtcSeconds(readString(metric, L"reset_at"));
        switch (static_cast<int>(windowSecs)) {
            case kSessionWindowSecs:
                usage.sessionPercent = percent;
                usage.sessionResetAt = resetAt;
                break;
            case kWeeklyWindowSecs:
                usage.weeklyPercent = percent;
                usage.weeklyResetAt = resetAt;
                break;
            default:
                break;
        }
    }

    static std::wstring readString(const json::JsonObject& object, const wchar_t* key) {
        json::IJsonValue value = object.GetNamedValue(key, json::JsonValue::CreateNullValue());
        bool isString = value.ValueType() == json::JsonValueType::String;
        return isString ? std::wstring(value.GetString()) : std::wstring();
    }

    static double readNumber(const json::JsonObject& object, const wchar_t* key) {
        json::IJsonValue value = object.GetNamedValue(key, json::JsonValue::CreateNullValue());
        bool isNumber = value.ValueType() == json::JsonValueType::Number;
        return isNumber ? value.GetNumber() : std::nan("");
    }
};

}  // namespace
