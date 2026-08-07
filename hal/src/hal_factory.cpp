#include "hal_factory.h"
#include "mock_diag_hal.h"
#include "doip_diag_hal.h"
#include "can_diag_hal.h"
#include <stdexcept>

namespace autodiag {

std::unique_ptr<IDiagnosticHal> HalFactory::createHal(const std::string& spec) {
    if (spec == "mock") {
        return std::make_unique<autodiag::MockDiagnosticHal>();
    } else if (spec.rfind("doip:", 0) == 0) {
        // Parse host and port from spec, expected format: "doip:host:port"
        std::string rest = spec.substr(5);
        auto pos = rest.find(':');
        if (pos == std::string::npos) {
            throw std::invalid_argument("Invalid DoIP spec, expected doip:host:port");
        }
        std::string host = rest.substr(0, pos);
        uint16_t port = static_cast<uint16_t>(std::stoi(rest.substr(pos + 1)));
        return std::make_unique<autodiag::DoipDiagnosticHal>(host, port);
    } 
#ifndef __ANDROID__
    else if (spec.rfind("can:", 0) == 0) {
        std::string iface = spec.substr(4);
        if (iface.empty()) iface = "vcan0";
        return std::make_unique<autodiag::can::CanDiagnosticHal>(iface);
    }
#endif     
    else {
        throw std::invalid_argument("Unknown HAL type: " + spec);
    }
}

} // namespace autodiag
