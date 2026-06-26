#include "diag_type.h"
#include "uds_codec.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using autodiag::DiagProperty;
using autodiag::DiagRequest;
using autodiag::DiagResponse;
using autodiag::Nrc;
using autodiag::UdsService;

int g_failures = 0;
int g_checks = 0;

#define CHECK_TRUE(cond, name) \
    do { \
        ++g_checks; \
        if (!(cond)) { \
            ++g_failures; \
            std::cerr << "[FAIL] " << (name) << "\n"; \
        } \
    } while (0)

#define CHECK_EQ(actual, expected, name) \
    do { \
        ++g_checks; \
        if (!((actual) == (expected))) { \
            ++g_failures; \
            std::cerr << "[FAIL] " << (name) << " actual=" << (actual) << " expected=" << (expected) << "\n"; \
        } \
    } while (0)

bool vecEq(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    return a == b;
}

void runEncodeTests() {
    // 1) ReadDID VIN encode
    {
        DiagRequest req{};
        req.service = UdsService::ReadDataByIdentifier;
        req.dataId = static_cast<std::uint16_t>(DiagProperty::VIN);
        const auto out = autodiag::encode(req);
        CHECK_TRUE(vecEq(out, {0x22, 0xF1, 0x90}), "encode_read_did_vin");
    }

    // 2) ClearDTC encode
    {
        DiagRequest req{};
        req.service = UdsService::ClearDTC;
        const auto out = autodiag::encode(req);
        CHECK_TRUE(vecEq(out, {0x14, 0xFF, 0xFF, 0xFF}), "encode_clear_dtc");
    }

    // 3) TesterPresent encode
    {
        DiagRequest req{};
        req.service = UdsService::TesterPresent;
        req.subFunction = 0x00;
        const auto out = autodiag::encode(req);
        CHECK_TRUE(vecEq(out, {0x3E, 0x00}), "encode_tester_present");
    }

    // 4) payload append
    {
        DiagRequest req{};
        req.service = UdsService::ReadDTC;
        req.payload = {0x01, 0x02, 0x03};
        const auto out = autodiag::encode(req);
        CHECK_TRUE(vecEq(out, {0x19, 0x01, 0x02, 0x03}), "encode_payload_append");
    }

    // 5) preserve request id / no crash path
    {
        DiagRequest req{};
        req.requestId = 99;
        req.service = UdsService::ReadDTC;
        const auto out = autodiag::encode(req);
        CHECK_TRUE(!out.empty(), "encode_non_empty");
    }
}

void runDecodePositiveTests() {
    // 6) VIN positive response
    {
        const std::vector<std::uint8_t> in = {
            0x62, 0xF1, 0x90,
            'V', 'I', 'N', 'F', 'A', 'S', 'T', '1', '2', '3'
        };
        const DiagResponse r = autodiag::decode(1, in);
        CHECK_TRUE(r.positive, "decode_vin_positive_flag");
        CHECK_EQ(r.valueString, std::string("VINFAST123"), "decode_vin_value");
    }

    // 7) RPM positive response (0x0C80 = 3200 raw, RPM=800)
    {
        const std::vector<std::uint8_t> in = {0x62, 0x01, 0x0C, 0x0C, 0x80};
        const DiagResponse r = autodiag::decode(2, in);
        CHECK_TRUE(r.positive, "decode_rpm_positive_flag");
        CHECK_EQ(r.valueString, std::string("800"), "decode_rpm_value");
    }

    // 8) SOC positive response
    {
        const std::vector<std::uint8_t> in = {0x62, 0x01, 0x05, 0x4E};
        const DiagResponse r = autodiag::decode(3, in);
        CHECK_EQ(r.valueString, std::string("78"), "decode_soc_value");
    }

    // 9) empty payload on positive sid (non 0x62)
    {
        const std::vector<std::uint8_t> in = {0x50};
        const DiagResponse r = autodiag::decode(4, in);
        CHECK_TRUE(r.positive, "decode_generic_positive_sid");
    }

    // 10) truncated 0x62 response
    {
        const std::vector<std::uint8_t> in = {0x62, 0xF1};
        const DiagResponse r = autodiag::decode(5, in);
        CHECK_TRUE(!r.positive, "decode_truncated_62_negative");
        CHECK_EQ(static_cast<int>(r.nrc), static_cast<int>(Nrc::RequestOutOfRange), "decode_truncated_62_nrc");
    }
}

void runDecodeNegativeTests() {
    // 11) negative SecurityAccessDenied
    {
        const std::vector<std::uint8_t> in = {0x7F, 0x22, 0x13};
        const DiagResponse r = autodiag::decode(6, in);
        CHECK_TRUE(!r.positive, "decode_neg_13_flag");
        CHECK_EQ(static_cast<int>(r.nrc), static_cast<int>(Nrc::SecurityAccessDenied), "decode_neg_13_nrc");
    }

    // 12) negative RequestOutOfRange
    {
        const std::vector<std::uint8_t> in = {0x7F, 0x22, 0x31};
        const DiagResponse r = autodiag::decode(7, in);
        CHECK_EQ(static_cast<int>(r.nrc), static_cast<int>(Nrc::RequestOutOfRange), "decode_neg_31_nrc");
    }

    // 13) all defined NRC parse
    {
        const std::vector<Nrc> nrcs = {
            Nrc::ServiceNotSupported,
            Nrc::SubFunctionNotSupported,
            Nrc::SecurityAccessDenied,
            Nrc::AuthenticationRequired,
            Nrc::RequestOutOfRange,
            Nrc::SecurityAccessRequestSequenceError
        };
        for (const auto n : nrcs) {
            const std::vector<std::uint8_t> in = {0x7F, 0x22, static_cast<std::uint8_t>(n)};
            const DiagResponse r = autodiag::decode(8, in);
            CHECK_EQ(static_cast<int>(r.nrc), static_cast<int>(n), "decode_all_nrc_codes");
        }
    }

    // 14) short negative frame
    {
        const std::vector<std::uint8_t> in = {0x7F};
        const DiagResponse r = autodiag::decode(9, in);
        CHECK_EQ(static_cast<int>(r.nrc), static_cast<int>(Nrc::RequestOutOfRange), "decode_short_7f");
    }

    // 15) invalid positive sid (<0x40)
    {
        const std::vector<std::uint8_t> in = {0x22, 0xF1, 0x90};
        const DiagResponse r = autodiag::decode(10, in);
        CHECK_TRUE(!r.positive, "decode_invalid_sid_flag");
        CHECK_EQ(static_cast<int>(r.nrc), static_cast<int>(Nrc::ServiceNotSupported), "decode_invalid_sid_nrc");
    }
}

void runEdgeTests() {
    // 16) roundtrip service + did
    {
        DiagRequest req{};
        req.service = UdsService::ReadDataByIdentifier;
        req.dataId = static_cast<std::uint16_t>(DiagProperty::SoftwareVer);
        const auto encoded = autodiag::encode(req);
        CHECK_TRUE(vecEq(encoded, {0x22, 0xF1, 0x87}), "edge_roundtrip_encode");
    }

    // 17) big-endian consistency
    {
        const std::vector<std::uint8_t> in = {0x62, 0x12, 0x34, 0xAA};
        const DiagResponse r = autodiag::decode(11, in);
        CHECK_TRUE(r.positive, "edge_big_endian_parse");
    }

    // 18) UTF-8 payload decode
    {
        const std::vector<std::uint8_t> in = {0x62, 0xF1, 0x90, 0xE2, 0x9C, 0x93};
        const DiagResponse r = autodiag::decode(12, in);
        CHECK_TRUE(!r.valueString.empty(), "edge_utf8_payload");
    }

    // 19) max payload append
    {
        DiagRequest req{};
        req.service = UdsService::ReadDTC;
        req.payload.resize(255, 0xAB);
        const auto out = autodiag::encode(req);
        CHECK_EQ(static_cast<int>(out.size()), 256, "edge_max_payload_size");
    }

    // 20) toString helpers
    {
        CHECK_TRUE(!autodiag::udsServiceToString(UdsService::ReadDataByIdentifier).empty(), "edge_service_to_string");
        CHECK_TRUE(!autodiag::nrcToString(Nrc::RequestOutOfRange).empty(), "edge_nrc_to_string");
    }
}

}  // namespace

int main() {
    runEncodeTests();
    runDecodePositiveTests();
    runDecodeNegativeTests();
    runEdgeTests();

    if (g_failures == 0) {
        std::cout << "All strategy checks passed. checks=" << g_checks << "\n";
        return 0;
    }

    std::cerr << "Strategy checks failed. failures=" << g_failures << " checks=" << g_checks << "\n";
    return 1;
}
