#include "diag_type.h"
#include "mock_diag_hal.h"

#include <cstdint>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace autodiag {
namespace {

using ::testing::ElementsAre;

class MockHalTest : public ::testing::Test {};

TEST_F(MockHalTest, ReadDidVinSuccess) {
    MockDiagnosticHal hal;
    const auto result = hal.SendAndReceive({0x22, 0xF1, 0x90});

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error.empty());
    ASSERT_GE(result.data.size(), 4u);
    EXPECT_EQ(result.data[0], 0x62);
    EXPECT_EQ(result.data[1], 0xF1);
    EXPECT_EQ(result.data[2], 0x90);
}

TEST_F(MockHalTest, ReadDidUnknownReturnsNrc31) {
    MockDiagnosticHal hal;
    const auto result = hal.SendAndReceive({0x22, 0x12, 0x34});

    EXPECT_TRUE(result.success);
    EXPECT_THAT(result.data, ElementsAre(0x7F, 0x22, 0x31));
}

TEST_F(MockHalTest, ReadDtcDefaultList) {
    MockDiagnosticHal hal;
    const auto result = hal.SendAndReceive({0x19, 0x02});

    EXPECT_TRUE(result.success);
    ASSERT_GT(result.data.size(), 2u);
    EXPECT_EQ(result.data[0], 0x59);
    EXPECT_EQ(result.data[1], 0x02);
}

TEST_F(MockHalTest, ClearDtcThenReadDtcEmpty) {
    MockDiagnosticHal hal;

    const auto clearResult = hal.SendAndReceive({0x14, 0xFF, 0xFF, 0xFF});
    EXPECT_TRUE(clearResult.success);
    EXPECT_THAT(clearResult.data, ElementsAre(0x54, 0xFF, 0xFF, 0xFF));

    const auto readAfterClear = hal.SendAndReceive({0x19, 0x02});
    EXPECT_TRUE(readAfterClear.success);
    EXPECT_THAT(readAfterClear.data, ElementsAre(0x59, 0x02));
}

TEST_F(MockHalTest, TesterPresentPositive) {
    MockDiagnosticHal hal;
    const auto result = hal.SendAndReceive({0x3E, 0x00});

    EXPECT_TRUE(result.success);
    EXPECT_THAT(result.data, ElementsAre(0x7E, 0x00));
    EXPECT_TRUE(hal.isReady());
}

TEST_F(MockHalTest, ReadPropertySocInRange) {
    MockDiagnosticHal hal;
    for (int i = 0; i < 20; ++i) {
        const auto result = hal.readProperty(static_cast<uint32_t>(DiagProperty::BatterySoc), 0);
        EXPECT_TRUE(result.success);
        ASSERT_EQ(result.data.size(), 1u);
        const uint8_t soc = result.data[0];
        EXPECT_GE(soc, 77);
        EXPECT_LE(soc, 82);
    }
}

TEST_F(MockHalTest, ReadPropertyRpmInRange) {
    MockDiagnosticHal hal;
    for (int i = 0; i < 20; ++i) {
        const auto result = hal.readProperty(static_cast<uint32_t>(DiagProperty::RPM), 0);
        EXPECT_TRUE(result.success);
        ASSERT_EQ(result.data.size(), 2u);
        const int rpm = (static_cast<int>(result.data[0]) << 8) | result.data[1];
        EXPECT_GE(rpm, 2900);
        EXPECT_LE(rpm, 3500);
    }
}

TEST_F(MockHalTest, ReadPropertyVinConstant) {
    MockDiagnosticHal hal;
    const auto result = hal.readProperty(static_cast<uint32_t>(DiagProperty::VIN), 0);
    EXPECT_TRUE(result.success);
    const std::string expected = "VINFAST12345678901";
    const std::string actual(result.data.begin(), result.data.end());
    EXPECT_EQ(actual, expected);
}

TEST_F(MockHalTest, ReadPropertyTirePressureAreaAware) {
    MockDiagnosticHal hal;

    const auto fl = hal.readProperty(
            static_cast<uint32_t>(DiagProperty::TirePressure),
            static_cast<uint32_t>(DiagArea::FL));
    EXPECT_TRUE(fl.success);
    ASSERT_EQ(fl.data.size(), 1u);
    EXPECT_EQ(fl.data[0], 24);

    const auto fr = hal.readProperty(
            static_cast<uint32_t>(DiagProperty::TirePressure),
            static_cast<uint32_t>(DiagArea::FR));
    EXPECT_TRUE(fr.success);
    ASSERT_EQ(fr.data.size(), 1u);
    EXPECT_EQ(fr.data[0], 25);

    const auto invalid = hal.readProperty(
            static_cast<uint32_t>(DiagProperty::TirePressure),
            static_cast<uint32_t>(DiagArea::Global));
    EXPECT_FALSE(invalid.success);
}

}  // namespace
}  // namespace autodiag
