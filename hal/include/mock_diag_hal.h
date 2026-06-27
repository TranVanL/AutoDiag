#pragma once

#include "idiag_hal.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace autodiag {

class MockDiagnosticHal : public IDiagnosticHal {
public: 
    MockDiagnosticHal();
    IDiagnosticHal::Result SendAndReceive(const std::vector<uint8_t> &req) override ;
    bool isReady() const override;
    void reset() override;
private: 
    void initDIDDb();
    void initDTCData();

    bool is_ready{true};
    std::unordered_map<uint16_t, std::vector<uint8_t>> DID_db{};
    std::vector<uint8_t> DTC_payload{};

};

} // namespace autodiag 