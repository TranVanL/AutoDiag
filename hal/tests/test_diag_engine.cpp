#include "diag_engine.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;
int g_tests = 0;

void expectTrue(bool condition, const char* testName) {
    ++g_tests;
    if (!condition) {
        ++g_failures;
        std::cerr << "[FAIL] " << testName << "\n";
    }
}

class BlockingHal final : public autodiag::IDiagnosticHal {
public:
    Result SendAndReceive(const std::vector<uint8_t>&) override {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&] { return released_; });
        return Result{true, {0x62, 0xF1, 0x90, 'O', 'K'}, ""};
    }

    Result readProperty(uint32_t, uint32_t) override {
        return Result{false, {}, "not used"};
    }

    bool isReady() const override {
        return true;
    }

    void reset() override {
    }

    void release() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            released_ = true;
        }
        cv_.notify_all();
    }

private:
    mutable std::mutex mu_{};
    std::condition_variable cv_{};
    bool released_{false};
};

autodiag::DiagRequest makeReadVinRequest(uint32_t reqId) {
    autodiag::DiagRequest req{};
    req.requestId = reqId;
    req.service = autodiag::UdsService::ReadDataByIdentifier;
    req.dataId = static_cast<std::uint16_t>(autodiag::DiagProperty::VIN);
    return req;
}

bool waitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return predicate();
}

void testWorkerAliveAfterStartAndFalseAfterShutdown() {
    auto hal = std::make_unique<BlockingHal>();
    auto* halRaw = hal.get();
    autodiag::DiagEngine engine(std::move(hal));

    engine.start();
    const bool workerStarted = waitUntil([&] { return engine.isWorkerAlive(); }, std::chrono::milliseconds(300));
    expectTrue(workerStarted, "engine_worker_alive_after_start");

    halRaw->release();
    engine.shutdown();
    expectTrue(!engine.isWorkerAlive(), "engine_worker_false_after_shutdown");
}

void testQueueDepthTracking() {
    auto hal = std::make_unique<BlockingHal>();
    auto* halRaw = hal.get();
    autodiag::DiagEngine engine(std::move(hal));

    std::atomic<int> callbacks{0};
    engine.start();
    expectTrue(engine.submit(makeReadVinRequest(1), [&](const autodiag::DiagResponse&) {
        callbacks.fetch_add(1);
    }), "engine_submit_first_request");
    expectTrue(engine.submit(makeReadVinRequest(2), [&](const autodiag::DiagResponse&) {
        callbacks.fetch_add(1);
    }), "engine_submit_second_request");

    const bool depthObserved = waitUntil([&] { return engine.getQueueDepth() >= 1; }, std::chrono::milliseconds(400));
    expectTrue(depthObserved, "engine_queue_depth_observed_nonzero");

    halRaw->release();
    const bool drained = waitUntil([&] { return callbacks.load() == 2; }, std::chrono::milliseconds(400));
    expectTrue(drained, "engine_callbacks_drained_after_release");
    expectTrue(engine.getQueueDepth() == 0, "engine_queue_depth_zero_after_drain");

    engine.shutdown();
}

}  // namespace

int main() {
    testWorkerAliveAfterStartAndFalseAfterShutdown();
    testQueueDepthTracking();

    if (g_failures == 0) {
        std::cout << "All diag engine health tests passed. tests=" << g_tests << "\n";
        return 0;
    }

    std::cerr << "Diag engine health tests failed. failures=" << g_failures << " tests=" << g_tests << "\n";
    return 1;
}