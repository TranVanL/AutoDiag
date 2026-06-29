#include "diag_engine.h"

#include "uds_codec.h"

#include <chrono>
#include <iostream>
#include <utility>

namespace autodiag {

DiagEngine::DiagEngine(std::unique_ptr<IDiagnosticHal> hal)
    : hal_(std::move(hal)) {
}

DiagEngine::~DiagEngine() {
    shutdown();
}

void DiagEngine::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    stop_.store(false);
    try {
        worker_ = std::thread(&DiagEngine::workerLoop, this);
    } catch (...) {
        running_.store(false);
        throw;
    }
}

bool DiagEngine::submit(const DiagRequest& req, Callback cb) {
    if (!cb) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!running_.load() || stop_.load()) {
            return false;
        }
        queue_.push(WorkItem{req, std::move(cb)});
    }
    cv_.notify_one();
    return true;
}

std::size_t DiagEngine::pendingCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    return queue_.size();
}

void DiagEngine::shutdown() {
    if (!running_.load()) {
        return;
    }

    stop_.store(true);
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }

    {
        std::lock_guard<std::mutex> lk(mu_);
        while (!queue_.empty()) {
            queue_.pop();
        }
    }
    running_.store(false);
}

void DiagEngine::workerLoop() {
    while (true) {
        WorkItem item{};

        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return !queue_.empty() || stop_.load(); });

            if (stop_.load() && queue_.empty()) {
                break;
            }

            item = std::move(queue_.front());
            queue_.pop();
        }

        const auto t0 = std::chrono::steady_clock::now();
        DiagResponse response{};
        response.requestId = item.req.requestId;

        if (hal_ == nullptr || !hal_->isReady()) {
            response.positive = false;
            response.nrc = Nrc::RequestOutOfRange;
            response.valueString = "HAL not ready";
        } else {
            const auto encoded = encode(item.req);
            const auto halResult = hal_->SendAndReceive(encoded);
            if (!halResult.success) {
                response.positive = false;
                response.nrc = Nrc::RequestOutOfRange;
                response.valueString = halResult.error;
            } else {
                response = decode(item.req.requestId, halResult.data);
            }
        }

        const auto t1 = std::chrono::steady_clock::now();
        response.latencyUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        try {
            item.cb(response);
        } catch (const std::exception& ex) {
            std::cerr << "DiagEngine callback exception: " << ex.what() << "\n";
        } catch (...) {
            std::cerr << "DiagEngine callback unknown exception\n";
        }
    }
}

}  // namespace autodiag
