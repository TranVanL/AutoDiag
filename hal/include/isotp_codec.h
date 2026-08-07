#pragma once

#include "socketcan_types.h"

#include <cstdint>
#include <vector>

namespace autodiag {
namespace can {


class IsotpCodec {
public:
    enum class State {
        Idle,
        ReceivingMultiFrame,
        Complete,
        Error
    };

    IsotpCodec() = default;

    // Segment a UDS payload into ISO-TP CAN frames.
    std::vector<CanFrame> segment(const std::vector<uint8_t>& payload, uint32_t txId);

    // Feed one received CAN frame into the reassembly state machine.
    bool feedFrame(const CanFrame& frame);

    // Generate a Flow Control frame (CTS by default).
    CanFrame makeFlowControl(uint32_t rxId, uint8_t blockSize = 0, uint8_t stMin = 0);

    State getState() const { return state_; }
    bool isComplete() const { return state_ == State::Complete; }
    bool hasError() const { return state_ == State::Error; }

    std::vector<uint8_t> getPayload() const;
    void reset();

private:
    State state_ = State::Idle;
    std::vector<uint8_t> payload_;
    uint16_t expectedLength_ = 0;
    uint8_t nextSequence_ = 1;
    uint8_t blockSize_ = 0;
    uint8_t receivedSinceFc_ = 0;

    bool handleSingleFrame(const CanFrame& frame);
    bool handleFirstFrame(const CanFrame& frame);
    bool handleConsecutiveFrame(const CanFrame& frame);
    bool handleFlowControl(const CanFrame& frame);
};

} // namespace can
} // namespace autodiag
