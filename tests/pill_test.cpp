// Test harness for ai-usage-pill.wh.cpp, built with WH_EDITING so the Windhawk
// API calls become the header's no-op stubs.
//   pill_test.exe selftest <fixture.json>   unit checks, exit code 0 = pass
//   pill_test.exe preview <seconds>         shows the real pill on screen
//   pill_test.exe preview-stale <seconds>   same, with a failing command
#include "../ai-usage-pill.wh.cpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

int g_failures = 0;

void expectTrue(bool condition, const char* description) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) {
        ++g_failures;
    }
}

std::string readTextFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

bool parseThrows(const std::string& json) {
    try {
        UsageReportParser().parse(json);
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

void testParsesFixture(const char* fixturePath) {
    std::vector<VendorUsage> usages = UsageReportParser().parse(readTextFile(fixturePath));
    expectTrue(usages.size() == 2, "fixture: only the 2 ready vendors are kept");
    expectTrue(usages[0].vendorId == L"anthropic", "fixture: first vendor is anthropic");
    expectTrue(usages[0].sessionPercent == 10, "fixture: anthropic session = 10");
    expectTrue(usages[0].weeklyPercent == 52, "fixture: anthropic weekly = 52");
    expectTrue(usages[1].vendorId == L"openai", "fixture: second vendor is openai");
}

void testRejectsMalformedJson() {
    expectTrue(parseThrows("not json"), "malformed JSON throws");
    expectTrue(parseThrows("{\"other\":1}"), "object without entries throws");
}

void testMetricEdgeCases() {
    std::string json = R"({"entries":[{"id":"x","status":"ready","metrics":[
        {"window_secs":18000,"percent":150},{"window_secs":604800,"percent":null}]},
        {"id":"y","status":"ready","metrics":[{"window_secs":18000,"percent":-3},
        {"percent":70},{"window_secs":"7d","percent":80}]}]})";
    std::vector<VendorUsage> usages = UsageReportParser().parse(json);
    expectTrue(usages.size() == 2, "edge: bad metric does not drop the vendor list");
    expectTrue(usages[0].sessionPercent == 100, "edge: percent above 100 is clamped");
    expectTrue(usages[0].weeklyPercent == kUnknownPercent, "edge: null percent is unknown");
    expectTrue(usages[1].sessionPercent == kUnknownPercent, "edge: negative percent is unknown");
    expectTrue(usages[1].weeklyPercent == kUnknownPercent, "edge: missing weekly is unknown");
    expectTrue(usages[1].sessionPercent == kUnknownPercent,
               "edge: metric without numeric window_secs is ignored");
}

bool colorThrows(const wchar_t* hexColor) {
    try {
        parseHexColor(hexColor);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void testParsesColors() {
    expectTrue(parseHexColor(L"#D97757") == 0xFFD97757, "color: #D97757 parses");
    expectTrue(parseHexColor(L"10a37f") == 0xFF10A37F, "color: without # parses");
    expectTrue(colorThrows(L"#ZZ0000"), "color: non-hex throws");
    expectTrue(colorThrows(L"#FFF"), "color: short form throws");
}

void testBuildsRowsInSettingsOrder() {
    std::vector<VendorStyle> styles = {{L"openai", 0xFF10A37F}, {L"anthropic", 0xFFD97757}};
    std::vector<VendorUsage> usages = {{L"anthropic", 40, 50}};
    std::vector<PillRow> rows = buildPillRows(styles, usages);
    expectTrue(rows.size() == 2, "rows: one per configured vendor");
    expectTrue(rows[0].sessionPercent == kUnknownPercent, "rows: vendor without data is unknown");
    expectTrue(rows[1].sessionPercent == 40 && rows[1].color == 0xFFD97757,
               "rows: vendor data matched by id");
    expectTrue(rows[1].vendorId == L"anthropic", "rows: keep the vendor id for the logo");
}

void testFindsBrandLogos() {
    expectTrue(findBrandLogo(L"anthropic") != nullptr, "logo: anthropic has a logo");
    expectTrue(findBrandLogo(L"openai") != nullptr, "logo: openai has a logo");
    expectTrue(findBrandLogo(L"cursor") == nullptr, "logo: unknown vendor has none");
}

void testBuildsLogoPath() {
    GdiplusSession gdiplus;
    Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);
    LogoPathBuilder(Gdiplus::RectF(10, 20, 16, 16), path).build(*findBrandLogo(L"openai"));
    Gdiplus::RectF bounds;
    path.GetBounds(&bounds);
    expectTrue(path.GetPointCount() > 50, "logo path: has the SVG points");
    expectTrue(bounds.X >= 10 && bounds.GetRight() <= 26 && bounds.Y >= 20 &&
                   bounds.GetBottom() <= 36,
               "logo path: scaled into its box");
}

void testPlacesPillOnTaskbar() {
    RECT taskbar{0, 1032, 1920, 1080};
    POINT origin = taskbarPillOrigin(taskbar, SIZE{250, 32}, 12);
    expectTrue(origin.x == 12, "taskbar: pill starts at the left offset");
    expectTrue(origin.y == 1040, "taskbar: pill is vertically centered");
    SIZE pill = PillLayout::forDpi(96).pillSize(2);
    expectTrue(pill.cy <= 40, "taskbar: two vendors fit the 48 px taskbar");
}

void testRunsHiddenProcess() {
    UniqueHandle cancelEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    HiddenProcessRunner runner(5000, cancelEvent.get());
    std::string output = runner.run(L"cmd.exe /c echo pill-ok");
    expectTrue(output.find("pill-ok") != std::string::npos, "runner: captures stdout");
}

int runSelfTest(const char* fixturePath) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    testParsesFixture(fixturePath);
    testRejectsMalformedJson();
    testMetricEdgeCases();
    testParsesColors();
    testBuildsRowsInSettingsOrder();
    testRunsHiddenProcess();
    testFindsBrandLogos();
    testBuildsLogoPath();
    testPlacesPillOnTaskbar();
    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}

int runPreview(int seconds, bool failing) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    PillSettings settings = defaultPillSettings();
    if (failing) {
        settings.command = L"cmd.exe /c exit 3";
    }
    // A separate lock name lets the preview run while the installed mod is active.
    PillApp app(settings, L"Local\\AiUsagePillPreview");
    if (!app.start()) {
        std::printf("another pill instance is running\n");
        return 1;
    }
    Sleep(static_cast<DWORD>(seconds) * 1000);
    app.stop();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "selftest" && argc > 2) {
        return runSelfTest(argv[2]);
    }
    if (mode == "preview" || mode == "preview-stale") {
        return runPreview(argc > 2 ? std::atoi(argv[2]) : 15, mode == "preview-stale");
    }
    std::printf("usage: pill_test selftest <fixture.json> | preview[-stale] [seconds]\n");
    return 2;
}
