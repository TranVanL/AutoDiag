#include "session_state.h"

#include <gtest/gtest.h>

namespace autodiag {
namespace {

class SessionStateTest : public ::testing::Test {};

TEST_F(SessionStateTest, IdleToPendingToDone) {
    SessionStateMachine session;

    EXPECT_TRUE(session.transition(State::Pending));
    EXPECT_EQ(session.current(), State::Pending);
    EXPECT_TRUE(session.transition(State::Done));
    EXPECT_EQ(session.current(), State::Done);
}

TEST_F(SessionStateTest, IdleToPendingToError) {
    SessionStateMachine session;

    EXPECT_TRUE(session.transition(State::Pending));
    EXPECT_TRUE(session.transition(State::Error));
    EXPECT_EQ(session.current(), State::Error);
}

TEST_F(SessionStateTest, IdleToDoneRejected) {
    SessionStateMachine session;

    EXPECT_FALSE(session.transition(State::Done));
    EXPECT_EQ(session.current(), State::Idle);
}

TEST_F(SessionStateTest, DoneToPendingRejected) {
    SessionStateMachine session;
    session.transition(State::Pending);
    session.transition(State::Done);

    EXPECT_FALSE(session.transition(State::Pending));
    EXPECT_EQ(session.current(), State::Done);
}

TEST_F(SessionStateTest, ResetReturnsToIdle) {
    SessionStateMachine session;
    session.transition(State::Pending);
    session.transition(State::Error);
    session.reset();

    EXPECT_EQ(session.current(), State::Idle);
    EXPECT_TRUE(session.transition(State::Pending));
}

}  // namespace
}  // namespace autodiag
