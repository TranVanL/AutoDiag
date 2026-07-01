#include "uds_codec.h"

namespace autodiag {

std::vector<std::uint8_t> encode(const DiagRequest& req) {
    std::vector<std::uint8_t> out;

    const auto service = static_cast<std::uint8_t>(req.service);
    out.push_back(service);

    switch (req.service) {
        case UdsService::DiagnosticSessionControl:
        case UdsService::ReadDTC:
        case UdsService::SecurityAccess:
        case UdsService::TesterPresent:
            out.push_back(req.subFunction);
            break;
        case UdsService::ReadDataByIdentifier:
            out.push_back(static_cast<std::uint8_t>((req.dataId >> 8U) & 0xFFU));
            out.push_back(static_cast<std::uint8_t>(req.dataId & 0xFFU));
            break;
        case UdsService::ClearDTC:
            out.push_back(0xFF);
            out.push_back(0xFF);
            out.push_back(0xFF);
            break;
        default:
            break;
    }

    out.insert(out.end(), req.payload.begin(), req.payload.end());
    return out;
}

DiagResponse decode(std::uint32_t reqId, const std::vector<std::uint8_t>& bytes) {
    DiagResponse resp {};
    resp.requestId = reqId;

    if (bytes.empty()) {
        resp.positive = false;
        resp.nrc = Nrc::RequestOutOfRange;
        return resp;
    }

    const auto sid = bytes[0];
    if (sid == 0x7F) {
        resp.positive = false;
        resp.nrc = (bytes.size() >= 3)
            ? static_cast<Nrc>(bytes[2])
            : Nrc::RequestOutOfRange;
        return resp;
    }

    // Positive UDS responses are service + 0x40.
    if (sid < 0x40) {
        resp.positive = false;
        resp.nrc = Nrc::ServiceNotSupported;
        return resp;
    }

    resp.positive = true;

    if (sid == 0x62) {

        if (bytes.size() < 3) {
            resp.positive = false;
            resp.nrc = Nrc::RequestOutOfRange;
            return resp;
        }

        const std::uint16_t did =
            static_cast<std::uint16_t>(bytes[1] << 8U) |
            static_cast<std::uint16_t>(bytes[2]);

        resp.data.assign(bytes.begin() + 3, bytes.end());

        if (did == static_cast<std::uint16_t>(DiagProperty::VIN) || did == static_cast<std::uint16_t>(DiagProperty::SoftwareVer) ) {
            resp.valueString.assign(resp.data.begin(), resp.data.end());
        } else if (did == static_cast<std::uint16_t>(DiagProperty::RPM) && resp.data.size() >= 2) {
            const auto raw = static_cast<std::uint16_t>(resp.data[0] << 8U) |
                             static_cast<std::uint16_t>(resp.data[1]);
            resp.valueString = std::to_string(raw / 4U);
        } else if (did == static_cast<std::uint16_t>(DiagProperty::SOC) && !resp.data.empty()) {
            resp.valueString = std::to_string(resp.data[0]);
        }
    }
    else if (sid == 0x59) {

        if (bytes.size() < 2) {
            resp.positive = false;
            resp.nrc = Nrc::RequestOutOfRange;
            return resp;
        }

        resp.data.assign(bytes.begin() + 2, bytes.end());

        resp.valueString.assign(resp.data.begin(), resp.data.end());
    }
    else if (sid == 0x54) {
        resp.data.assign(bytes.begin() + 1, bytes.end());
        resp.valueString = "OK";
    }
    else {
        resp.data.assign(bytes.begin() + 1, bytes.end());
    }

    return resp;
}

}  // namespace autodiag