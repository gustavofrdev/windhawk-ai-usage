#pragma once
#include "pill_painter.h"
#include "usage_poller.h"

namespace {

constexpr UINT_PTR kPlacementTimerId = 1;
constexpr UINT kPlacementPollMs = 2000;
constexpr wchar_t kTaskbarClassName[] = L"Shell_TrayWnd";

// Centers the pill vertically on the taskbar, leftOffsetPixels from its left edge.
// Example: taskbarPillOrigin({0, 1032, 1920, 1080}, {250, 32}, 12) == POINT{12, 1040}
POINT taskbarPillOrigin(RECT taskbar, SIZE pill, int leftOffsetPixels) {
    LONG freeHeight = std::max<LONG>(0, taskbar.bottom - taskbar.top - pill.cy);
    return {taskbar.left + leftOffsetPixels, taskbar.top + freeHeight / 2};
}

// Layered, click-through window drawn over the left end of the taskbar. It is
// owned by the taskbar, so it stays above it without fighting other topmost
// windows. Create and pump it on the same thread; any thread may post
// kSnapshotChangedMsg to it.
// Example: PillWindow window(settings, store); window.create(); PillWindow::runMessageLoop();
class PillWindow {
public:
    PillWindow(const PillSettings& settings, const UsageSnapshotStore& store)
        : settings_(settings), store_(store) {}

    ~PillWindow() {
        if (window_ != nullptr) {
            DestroyWindow(window_);
        }
        UnregisterClassW(kWindowClassName, moduleInstance());
    }

    PillWindow(const PillWindow&) = delete;
    PillWindow& operator=(const PillWindow&) = delete;

    HWND create() {
        registerWindowClass();
        DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                        WS_EX_NOACTIVATE;
        HWND taskbar = FindWindowW(kTaskbarClassName, nullptr);
        window_ = CreateWindowExW(exStyle, kWindowClassName, L"AI Usage Pill", WS_POPUP, 0, 0, 1, 1,
                                  taskbar, nullptr, moduleInstance(), this);
        if (window_ == nullptr) {
            throwLastError("CreateWindowExW", kWindowClassName);
        }
        SetTimer(window_, kPlacementTimerId, kPlacementPollMs, nullptr);
        repaintSafely();
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        return window_;
    }

    static void runMessageLoop() {
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

private:
    // Inside explorer.exe the window belongs to the mod DLL, not to the exe.
    static HINSTANCE moduleInstance() {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&PillWindow::windowProc), &module);
        return module;
    }

    static void registerWindowClass() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &PillWindow::windowProc;
        windowClass.hInstance = moduleInstance();
        windowClass.lpszClassName = kWindowClassName;
        RegisterClassExW(&windowClass);
    }

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            CREATESTRUCTW* creation = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        }
        PillWindow* self = reinterpret_cast<PillWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self == nullptr) {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        return self->handleMessage(window, message, wParam, lParam);
    }

    LRESULT handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case kSnapshotChangedMsg:
            case WM_DISPLAYCHANGE:
            case WM_DPICHANGED:
            case WM_SETTINGCHANGE:
                repaintSafely();
                return 0;
            case WM_TIMER:
                refreshPlacement();
                return 0;
            case WM_DESTROY:
                window_ = nullptr;
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    // An exception escaping a window procedure would take explorer.exe down.
    void repaintSafely() {
        try {
            repaint();
        } catch (const std::exception& error) {
            Wh_Log(L"Pill repaint failed: %S", error.what());
        }
    }

    void repaint() {
        if (window_ == nullptr) {
            return;
        }
        UsageSnapshot snapshot = store_.read();
        std::vector<PillRow> rows = buildPillRows(settings_.vendorStyles, snapshot.usages);
        PillLayout layout = PillLayout::forDpi(GetDpiForWindow(window_));
        SIZE size = layout.pillSize(rows.size());
        DibCanvas canvas(size.cx, size.cy);
        PillPainter(layout).paint(canvas, rows, snapshot.stale);
        lastTaskbarRect_ = currentTaskbarRect();
        int leftOffset = static_cast<int>(std::lround(settings_.leftOffset * layout.scale));
        POINT origin = taskbarPillOrigin(lastTaskbarRect_, size, leftOffset);
        present(canvas, origin, opacityByte(snapshot.stale));
    }

    // Polled because the shell sends no message to other windows when the
    // taskbar moves or a fullscreen app starts.
    void refreshPlacement() {
        ShowWindow(window_, isFullscreenAppRunning() ? SW_HIDE : SW_SHOWNOACTIVATE);
        RECT taskbarRect = currentTaskbarRect();
        if (!EqualRect(&taskbarRect, &lastTaskbarRect_)) {
            repaintSafely();
        }
    }

    // Without a taskbar (explorer still starting) the pill uses the bottom
    // strip of the primary monitor, and the next poll moves it into place.
    static RECT currentTaskbarRect() {
        RECT taskbarRect{};
        HWND taskbar = FindWindowW(kTaskbarClassName, nullptr);
        if (taskbar != nullptr && GetWindowRect(taskbar, &taskbarRect)) {
            return taskbarRect;
        }
        MONITORINFO info{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
        return {info.rcMonitor.left, info.rcMonitor.bottom - 48, info.rcMonitor.right,
                info.rcMonitor.bottom};
    }

    static bool isFullscreenAppRunning() {
        QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;
        if (FAILED(SHQueryUserNotificationState(&state))) {
            return false;
        }
        switch (state) {
            case QUNS_BUSY:
            case QUNS_RUNNING_D3D_FULL_SCREEN:
            case QUNS_PRESENTATION_MODE:
                return true;
            default:
                return false;
        }
    }

    BYTE opacityByte(bool stale) const {
        int percent = stale ? settings_.opacityPercent / 2 : settings_.opacityPercent;
        return static_cast<BYTE>(percent * 255 / 100);
    }

    void present(const DibCanvas& canvas, POINT origin, BYTE alpha) {
        SIZE size{canvas.width(), canvas.height()};
        POINT source{0, 0};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
        UpdateLayeredWindow(window_, nullptr, &origin, &size, canvas.deviceContext(), &source, 0,
                            &blend, ULW_ALPHA);
    }

    const PillSettings& settings_;
    const UsageSnapshotStore& store_;
    HWND window_ = nullptr;
    RECT lastTaskbarRect_{};
};

}  // namespace
