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

bool DiagEngine::submit(RequestPriority priority, const DiagRequest& req, Callback cb) {
    if (!cb) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!running_.load() || stop_.load()) {
            return false;
        }
        queue_.emplace(priority, seq_.fetch_add(1), std::move(req), std::move(cb));
        queueDepth_.fetch_add(1);
    }
    cv_.notify_one();
    return true;
}

std::size_t DiagEngine::pendingCount() const {
    return static_cast<std::size_t>(queueDepth_.load());
}

bool DiagEngine::isWorkerAlive() const {
    return workerAlive_.load();
}

int DiagEngine::getQueueDepth() const {
    return queueDepth_.load();
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
    queueDepth_.store(0);
    running_.store(false);
}

void DiagEngine::workerLoop() {
    workerAlive_.store(true);
    while (true) {
       
        DiagRequest req{};
        Callback cb{};
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return !queue_.empty() || stop_.load(); });

            if (stop_.load() && queue_.empty()) {
                break;
            }

            req = std::move(std::get<2>(queue_.top()));
            cb = std::move(std::get<3>(queue_.top()));
            queue_.pop();
            queueDepth_.fetch_sub(1);
        }

        
        const auto t0 = std::chrono::steady_clock::now();
        DiagResponse response{};
        response.requestId = req.requestId;

        if (hal_ == nullptr) {
            response.positive = false;
            response.nrc = Nrc::EngineNotReady;
            response.valueString = "HAL is null";
        } else {
            // hal_->isReady() is NOT checked here intentionally:
            // SendAndReceive performs lazy reconnect if socket is down.
            const auto encoded = encode(req);
            const auto halResult = hal_->SendAndReceive(encoded);
            if (!halResult.success) {
                response.positive = false;
                response.nrc = Nrc::CommunicationError;
                response.valueString = halResult.error;
            } else {
                response = decode(req.requestId, halResult.data);
            }
        }

        const auto t1 = std::chrono::steady_clock::now();
        response.latencyUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        // Cout the latency for debugging purposes
        std::cout << "[trace] DiagEngine::processRequest" 
                  << " requestId=" << response.requestId
                  << " latencyUs=" << response.latencyUs
                  << " QueueDepth=" << queueDepth_.load()
                  << "\n";

        try {
            cb(response);
        } catch (const std::exception& ex) {
            std::cerr << "DiagEngine callback exception: " << ex.what() << "\n";
        } catch (...) {
            std::cerr << "DiagEngine callback unknown exception\n";
        }

        // item.session.reset();
    }
    workerAlive_.store(false);
}

}  // namespace autodiag
