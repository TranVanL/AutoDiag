#pragma once

#include "diag_type.h"
#include "idiag_hal.h"
#include "session_state.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

namespace autodiag {

class DiagEngine {
public:
    using Callback = std::function<void(const DiagResponse&)>;

    
    explicit DiagEngine(std::unique_ptr<IDiagnosticHal> hal);
    ~DiagEngine();

    
    DiagEngine(const DiagEngine&) = delete;
    DiagEngine& operator=(const DiagEngine&) = delete;

    void start();
    bool submit(RequestPriority priority, const DiagRequest& req, Callback cb);
    void shutdown();
    std::size_t pendingCount() const;
    bool isWorkerAlive() const;
    int getQueueDepth() const;
    
    IDiagnosticHal* getHal() const { return hal_.get(); }

private:
    void workerLoop();

    using QueueItem = std::tuple<RequestPriority , int , DiagRequest, Callback>;
    bool CompareQueueItem(const QueueItem& a, const QueueItem& b) const {
        if (std::get<0>(a) != std::get<0>(b)) {
            return std::get<0>(a) < std::get<0>(b);
        }
        return std::get<1>(a) <= std::get<1>(b);
    }
    std::unique_ptr<IDiagnosticHal> hal_{};
    mutable std::mutex mu_{};
    std::condition_variable cv_{};
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::function<bool(const QueueItem&, const QueueItem&)>> queue_;
    std::thread worker_{};
    std::atomic<int> seq_{0};
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<bool> workerAlive_{false};
    std::atomic<int> queueDepth_{0};
};

}  // namespace autodiag


