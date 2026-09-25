#pragma once
#include "hidden_process_runner.h"
#include "usage_report_parser.h"

namespace {

struct UsageSnapshot {
    std::vector<VendorUsage> usages;
    bool stale = true;
};

// Thread-safe holder of the latest usage: the poller writes, the window reads.
// Example: store.publishSuccess(usages); UsageSnapshot current = store.read();
class UsageSnapshotStore {
public:
    void publishSuccess(std::vector<VendorUsage> usages) {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.usages = std::move(usages);
        snapshot_.stale = false;
    }

    // Keeps the last known numbers so the pill stays readable while dimmed.
    void publishFailure() {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.stale = true;
    }

    UsageSnapshot read() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }

private:
    mutable std::mutex mutex_;
    UsageSnapshot snapshot_;
};

// Background thread that refreshes the store right away and then every
// refreshMinutes, until stop() is called.
// Example: UsagePoller poller(command, 2, store, [] { repaintPill(); }); poller.start();
class UsagePoller {
public:
    UsagePoller(std::wstring command, int refreshMinutes, UsageSnapshotStore& store,
                std::function<void()> onPublished)
        : command_(std::move(command)),
          intervalMs_(static_cast<DWORD>(refreshMinutes) * 60000),
          store_(store),
          onPublished_(std::move(onPublished)),
          stopEvent_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    ~UsagePoller() { stop(); }

    void start() { worker_ = std::thread(&UsagePoller::runLoop, this); }

    // Also cancels a command that is still running, so explorer.exe never waits
    // for the full command timeout when the mod is unloaded.
    void stop() {
        SetEvent(stopEvent_.get());
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    // An exception escaping a std::thread calls std::terminate and would take
    // explorer.exe down, so a failed COM setup is logged and ends the thread.
    void runLoop() {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (const winrt::hresult_error& error) {
            Wh_Log(L"COM apartment setup failed: %s", error.message().c_str());
            return;
        }
        do {
            refreshOnce();
        } while (WaitForSingleObject(stopEvent_.get(), intervalMs_) == WAIT_TIMEOUT);
        winrt::uninit_apartment();
    }

    void refreshOnce() {
        try {
            HiddenProcessRunner runner(kCommandTimeoutMs, stopEvent_.get());
            store_.publishSuccess(UsageReportParser().parse(runner.run(command_)));
        } catch (const std::exception& error) {
            Wh_Log(L"Usage refresh failed: %S", error.what());
            store_.publishFailure();
        } catch (const winrt::hresult_error& error) {
            Wh_Log(L"Usage refresh failed: %s", error.message().c_str());
            store_.publishFailure();
        }
        onPublished_();
    }

    std::wstring command_;
    DWORD intervalMs_;
    UsageSnapshotStore& store_;
    std::function<void()> onPublished_;
    UniqueHandle stopEvent_;
    std::thread worker_;
};

}  // namespace
