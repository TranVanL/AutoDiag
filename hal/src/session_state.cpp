#include "session_state.h"

namespace autodiag {

bool SessionStateMachine::transition(State next) {
    if (!isValidTransition(next)) {
        return false;
    }

    current_ = next;
    return true;
}

State SessionStateMachine::current() const {
    return current_;
}

void SessionStateMachine::reset() {
    current_ = State::Idle;
}

bool SessionStateMachine::isValidTransition(State next) const {
    switch (current_) {
        case State::Idle:
            return next == State::Pending;
        case State::Pending:
            return next == State::Done || next == State::Error;
        case State::Done:
        case State::Error:
            return false;
        default:
            return false;
    }
}

}  // namespace autodiag
