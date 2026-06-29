#pragma once

#include <cstdint>

namespace autodiag {

enum class State : std::uint8_t {
    Idle = 0,
    Pending = 1,
    Done = 2,
    Error = 3,
};

class SessionStateMachine {
public:
    SessionStateMachine() = default;

    bool transition(State next);
    State current() const;
    void reset();
    bool isDone() const { return current_ == State::Done || current_ == State::Error;}

private:
    bool isValidTransition(State next) const;

    State current_ {State::Idle};
};

}  // namespace autodiag
