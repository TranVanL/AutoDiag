#include "diag_type.h"
#include "uds_codec.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using autodiag::DiagProperty;
using autodiag::DiagRequest;
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

    if (g_failures == 0) {
        std::cout << "All encode tests passed. tests=" << g_tests << "\n";
        return 0;
    }

    std::cerr << "Encode tests failed. failures=" << g_failures << " tests=" << g_tests << "\n";
    return 1;
}
