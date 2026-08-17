#pragma once

#include <cstdint>
#include <string>
#include <vector>


namespace autodiag {

class IDiagnosticHal {
public:
    struct Result {
        bool success {false};
        std::vector<uint8_t> data{};
        std::string error{};
    };

    virtual Result SendAndReceive(const std::vector<uint8_t> &req) = 0;
    virtual Result readProperty(uint32_t propId, uint32_t areaId = 0) = 0;
    virtual bool isReady() const = 0;
    virtual void reset() = 0;
    virtual ~IDiagnosticHal() = default;
    virtual void flashFirmware(const uint8_t* data, size_t len) = 0;
    
};

enum class HalType {
    Mock,
    Doip
};

} // namespace autodiag