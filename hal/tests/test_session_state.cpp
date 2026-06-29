#include "session_state.h"

#include <iostream>

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

void expectStateEq(autodiag::State actual, autodiag::State expected, const char* testName) {
    ++g_tests;
    if (actual != expected) {
        ++g_failures;
        std::cerr << "[FAIL] " << testName
                  << " expected=" << static_cast<int>(expected)
                  << " actual=" << static_cast<int>(actual) << "\n";
    }
}

void testIdleToPendingToDone() {
    autodiag::SessionStateMachine session;

    expectTrue(session.transition(autodiag::State::Pending), "session_idle_to_pending_valid");
    expectStateEq(session.current(), autodiag::State::Pending, "session_current_pending");
    expectTrue(session.transition(autodiag::State::Done), "session_pending_to_done_valid");
    expectStateEq(session.current(), autodiag::State::Done, "session_current_done");
}

void testIdleToPendingToError() {
    autodiag::SessionStateMachine session;

    expectTrue(session.transition(autodiag::State::Pending), "session_pending_for_error_path");
    expectTrue(session.transition(autodiag::State::Error), "session_pending_to_error_valid");
    expectStateEq(session.current(), autodiag::State::Error, "session_current_error");
}

void testIdleToDoneRejected() {
    autodiag::SessionStateMachine session;

    expectTrue(!session.transition(autodiag::State::Done), "session_idle_to_done_rejected");
    expectStateEq(session.current(), autodiag::State::Idle, "session_stays_idle_after_done_reject");
}

void testDoneToPendingRejected() {
    autodiag::SessionStateMachine session;
    session.transition(autodiag::State::Pending);
    session.transition(autodiag::State::Done);

    expectTrue(!session.transition(autodiag::State::Pending), "session_done_to_pending_rejected");
    expectStateEq(session.current(), autodiag::State::Done, "session_stays_done_after_reject");
}

void testResetReturnsToIdle() {
    autodiag::SessionStateMachine session;
    session.transition(autodiag::State::Pending);
    session.transition(autodiag::State::Error);
    session.reset();

    expectStateEq(session.current(), autodiag::State::Idle, "session_reset_to_idle");
    expectTrue(session.transition(autodiag::State::Pending), "session_pending_after_reset_valid");
}

}  // namespace

int main() {
    testIdleToPendingToDone();
    testIdleToPendingToError();
    testIdleToDoneRejected();
    testDoneToPendingRejected();
    testResetReturnsToIdle();

    if (g_failures == 0) {
        std::cout << "All session state tests passed. tests=" << g_tests << "\n";
        return 0;
    }

    std::cerr << "Session state tests failed. failures=" << g_failures << " tests=" << g_tests << "\n";
    return 1;
}
