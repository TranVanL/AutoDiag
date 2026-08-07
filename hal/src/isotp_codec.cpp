#include "isotp_codec.h"

#include <algorithm>
#include <cstring>

namespace autodiag {
namespace can {

namespace {

constexpr uint16_t kMaxIsoTpPayload = 4095;
constexpr uint8_t  kCanPayloadSize = 7;
constexpr uint8_t  kFirstFramePayloadSize = 6;

} // namespace

std::vector<CanFrame> IsotpCodec::segment(const std::vector<uint8_t>& payload, uint32_t txId) {
    std::vector<CanFrame> frames;

    if (payload.empty() || payload.size() > kMaxIsoTpPayload) {
        return frames; // invalid
    }

    if (payload.size() <= kCanPayloadSize) {
        CanFrame f{};
        f.id = txId;
        f.len = 8;
        f.data[0] = static_cast<uint8_t>(ISO_TP_SF | payload.size());
        std::memcpy(&f.data[1], payload.data(), payload.size());
        frames.push_back(f);
        return frames;
    }

    // Multi-frame: First Frame
    {
        CanFrame f{};
        f.id = txId;
        f.len = 8;
        f.data[0] = static_cast<uint8_t>(ISO_TP_FF | ((payload.size() >> 8) & 0x0F));
        f.data[1] = static_cast<uint8_t>(payload.size() & 0xFF);
        std::memcpy(&f.data[2], payload.data(), kFirstFramePayloadSize);
        frames.push_back(f);
    }

    // Consecutive Frames
    size_t offset = kFirstFramePayloadSize;
    uint8_t sequence = 1;
    while (offset < payload.size()) {
        size_t remaining = payload.size() - offset;
        size_t chunk = std::min<size_t>(remaining, kCanPayloadSize);

        CanFrame f{};
        f.id = txId;
        f.len = 8;
        f.data[0] = static_cast<uint8_t>(ISO_TP_CF | sequence);
        std::memcpy(&f.data[1], payload.data() + offset, chunk);
        frames.push_back(f);

        offset += chunk;
        sequence = (sequence + 1) & 0x0F;
    }

    return frames;
}

bool IsotpCodec::feedFrame(const CanFrame& frame) {
    if (frame.len == 0) return false;

    uint8_t type = getIsoTpType(frame.data[0]);

    switch (type) {
        case ISO_TP_SF: return handleSingleFrame(frame);
        case ISO_TP_FF: return handleFirstFrame(frame);
        case ISO_TP_CF: return handleConsecutiveFrame(frame);
        case ISO_TP_FC: return handleFlowControl(frame);
        default:
            state_ = State::Error;
            return false;
    }
}

CanFrame IsotpCodec::makeFlowControl(uint32_t rxId, uint8_t blockSize, uint8_t stMin) {
    CanFrame f{};
    f.id = rxId;
    f.len = 8;
    f.data[0] = static_cast<uint8_t>(ISO_TP_FC | 0x00); // CTS
    f.data[1] = blockSize;
    f.data[2] = stMin;
    return f;
}

std::vector<uint8_t> IsotpCodec::getPayload() const {
    if (state_ != State::Complete) return {};
    return payload_;
}

void IsotpCodec::reset() {
    state_ = State::Idle;
    payload_.clear();
    expectedLength_ = 0;
    nextSequence_ = 1;
    blockSize_ = 0;
    receivedSinceFc_ = 0;
}

bool IsotpCodec::handleSingleFrame(const CanFrame& frame) {
    if (state_ != State::Idle) {
        reset();
    }

    uint8_t length = getSingleFrameLength(frame.data[0]);
    if (length == 0 || length > kCanPayloadSize || length + 1 > frame.len) {
        state_ = State::Error;
        return false;
    }

    payload_.assign(frame.data + 1, frame.data + 1 + length);
    expectedLength_ = length;
    state_ = State::Complete;
    return true;
}

bool IsotpCodec::handleFirstFrame(const CanFrame& frame) {
    reset();

    if (frame.len < 8) {
        state_ = State::Error;
        return false;
    }

    uint16_t length = getFirstFrameLength(frame.data[0], frame.data[1]);
    if (length == 0 || length > kMaxIsoTpPayload) {
        state_ = State::Error;
        return false;
    }

    expectedLength_ = length;
    payload_.reserve(expectedLength_);
    payload_.assign(frame.data + 2, frame.data + 8);
    nextSequence_ = 1;
    receivedSinceFc_ = 0;
    state_ = State::ReceivingMultiFrame;
    return true;
}

bool IsotpCodec::handleConsecutiveFrame(const CanFrame& frame) {
    if (state_ != State::ReceivingMultiFrame) {
        state_ = State::Error;
        return false;
    }

    uint8_t sequence = getConsecutiveSequence(frame.data[0]);
    if (sequence != nextSequence_) {
        state_ = State::Error;
        return false;
    }

    size_t remaining = expectedLength_ - payload_.size();
    if (remaining == 0) {
        state_ = State::Error;
        return false;
    }

    if (frame.len < 2) {
        state_ = State::Error;
        return false;
    }

    size_t chunk = std::min<size_t>(remaining, kCanPayloadSize);
    size_t available = std::min<size_t>(chunk, frame.len - 1);
    if (available == 0) {
        state_ = State::Error;
        return false;
    }

    payload_.insert(payload_.end(), frame.data + 1, frame.data + 1 + available);
    nextSequence_ = (nextSequence_ + 1) & 0x0F;
    ++receivedSinceFc_;

    if (payload_.size() == expectedLength_) {
        state_ = State::Complete;
    }

    return true;
}

bool IsotpCodec::handleFlowControl(const CanFrame& frame) {
    if (frame.len < 3) {
        state_ = State::Error;
        return false;
    }

    uint8_t fs = getFlowStatus(frame.data[0]);
    if (fs > 2) {
        state_ = State::Error;
        return false;
    }

    blockSize_ = frame.data[1];
    receivedSinceFc_ = 0;
    return true;
}

} // namespace can
} // namespace autodiag
