#include "diag_engine.h"

#include "diag_type.h"
#include "idiag_hal.h"
#include "mock_diag_hal.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
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

    auto outPromise = std::make_shared<std::promise<autodiag::DiagResponse>>();
    auto outFuture = outPromise->get_future();

    autodiag::DiagRequest req{};
    req.requestId = 1;
    req.service = autodiag::UdsService::ReadDataByIdentifier;
    req.dataId = static_cast<std::uint16_t>(autodiag::DiagProperty::VIN);

    const bool accepted = engine.submit(req, [outPromise](const autodiag::DiagResponse& resp) {
        outPromise->set_value(resp);
    });

    expectTrue(accepted, "engine_submit_single_accepted");

    const bool completed = outFuture.wait_for(std::chrono::milliseconds(1500)) == std::future_status::ready;
    expectTrue(completed, "engine_submit_single_callback_fired");
    if (completed) {
        const auto out = outFuture.get();
        expectTrue(out.positive, "engine_submit_single_positive");
        expectTrue(!out.valueString.empty(), "engine_submit_single_has_value");
    }

    engine.shutdown();
}

void testSubmitFiveRequestsAllCallbacksFire() {
    auto hal = std::make_unique<autodiag::MockDiagnosticHal>();
    autodiag::DiagEngine engine(std::move(hal));
    engine.start();

    auto donePromise = std::make_shared<std::promise<void>>();
    auto doneFuture = donePromise->get_future();
    auto count = std::make_shared<std::atomic<std::size_t>>(0);

    for (std::uint32_t i = 0; i < 5; ++i) {
        autodiag::DiagRequest req{};
        req.requestId = 100 + i;
        req.service = autodiag::UdsService::ReadDataByIdentifier;
        req.dataId = static_cast<std::uint16_t>(autodiag::DiagProperty::VIN);

        const bool accepted = engine.submit(req, [count, donePromise](const autodiag::DiagResponse& resp) {
            if (resp.positive) {
                const auto current = count->fetch_add(1) + 1;
                if (current == 5) {
                    donePromise->set_value();
                }
            }
        });
        expectTrue(accepted, "engine_submit_five_request_accepted");
    }

    const bool completed = doneFuture.wait_for(std::chrono::milliseconds(2500)) == std::future_status::ready;
    expectTrue(completed, "engine_submit_five_all_callbacks_fired");
    expectEqSize(count->load(), 5, "engine_submit_five_callback_count");

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

    auto outPromise = std::make_shared<std::promise<autodiag::DiagResponse>>();
    auto outFuture = outPromise->get_future();

    autodiag::DiagRequest req{};
    req.requestId = 313;
    req.service = autodiag::UdsService::TesterPresent;
    req.subFunction = 0x00;

    const bool accepted = engine.submit(req, [outPromise](const autodiag::DiagResponse& resp) {
        outPromise->set_value(resp);
    });
    expectTrue(accepted, "engine_not_ready_submit_accepted");

    const bool completed = outFuture.wait_for(std::chrono::milliseconds(1500)) == std::future_status::ready;
    expectTrue(completed, "engine_not_ready_callback_fired");
    if (completed) {
        const auto out = outFuture.get();
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
