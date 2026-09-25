#pragma once
#include "pill_window.h"

namespace {

// Owns the poller and the UI thread. Only one instance runs per logon session,
// because Windhawk injects the mod into every explorer.exe process.
// Example: PillApp app(settings); if (app.start()) { ...; app.stop(); }
class PillApp {
public:
    explicit PillApp(PillSettings settings, std::wstring instanceMutexName = kInstanceMutexName)
        : settings_(std::move(settings)),
          instanceMutexName_(std::move(instanceMutexName)),
          windowReady_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    ~PillApp() { stop(); }

    PillApp(const PillApp&) = delete;
    PillApp& operator=(const PillApp&) = delete;

    // Returns false when another process already shows the pill.
    bool start() {
        instanceMutex_.reset(CreateMutexW(nullptr, FALSE, instanceMutexName_.c_str()));
        if (instanceMutex_.get() == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
            instanceMutex_.reset();
            return false;
        }
        uiThread_ = std::thread(&PillApp::runUiThread, this);
        WaitForSingleObject(windowReady_.get(), 5000);
        poller_ = std::make_unique<UsagePoller>(settings_.command, settings_.refreshMinutes, store_,
                                                [this] { notifyWindow(); });
        poller_->start();
        return true;
    }

    // The poller stops first so nothing posts to the window while it closes.
    void stop() {
        poller_.reset();
        HWND window = window_.exchange(nullptr);
        if (window != nullptr) {
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
        if (uiThread_.joinable()) {
            uiThread_.join();
        }
        instanceMutex_.reset();
    }

private:
    void runUiThread() {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        try {
            GdiplusSession gdiplus;
            PillWindow window(settings_, store_);
            window_.store(window.create());
            SetEvent(windowReady_.get());
            PillWindow::runMessageLoop();
        } catch (const std::exception& error) {
            Wh_Log(L"Pill window failed: %S", error.what());
            SetEvent(windowReady_.get());
        }
    }

    void notifyWindow() {
        HWND window = window_.load();
        if (window != nullptr) {
            PostMessageW(window, kSnapshotChangedMsg, 0, 0);
        }
    }

    PillSettings settings_;
    std::wstring instanceMutexName_;
    UsageSnapshotStore store_;
    UniqueHandle windowReady_;
    UniqueHandle instanceMutex_;
    std::atomic<HWND> window_{nullptr};
    std::thread uiThread_;
    std::unique_ptr<UsagePoller> poller_;
};

}  // namespace
