#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace autodiag {

enum class UdsService : std::uint8_t {
    DiagnosticSessionControl = 0x10,
    ECUReset                 = 0x11,
    ClearDTC                 = 0x14,
    ReadDTC                  = 0x19,
    ReadDataByIdentifier     = 0x22,
    SecurityAccess           = 0x27,
    CommunicationControl     = 0x28,
    TesterPresent            = 0x3E,
    ControlDTCSetting        = 0x85,
};

enum class DiagProperty : std::uint16_t {
    VIN         = 0xF190,
    SoftwareVer = 0xF187,
    SOC         = 0x0105,
    RPM         = 0x010C,
    DtcList     = 0xF191,
    DtcClear    = 0xF193,
    Timestamp   = 0xF192,
};

enum class Nrc : std::uint8_t {
    PositiveResponse                    = 0x00,
    ServiceNotSupported                 = 0x11,
    SubFunctionNotSupported             = 0x12,
    SecurityAccessDenied                = 0x13,
    AuthenticationRequired              = 0x14,
    RequestOutOfRange                   = 0x31,
    SecurityAccessRequestSequenceError  = 0x33,
};

struct DiagRequest {
    std::uint32_t requestId {0};
    UdsService service {UdsService::ReadDataByIdentifier};
    std::uint16_t dataId {0};
    std::vector<std::uint8_t> payload {};
    std::uint8_t subFunction {0};
};

struct DiagResponse {
    std::uint32_t requestId {0};
    bool positive {false};
    Nrc nrc {Nrc::PositiveResponse};
    std::vector<std::uint8_t> data {};
    std::int64_t latencyUs {0};
    std::string valueString {};
};

inline std::string udsServiceToString(UdsService service) {
    switch (service) {
        case UdsService::DiagnosticSessionControl: return "DiagnosticSessionControl";
        case UdsService::ECUReset: return "ECUReset";
        case UdsService::ClearDTC: return "ClearDTC";
        case UdsService::ReadDTC: return "ReadDTC";
        case UdsService::ReadDataByIdentifier: return "ReadDataByIdentifier";
        case UdsService::SecurityAccess: return "SecurityAccess";
        case UdsService::CommunicationControl: return "CommunicationControl";
        case UdsService::TesterPresent: return "TesterPresent";
        case UdsService::ControlDTCSetting: return "ControlDTCSetting";
        default: return "UnknownService";
    }
}

inline std::string nrcToString(Nrc nrc) {
    switch (nrc) {
        case Nrc::PositiveResponse: return "PositiveResponse";
        case Nrc::ServiceNotSupported: return "ServiceNotSupported";
        case Nrc::SubFunctionNotSupported: return "SubFunctionNotSupported";
        case Nrc::SecurityAccessDenied: return "SecurityAccessDenied";
        case Nrc::AuthenticationRequired: return "AuthenticationRequired";
        case Nrc::RequestOutOfRange: return "RequestOutOfRange";
        case Nrc::SecurityAccessRequestSequenceError: return "SecurityAccessRequestSequenceError";
        default: return "UnknownNrc";
    }
}

}  // namespace autodiag
