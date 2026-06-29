#pragma once

#include "diag_type.h"
#include "idiag_hal.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace autodiag {

class DiagEngine {
public:
    using Callback = std::function<void(const DiagResponse&)>;

    explicit DiagEngine(std::unique_ptr<IDiagnosticHal> hal);
    ~DiagEngine();

    DiagEngine(const DiagEngine&) = delete;
    DiagEngine& operator=(const DiagEngine&) = delete;

    void start();
    bool submit(const DiagRequest& req, Callback cb);
    void shutdown();
    std::size_t pendingCount() const;

private:
    void workerLoop();

    struct WorkItem {
        DiagRequest req{};
        Callback cb{};
    };

    std::unique_ptr<IDiagnosticHal> hal_{};
    mutable std::mutex mu_{};
    std::condition_variable cv_{};
    std::queue<WorkItem> queue_{};
    std::thread worker_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
};

}  // namespace autodiag


