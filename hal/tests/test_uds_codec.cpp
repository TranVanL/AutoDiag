#include "diag_type.h"
#include "uds_codec.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using autodiag::DiagProperty;
using autodiag::DiagRequest;
using autodiag::DiagResponse;
using autodiag::Nrc;
using autodiag::UdsService;

int g_failures = 0;
int g_tests = 0;

void expectVecEq(const std::vector<std::uint8_t>& actual,
                 const std::vector<std::uint8_t>& expected,
                 const char* testName) {
    ++g_tests;
    if (actual != expected) {
        ++g_failures;
        std::cerr << "[FAIL] " << testName << " expected size=" << expected.size()
                  << " actual size=" << actual.size() << "\n";
    }
}

void testEncodeReadDidVin() {
    DiagRequest req{};
    req.service = UdsService::ReadDataByIdentifier;
    req.dataId = static_cast<std::uint16_t>(DiagProperty::VIN);
    expectVecEq(autodiag::encode(req), {0x22, 0xF1, 0x90}, "encode_read_did_vin");
}

void testEncodeClearDtc() {
    DiagRequest req{};
    req.service = UdsService::ClearDTC;
    expectVecEq(autodiag::encode(req), {0x14, 0xFF, 0xFF, 0xFF}, "encode_clear_dtc");
}

void testEncodeDiagnosticSessionControl() {
    DiagRequest req{};
    req.service = UdsService::DiagnosticSessionControl;
    req.subFunction = 0x03;
    expectVecEq(autodiag::encode(req), {0x10, 0x03}, "encode_diagnostic_session_control");
}

void testEncodeReadDtc() {
    DiagRequest req{};
    req.service = UdsService::ReadDTC;
    req.subFunction = 0x02;
    expectVecEq(autodiag::encode(req), {0x19, 0x02}, "encode_read_dtc");
}

void testEncodeSecurityAccessRequestSeed() {
    DiagRequest req{};
    req.service = UdsService::SecurityAccess;
    req.subFunction = 0x01;
    expectVecEq(autodiag::encode(req), {0x27, 0x01}, "encode_security_access_request_seed");
}

void testEncodeSecurityAccessSendKeyPayload() {
    DiagRequest req{};
    req.service = UdsService::SecurityAccess;
    req.subFunction = 0x02;
    req.payload = {0x12, 0x34, 0x56, 0x78};
    expectVecEq(autodiag::encode(req), {0x27, 0x02, 0x12, 0x34, 0x56, 0x78}, "encode_security_access_send_key");
}

void testEncodeTesterPresent() {
    DiagRequest req{};
    req.service = UdsService::TesterPresent;
    req.subFunction = 0x00;
    expectVecEq(autodiag::encode(req), {0x3E, 0x00}, "encode_tester_present");
}

void testEncodeReadDidWithPayloadAppended() {
    DiagRequest req{};
    req.service = UdsService::ReadDataByIdentifier;
    req.dataId = static_cast<std::uint16_t>(DiagProperty::SoftwareVer);
    req.payload = {0xAA, 0xBB};
    expectVecEq(autodiag::encode(req), {0x22, 0xF1, 0x87, 0xAA, 0xBB}, "encode_read_did_with_payload");
}

void expectBoolEq(bool actual, bool expected, const char* testName) {
    ++g_tests;
    if (actual != expected) {
        ++g_failures;
        std::cerr << "[FAIL] " << testName << " expected=" << expected
                  << " actual=" << actual << "\n";
    }
}

void expectNrcEq(autodiag::Nrc actual, autodiag::Nrc expected, const char* testName) {
    ++g_tests;
    if (actual != expected) {
        ++g_failures;
        std::cerr << "[FAIL] " << testName << " expected NRC="
                  << static_cast<int>(expected) << " actual="
                  << static_cast<int>(actual) << "\n";
    }
}

void expectStrEq(const std::string& actual, const std::string& expected, const char* testName) {
    ++g_tests;
    if (actual != expected) {
        ++g_failures;
        std::cerr << "[FAIL] " << testName << " expected='" << expected
                  << "' actual='" << actual << "'\n";
    }
}

void testDecodeVinPositive() {
    const std::vector<std::uint8_t> input = {
        0x62, 0xF1, 0x90,
        'V', 'I', 'N', 'F', 'A', 'S', 'T'
    };
    const DiagResponse response = autodiag::decode(101, input);
    expectBoolEq(response.positive, true, "decode_vin_positive");
    expectStrEq(response.valueString, "VINFAST", "decode_vin_value");
}

void testDecodeRpmPositive() {
    const std::vector<std::uint8_t> input = {0x62, 0x01, 0x0C, 0x0C, 0x80};
    const DiagResponse response = autodiag::decode(102, input);
    expectBoolEq(response.positive, true, "decode_rpm_positive");
    expectStrEq(response.valueString, "800", "decode_rpm_value");
}

void testDecodeSocPositive() {
    const std::vector<std::uint8_t> input = {0x62, 0x01, 0x05, 0x4E};
    const DiagResponse response = autodiag::decode(103, input);
    expectBoolEq(response.positive, true, "decode_soc_positive");
    expectStrEq(response.valueString, "78", "decode_soc_value");
}

void testDecodeSecurityAccessDenied() {
    const std::vector<std::uint8_t> input = {0x7F, 0x22, 0x13};
    const DiagResponse response = autodiag::decode(104, input);
    expectBoolEq(response.positive, false, "decode_nrc_13_positive_flag");
    expectNrcEq(response.nrc, Nrc::SecurityAccessDenied, "decode_nrc_13");
}

void testDecodeRequestOutOfRange() {
    const std::vector<std::uint8_t> input = {0x7F, 0x22, 0x31};
    const DiagResponse response = autodiag::decode(105, input);
    expectBoolEq(response.positive, false, "decode_nrc_31_positive_flag");
    expectNrcEq(response.nrc, Nrc::RequestOutOfRange, "decode_nrc_31");
}

void testDecodeTruncatedPositiveFrame() {
    const std::vector<std::uint8_t> input = {0x62, 0xF1};
    const DiagResponse response = autodiag::decode(106, input);
    expectBoolEq(response.positive, false, "decode_truncated_positive_flag");
    expectNrcEq(response.nrc, Nrc::RequestOutOfRange, "decode_truncated_positive_nrc");
}

void testDecodeEmptyFrame() {
    const std::vector<std::uint8_t> input = {};
    const DiagResponse response = autodiag::decode(107, input);
    expectBoolEq(response.positive, false, "decode_empty_positive_flag");
    expectNrcEq(response.nrc, Nrc::RequestOutOfRange, "decode_empty_nrc");
}

void testDecodeWrongServiceId() {
    const std::vector<std::uint8_t> input = {0x22, 0xF1, 0x90};
    const DiagResponse response = autodiag::decode(108, input);
    expectBoolEq(response.positive, false, "decode_wrong_service_positive_flag");
    expectNrcEq(response.nrc, Nrc::ServiceNotSupported, "decode_wrong_service_nrc");
}

void testDecodeAllNrcCodes() {
    const std::vector<Nrc> nrcValues = {
        Nrc::ServiceNotSupported,
        Nrc::SubFunctionNotSupported,
        Nrc::SecurityAccessDenied,
        Nrc::AuthenticationRequired,
        Nrc::RequestOutOfRange,
        Nrc::SecurityAccessRequestSequenceError,
    };

    for (const auto nrc : nrcValues) {
        const std::vector<std::uint8_t> input = {0x7F, 0x22, static_cast<std::uint8_t>(nrc)};
        const DiagResponse response = autodiag::decode(109, input);
        expectBoolEq(response.positive, false, "decode_all_nrc_positive_flag");
        expectNrcEq(response.nrc, nrc, "decode_all_nrc_code");
    }
}

void testDecodeMultiByteSocAndRpm() {
    const std::vector<std::uint8_t> rpmInput = {0x62, 0x01, 0x0C, 0x0C, 0x80};
    const DiagResponse rpmResponse = autodiag::decode(110, rpmInput);
    expectStrEq(rpmResponse.valueString, "800", "decode_rpm_multibyte_value");

    const std::vector<std::uint8_t> socInput = {0x62, 0x01, 0x05, 0x4E};
    const DiagResponse socResponse = autodiag::decode(111, socInput);
    expectStrEq(socResponse.valueString, "78", "decode_soc_multibyte_value");
}

void testDecodeVinUtf8Payload() {
    const std::vector<std::uint8_t> input = {
        0x62, 0xF1, 0x90,
        0x56, 0x49, 0x4E, 0x46, 0x41, 0x53, 0x54, 0xE2, 0x9C, 0x93
    };
    const DiagResponse response = autodiag::decode(112, input);
    expectBoolEq(response.positive, true, "decode_vin_utf8_positive_flag");
    expectStrEq(response.valueString, std::string("VINFAST") + "\xE2\x9C\x93", "decode_vin_utf8_value");
}

}  // namespace

int main() {
    testEncodeReadDidVin();
    testEncodeClearDtc();
    testEncodeDiagnosticSessionControl();
    testEncodeReadDtc();
    testEncodeSecurityAccessRequestSeed();
    testEncodeSecurityAccessSendKeyPayload();
    testEncodeTesterPresent();
    testEncodeReadDidWithPayloadAppended();
    testDecodeVinPositive();
    testDecodeRpmPositive();
    testDecodeSocPositive();
    testDecodeSecurityAccessDenied();
    testDecodeRequestOutOfRange();
    testDecodeTruncatedPositiveFrame();
    testDecodeEmptyFrame();
    testDecodeWrongServiceId();
    testDecodeAllNrcCodes();
    testDecodeMultiByteSocAndRpm();
    testDecodeVinUtf8Payload();

    if (g_failures == 0) {
        std::cout << "All encode/decode tests passed. tests=" << g_tests << "\n";
        return 0;
    }

    std::cerr << "Encode/decode tests failed. failures=" << g_failures << " tests=" << g_tests << "\n";
    return 1;
}
