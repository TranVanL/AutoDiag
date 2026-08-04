#pragma once

#include "idiag_hal.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace autodiag { 
class DoipDiagnosticHal : public IDiagnosticHal {
public: 
    DoipDiagnosticHal(std::string host, uint16_t port);
    ~DoipDiagnosticHal() override;
    IDiagnosticHal::Result SendAndReceive(const std::vector<uint8_t> &req) override;
    IDiagnosticHal::Result readProperty(uint32_t propId, uint32_t areaId) override;
    bool isReady() const override;
    void reset() override;

private:
    bool connect();
    void disconnect();

    bool sendAll(const std::vector<uint8_t> &data , std::size_t size);
    bool receiveAll(std::vector<uint8_t> &data, std::size_t size);

    static void appendU16(std::vector<std::uint8_t>& out, std::uint16_t v);
    static void appendU32(std::vector<std::uint8_t>& out, std::uint32_t v);
    static std::uint16_t readU16(const std::uint8_t* p);
    static std::uint32_t readU32(const std::uint8_t* p);

    std::string host_;
    uint16_t port_;
    int sockfd_{-1};
    bool isReady_{false};
    mutable std::mutex socket_mutex_;
    
    std::uint16_t EcuAddress{0x1234};
    std::uint16_t TesterAddress{0x0E00};
    

};

} // autodiag