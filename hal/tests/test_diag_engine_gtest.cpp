#include "diag_engine.h"

#include "diag_type.h"
#include "idiag_hal.h"
#include "uds_codec.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace autodiag {
namespace {

class BlockingHal final : public IDiagnosticHal {
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

    void reset() override {}
    void flashFirmware(const uint8_t*, size_t) override {}

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

DiagRequest makeReadVinRequest(uint32_t reqId) {
    DiagRequest req{};
    req.requestId = reqId;
    req.service = UdsService::ReadDataByIdentifier;
    req.dataId = static_cast<std::uint16_t>(DiagProperty::VIN);
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

class DiagEngineTest : public ::testing::Test {};

TEST_F(DiagEngineTest, WorkerAliveAfterStartAndFalseAfterShutdown) {
    auto hal = std::make_unique<BlockingHal>();
    auto* halRaw = hal.get();
    DiagEngine engine(std::move(hal));

    engine.start();
    const bool workerStarted = waitUntil([&] { return engine.isWorkerAlive(); }, std::chrono::milliseconds(300));
    EXPECT_TRUE(workerStarted);

    halRaw->release();
    engine.shutdown();
    EXPECT_FALSE(engine.isWorkerAlive());
}

TEST_F(DiagEngineTest, QueueDepthTracking) {
    auto hal = std::make_unique<BlockingHal>();
    auto* halRaw = hal.get();
    DiagEngine engine(std::move(hal));

    std::atomic<int> callbacks{0};
    engine.start();
    EXPECT_TRUE(engine.submit(RequestPriority::NORMAL, makeReadVinRequest(1), [&](const DiagResponse&) {
        callbacks.fetch_add(1);
    }));
    EXPECT_TRUE(engine.submit(RequestPriority::NORMAL, makeReadVinRequest(2), [&](const DiagResponse&) {
        callbacks.fetch_add(1);
    }));

    const bool depthObserved = waitUntil([&] { return engine.getQueueDepth() >= 1; }, std::chrono::milliseconds(400));
    EXPECT_TRUE(depthObserved);

    halRaw->release();
    const bool drained = waitUntil([&] { return callbacks.load() == 2; }, std::chrono::milliseconds(400));
    EXPECT_TRUE(drained);
    EXPECT_EQ(engine.getQueueDepth(), 0);

    engine.shutdown();
}

}  // namespace
}  // namespace autodiag
