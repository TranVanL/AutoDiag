#pragma once

#include <cstdint>

namespace autodiag {
namespace can {


struct CanFrame {
    uint32_t id;          // 11-bit standard or 29-bit extended CAN ID
    uint8_t  len;         // DLC 0-8
    uint8_t  data[8];

    bool operator==(const CanFrame& other) const {
        if (id != other.id || len != other.len) return false;
        for (uint8_t i = 0; i < len; ++i) {
            if (data[i] != other.data[i]) return false;
        }
        return true;
    }
};

constexpr uint8_t ISO_TP_SF = 0x00;  // Single Frame
constexpr uint8_t ISO_TP_FF = 0x10;  // First Frame
constexpr uint8_t ISO_TP_CF = 0x20;  // Consecutive Frame
constexpr uint8_t ISO_TP_FC = 0x30;  // Flow Control

inline uint8_t getIsoTpType(uint8_t firstByte) {
    return firstByte & 0xF0;
}

inline uint8_t getSingleFrameLength(uint8_t firstByte) {
    return firstByte & 0x0F;
}

inline uint16_t getFirstFrameLength(uint8_t byte0, uint8_t byte1) {
    return static_cast<uint16_t>(((byte0 & 0x0F) << 8) | byte1);
}

inline uint8_t getConsecutiveSequence(uint8_t firstByte) {
    return firstByte & 0x0F;
}

inline uint8_t getFlowStatus(uint8_t firstByte) {
    return firstByte & 0x0F;
}

} // namespace can
} // namespace autodiag
