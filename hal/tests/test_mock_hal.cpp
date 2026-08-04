#include "diag_type.h"
#include "mock_diag_hal.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

int g_failures = 0;
int g_tests = 0;

void expectTrue(bool condition, const char* testName) {
    ++g_tests;
    if (!condition) {
        ++g_failures;
        std::cerr << "[FAIL] " << testName << "\n";
    }
}

void expectEqByte(std::uint8_t actual, std::uint8_t expected, const char* testName) {
    ++g_tests;
    if (actual != expected) {
        ++g_failures;
        std::cerr << "[FAIL] " << testName
                  << " expected=" << static_cast<int>(expected)
                  << " actual=" << static_cast<int>(actual) << "\n";
    }
}

void expectVecEq(const std::vector<std::uint8_t>& actual,
                 const std::vector<std::uint8_t>& expected,
                 const char* testName) {
    ++g_tests;
    if (actual != expected) {
        ++g_failures;
        std::cerr << "[FAIL] " << testName
                  << " expected size=" << expected.size()
                  << " actual size=" << actual.size() << "\n";
    }
}

void testReadDidVinSuccess() {
    autodiag::MockDiagnosticHal hal;
    const auto result = hal.SendAndReceive({0x22, 0xF1, 0x90});

    expectTrue(result.success, "mock_read_did_vin_success_flag");
    expectTrue(result.error.empty(), "mock_read_did_vin_no_error");
    expectTrue(result.data.size() >= 4, "mock_read_did_vin_min_size");
    if (result.data.size() < 3) {
        return;
    }
    expectEqByte(result.data[0], 0x62, "mock_read_did_vin_sid");
    expectEqByte(result.data[1], 0xF1, "mock_read_did_vin_did_hi");
    expectEqByte(result.data[2], 0x90, "mock_read_did_vin_did_lo");
}

void testReadDidUnknownReturnsNrc31() {
    autodiag::MockDiagnosticHal hal;
    const auto result = hal.SendAndReceive({0x22, 0x12, 0x34});

    expectTrue(result.success, "mock_read_unknown_success_transport");
    expectVecEq(result.data, {0x7F, 0x22, 0x31}, "mock_read_unknown_nrc31_frame");
}

void testReadDtcDefaultList() {
    autodiag::MockDiagnosticHal hal;
    const auto result = hal.SendAndReceive({0x19, 0x02});

    expectTrue(result.success, "mock_read_dtc_success");
    expectTrue(result.data.size() > 2, "mock_read_dtc_has_payload");
    if (result.data.size() < 2) {
        return;
    }
    expectEqByte(result.data[0], 0x59, "mock_read_dtc_sid");
    expectEqByte(result.data[1], 0x02, "mock_read_dtc_subfunction");
}

void testClearDtcThenReadDtcEmpty() {
    autodiag::MockDiagnosticHal hal;

    const auto clearResult = hal.SendAndReceive({0x14, 0xFF, 0xFF, 0xFF});
    expectTrue(clearResult.success, "mock_clear_dtc_success");
    expectVecEq(clearResult.data, {0x54, 0xFF, 0xFF, 0xFF}, "mock_clear_dtc_positive_frame");

    const auto readAfterClear = hal.SendAndReceive({0x19, 0x02});
    expectTrue(readAfterClear.success, "mock_read_dtc_after_clear_success");
    expectVecEq(readAfterClear.data, {0x59, 0x02}, "mock_read_dtc_after_clear_empty_payload");
}

void testTesterPresentPositive() {
    autodiag::MockDiagnosticHal hal;
    const auto result = hal.SendAndReceive({0x3E, 0x00});

    expectTrue(result.success, "mock_tester_present_success");
    expectVecEq(result.data, {0x7E, 0x00}, "mock_tester_present_positive_frame");
    expectTrue(hal.isReady(), "mock_is_ready_true");
}

void testReadPropertySocInRange() {
    autodiag::MockDiagnosticHal hal;
    // Run a few times to catch out-of-range drift values
    for (int i = 0; i < 20; ++i) {
        const auto result = hal.readProperty(static_cast<uint32_t>(autodiag::DiagProperty::BatterySoc));
        expectTrue(result.success, "read_property_soc_success");
        expectTrue(result.data.size() == 1, "read_property_soc_size_1");
        if (!result.data.empty()) {
            const uint8_t soc = result.data[0];
            expectTrue(soc >= 77 && soc <= 82, "read_property_soc_in_range_77_82");
        }
    }
}

void testReadPropertyRpmInRange() {
    autodiag::MockDiagnosticHal hal;
    for (int i = 0; i < 20; ++i) {
        const auto result = hal.readProperty(static_cast<uint32_t>(autodiag::DiagProperty::RPM));
        expectTrue(result.success, "read_property_rpm_success");
        expectTrue(result.data.size() == 2, "read_property_rpm_size_2");
        if (result.data.size() == 2) {
            const int rpm = (static_cast<int>(result.data[0]) << 8) | result.data[1];
            expectTrue(rpm >= 2900 && rpm <= 3500, "read_property_rpm_in_range_2900_3500");
        }
    }
}

void testReadPropertyVinConstant() {
    autodiag::MockDiagnosticHal hal;
    const auto result = hal.readProperty(static_cast<uint32_t>(autodiag::DiagProperty::VIN));
    expectTrue(result.success, "read_property_vin_success");
    const std::string expected = "VINFAST12345678901";
    const std::string actual(result.data.begin(), result.data.end());
    expectTrue(actual == expected, "read_property_vin_value");
}

void testReadPropertyTirePressureAreaAware() {
    autodiag::MockDiagnosticHal hal;

    const auto fl = hal.readProperty(
            static_cast<uint32_t>(autodiag::DiagProperty::TirePressure),
            static_cast<uint32_t>(autodiag::DiagArea::FL));
    expectTrue(fl.success, "read_property_tire_pressure_fl_success");
    expectTrue(fl.data.size() == 1, "read_property_tire_pressure_fl_size_1");
    if (!fl.data.empty()) {
        expectEqByte(fl.data[0], 24, "read_property_tire_pressure_fl_value");
    }

    const auto fr = hal.readProperty(
            static_cast<uint32_t>(autodiag::DiagProperty::TirePressure),
            static_cast<uint32_t>(autodiag::DiagArea::FR));
    expectTrue(fr.success, "read_property_tire_pressure_fr_success");
    expectTrue(fr.data.size() == 1, "read_property_tire_pressure_fr_size_1");
    if (!fr.data.empty()) {
        expectEqByte(fr.data[0], 25, "read_property_tire_pressure_fr_value");
    }

    const auto invalid = hal.readProperty(
            static_cast<uint32_t>(autodiag::DiagProperty::TirePressure),
            static_cast<uint32_t>(autodiag::DiagArea::Global));
    expectTrue(!invalid.success, "read_property_tire_pressure_invalid_unavailable");
}

}  // namespace

int main() {
    testReadDidVinSuccess();
    testReadDidUnknownReturnsNrc31();
    testReadDtcDefaultList();
    testClearDtcThenReadDtcEmpty();
    testTesterPresentPositive();
    testReadPropertySocInRange();
    testReadPropertyRpmInRange();
    testReadPropertyVinConstant();
    testReadPropertyTirePressureAreaAware();

    if (g_failures == 0) {
        std::cout << "All mock HAL tests passed. tests=" << g_tests << "\n";
        return 0;
    }

    std::cerr << "Mock HAL tests failed. failures=" << g_failures << " tests=" << g_tests << "\n";
    return 1;
}
