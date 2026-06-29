#include "diag_engine.h"

#include "diag_type.h"
#include "idiag_hal.h"
#include "mock_diag_hal.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
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

void expectEqSize(std::size_t actual, std::size_t expected, const char* testName) {
    ++g_tests;
    if (actual != expected) {
        ++g_failures;
        std::cerr << "[FAIL] " << testName << " expected=" << expected
                  << " actual=" << actual << "\n";
    }
}

class NotReadyHal final : public autodiag::IDiagnosticHal {
public:
    Result SendAndReceive(const std::vector<uint8_t>&) override {
        return {false, {}, "HAL transport failure"};
    }

    bool isReady() const override {
        return false;
    }

    void reset() override {
    }
};

void testSubmitSingleRequestCallbackFires() {
    auto hal = std::make_unique<autodiag::MockDiagnosticHal>();
    autodiag::DiagEngine engine(std::move(hal));
    engine.start();

    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    autodiag::DiagResponse out{};

    autodiag::DiagRequest req{};
    req.requestId = 1;
    req.service = autodiag::UdsService::ReadDataByIdentifier;
    req.dataId = static_cast<std::uint16_t>(autodiag::DiagProperty::VIN);

    const bool accepted = engine.submit(req, [&](const autodiag::DiagResponse& resp) {
        std::lock_guard<std::mutex> lk(mu);
        out = resp;
        done = true;
        cv.notify_one();
    });

    expectTrue(accepted, "engine_submit_single_accepted");

    std::unique_lock<std::mutex> lk(mu);
    const bool completed = cv.wait_for(lk, std::chrono::milliseconds(300), [&] { return done; });
    expectTrue(completed, "engine_submit_single_callback_fired");
    if (completed) {
        expectTrue(out.positive, "engine_submit_single_positive");
        expectTrue(!out.valueString.empty(), "engine_submit_single_has_value");
    }

    engine.shutdown();
}

void testSubmitFiveRequestsAllCallbacksFire() {
    auto hal = std::make_unique<autodiag::MockDiagnosticHal>();
    autodiag::DiagEngine engine(std::move(hal));
    engine.start();

    std::mutex mu;
    std::condition_variable cv;
    std::size_t count = 0;

    for (std::uint32_t i = 0; i < 5; ++i) {
        autodiag::DiagRequest req{};
        req.requestId = 100 + i;
        req.service = autodiag::UdsService::ReadDataByIdentifier;
        req.dataId = static_cast<std::uint16_t>(autodiag::DiagProperty::VIN);

        const bool accepted = engine.submit(req, [&](const autodiag::DiagResponse& resp) {
            std::lock_guard<std::mutex> lk(mu);
            if (resp.positive) {
                ++count;
            }
            cv.notify_one();
        });
        expectTrue(accepted, "engine_submit_five_request_accepted");
    }

    std::unique_lock<std::mutex> lk(mu);
    const bool completed = cv.wait_for(lk, std::chrono::milliseconds(500), [&] { return count == 5; });
    expectTrue(completed, "engine_submit_five_all_callbacks_fired");
    expectEqSize(count, 5, "engine_submit_five_callback_count");

    engine.shutdown();
}

void testShutdownStopsWorkerAndRejectsNewSubmit() {
    auto hal = std::make_unique<autodiag::MockDiagnosticHal>();
    autodiag::DiagEngine engine(std::move(hal));
    engine.start();
    engine.shutdown();

    autodiag::DiagRequest req{};
    req.requestId = 777;
    req.service = autodiag::UdsService::ReadDataByIdentifier;
    req.dataId = static_cast<std::uint16_t>(autodiag::DiagProperty::VIN);

    const bool accepted = engine.submit(req, [](const autodiag::DiagResponse&) {});
    expectTrue(!accepted, "engine_submit_after_shutdown_rejected");
    expectEqSize(engine.pendingCount(), 0, "engine_pending_after_shutdown_is_zero");
}

void testHalNotReadyReturnsNegativeResponse() {
    auto hal = std::make_unique<NotReadyHal>();
    autodiag::DiagEngine engine(std::move(hal));
    engine.start();

    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    autodiag::DiagResponse out{};

    autodiag::DiagRequest req{};
    req.requestId = 313;
    req.service = autodiag::UdsService::TesterPresent;
    req.subFunction = 0x00;

    const bool accepted = engine.submit(req, [&](const autodiag::DiagResponse& resp) {
        std::lock_guard<std::mutex> lk(mu);
        out = resp;
        done = true;
        cv.notify_one();
    });
    expectTrue(accepted, "engine_not_ready_submit_accepted");

    std::unique_lock<std::mutex> lk(mu);
    const bool completed = cv.wait_for(lk, std::chrono::milliseconds(300), [&] { return done; });
    expectTrue(completed, "engine_not_ready_callback_fired");
    if (completed) {
        expectTrue(!out.positive, "engine_not_ready_negative_response");
        expectTrue(!out.valueString.empty(), "engine_not_ready_error_message");
    }

    engine.shutdown();
}

}  // namespace

int main() {
    testSubmitSingleRequestCallbackFires();
    testSubmitFiveRequestsAllCallbacksFire();
    testShutdownStopsWorkerAndRejectsNewSubmit();
    testHalNotReadyReturnsNegativeResponse();

    if (g_failures == 0) {
        std::cout << "All diag engine tests passed. tests=" << g_tests << "\n";
        return 0;
    }

    std::cerr << "Diag engine tests failed. failures=" << g_failures << " tests=" << g_tests << "\n";
    return 1;
}
