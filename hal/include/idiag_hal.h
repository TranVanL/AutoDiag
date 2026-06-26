#pragma once

#include <cstdint>
#include <string>
#include <vector>


namespace autodiag {

class IDiagnosticHal {
    struct Result {
        bool success {false};
        std::vector<uint8_t> data{};
        std::string error{};
    };
    virtual Result SendAndReceive(const std::vector<uint8_t> &req) = 0;
    virtual bool isReady() = 0;
    virtual void reset() = 0;
    virtual ~IDiagnosticHal() = default;
};

}