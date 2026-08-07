#pragma once

#include "idiag_hal.h"
#include "isotp_codec.h"

#include <string>

namespace autodiag {
namespace can {

#include "socketcan_types.h"
class CanDiagnosticHal : public IDiagnosticHal {
public:
    // txId: CAN ID used for diagnostic requests (default OBD-II functional 0x7DF)
    // rxId: CAN ID expected for ECU responses (default 0x7E8)
    explicit CanDiagnosticHal(std::string ifname = "vcan0",
                              uint32_t txId = 0x7DF,
                              uint32_t rxId = 0x7E8);

    ~CanDiagnosticHal() override;

    IDiagnosticHal::Result SendAndReceive(const std::vector<uint8_t>& req) override;
    IDiagnosticHal::Result readProperty(uint32_t propId, uint32_t areaId = 0) override;
    bool isReady() const override;
    void reset() override;

    // Explicit SocketCAN lifecycle (separate from constructor for testability).
    bool open();
    void close();

    // Exposed for tests.
    int sockFd() const { return sockFd_; }

private:
    std::string ifname_;
    uint32_t txId_;
    uint32_t rxId_;
    int sockFd_ = -1;
    bool isReady_ = false;
    IsotpCodec codec_;

    bool sendFrame(const CanFrame& frame);
    bool receiveFrame(CanFrame& frame, int timeoutMs);
};

} // namespace can
} // namespace autodiag
