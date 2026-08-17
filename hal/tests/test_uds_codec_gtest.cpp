#include "diag_type.h"
#include "uds_codec.h"

#include <cstdint>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace autodiag {
namespace {

using ::testing::ElementsAre;

class UdsCodecEncodeTest : public ::testing::Test {};

TEST_F(UdsCodecEncodeTest, ReadDidVin) {
    DiagRequest req{};
    req.service = UdsService::ReadDataByIdentifier;
    req.dataId = static_cast<std::uint16_t>(DiagProperty::VIN);
    EXPECT_THAT(encode(req), ElementsAre(0x22, 0xF1, 0x90));
}

TEST_F(UdsCodecEncodeTest, ClearDtc) {
    DiagRequest req{};
    req.service = UdsService::ClearDTC;
    EXPECT_THAT(encode(req), ElementsAre(0x14, 0xFF, 0xFF, 0xFF));
}

TEST_F(UdsCodecEncodeTest, DiagnosticSessionControl) {
    DiagRequest req{};
    req.service = UdsService::DiagnosticSessionControl;
    req.subFunction = 0x03;
    EXPECT_THAT(encode(req), ElementsAre(0x10, 0x03));
}

TEST_F(UdsCodecEncodeTest, ReadDtc) {
    DiagRequest req{};
    req.service = UdsService::ReadDTC;
    req.subFunction = 0x02;
    EXPECT_THAT(encode(req), ElementsAre(0x19, 0x02));
}

TEST_F(UdsCodecEncodeTest, SecurityAccessRequestSeed) {
    DiagRequest req{};
    req.service = UdsService::SecurityAccess;
    req.subFunction = 0x01;
    EXPECT_THAT(encode(req), ElementsAre(0x27, 0x01));
}

TEST_F(UdsCodecEncodeTest, SecurityAccessSendKeyPayload) {
    DiagRequest req{};
    req.service = UdsService::SecurityAccess;
    req.subFunction = 0x02;
    req.payload = {0x12, 0x34, 0x56, 0x78};
    EXPECT_THAT(encode(req), ElementsAre(0x27, 0x02, 0x12, 0x34, 0x56, 0x78));
}

TEST_F(UdsCodecEncodeTest, TesterPresent) {
    DiagRequest req{};
    req.service = UdsService::TesterPresent;
    req.subFunction = 0x00;
    EXPECT_THAT(encode(req), ElementsAre(0x3E, 0x00));
}

TEST_F(UdsCodecEncodeTest, ReadDidWithPayloadAppended) {
    DiagRequest req{};
    req.service = UdsService::ReadDataByIdentifier;
    req.dataId = static_cast<std::uint16_t>(DiagProperty::SoftwareVer);
    req.payload = {0xAA, 0xBB};
    EXPECT_THAT(encode(req), ElementsAre(0x22, 0xF1, 0x87, 0xAA, 0xBB));
}

class UdsCodecDecodeTest : public ::testing::Test {};

TEST_F(UdsCodecDecodeTest, VinPositive) {
    const std::vector<std::uint8_t> input = {
        0x62, 0xF1, 0x90,
        'V', 'I', 'N', 'F', 'A', 'S', 'T',
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1'
    };
    const DiagResponse response = decode(101, input);
    EXPECT_TRUE(response.positive);
    EXPECT_EQ(response.valueString, "VINFAST12345678901");
}

TEST_F(UdsCodecDecodeTest, SoftwareVersionPositive) {
    const std::vector<std::uint8_t> input = {
        0x62, 0xF1, 0x87,
        'S', 'W', '_', 'V', '3', '.', '2', '.', '1', '_', 'A', 'A', 'O', 'S'
    };
    const DiagResponse response = decode(150, input);
    EXPECT_TRUE(response.positive);
    EXPECT_EQ(response.valueString, "SW_V3.2.1_AAOS");
}

TEST_F(UdsCodecDecodeTest, RpmPositive) {
    const std::vector<std::uint8_t> input = {0x62, 0x01, 0x0C, 0x0C, 0x80};
    const DiagResponse response = decode(102, input);
    EXPECT_TRUE(response.positive);
    EXPECT_EQ(response.valueString, "800");
}

TEST_F(UdsCodecDecodeTest, SocPositive) {
    const std::vector<std::uint8_t> input = {0x62, 0x01, 0x05, 0x4E};
    const DiagResponse response = decode(103, input);
    EXPECT_TRUE(response.positive);
    EXPECT_EQ(response.valueString, "78");
}

TEST_F(UdsCodecDecodeTest, SecurityAccessDenied) {
    const std::vector<std::uint8_t> input = {0x7F, 0x22, 0x13};
    const DiagResponse response = decode(104, input);
    EXPECT_FALSE(response.positive);
    EXPECT_EQ(response.nrc, Nrc::SecurityAccessDenied);
}

TEST_F(UdsCodecDecodeTest, RequestOutOfRange) {
    const std::vector<std::uint8_t> input = {0x7F, 0x22, 0x31};
    const DiagResponse response = decode(105, input);
    EXPECT_FALSE(response.positive);
    EXPECT_EQ(response.nrc, Nrc::RequestOutOfRange);
}

TEST_F(UdsCodecDecodeTest, TruncatedPositiveFrame) {
    const std::vector<std::uint8_t> input = {0x62, 0xF1};
    const DiagResponse response = decode(106, input);
    EXPECT_FALSE(response.positive);
    EXPECT_EQ(response.nrc, Nrc::RequestOutOfRange);
}

TEST_F(UdsCodecDecodeTest, EmptyFrame) {
    const std::vector<std::uint8_t> input = {};
    const DiagResponse response = decode(107, input);
    EXPECT_FALSE(response.positive);
    EXPECT_EQ(response.nrc, Nrc::RequestOutOfRange);
}

TEST_F(UdsCodecDecodeTest, WrongServiceId) {
    const std::vector<std::uint8_t> input = {0x22, 0xF1, 0x90};
    const DiagResponse response = decode(108, input);
    EXPECT_FALSE(response.positive);
    EXPECT_EQ(response.nrc, Nrc::ServiceNotSupported);
}

TEST_F(UdsCodecDecodeTest, DtcListPositive) {
    const std::vector<std::uint8_t> input = {
        0x59, 0x02,
        'P', '0', 'A', '0', '0', ',', ' ', 'P', '0', '5', '6', '2'
    };
    const DiagResponse response = decode(151, input);
    EXPECT_TRUE(response.positive);
    EXPECT_EQ(response.valueString, "P0A00, P0562");
}

TEST_F(UdsCodecDecodeTest, ClearDtcPositive) {
    const std::vector<std::uint8_t> input = {0x54, 0xFF, 0xFF, 0xFF};
    const DiagResponse response = decode(152, input);
    EXPECT_TRUE(response.positive);
    EXPECT_EQ(response.valueString, "OK");
}

TEST_F(UdsCodecDecodeTest, AllNrcCodes) {
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
        const DiagResponse response = decode(109, input);
        EXPECT_FALSE(response.positive);
        EXPECT_EQ(response.nrc, nrc);
    }
}

TEST_F(UdsCodecDecodeTest, MultiByteSocAndRpm) {
    const std::vector<std::uint8_t> rpmInput = {0x62, 0x01, 0x0C, 0x0C, 0x80};
    const DiagResponse rpmResponse = decode(110, rpmInput);
    EXPECT_EQ(rpmResponse.valueString, "800");

    const std::vector<std::uint8_t> socInput = {0x62, 0x01, 0x05, 0x4E};
    const DiagResponse socResponse = decode(111, socInput);
    EXPECT_EQ(socResponse.valueString, "78");
}

TEST_F(UdsCodecDecodeTest, VinUtf8Payload) {
    const std::vector<std::uint8_t> input = {
        0x62, 0xF1, 0x90,
        0x56, 0x49, 0x4E, 0x46, 0x41, 0x53, 0x54, 0xE2, 0x9C, 0x93
    };
    const DiagResponse response = decode(112, input);
    EXPECT_TRUE(response.positive);
    EXPECT_EQ(response.valueString, std::string("VINFAST") + "\xE2\x9C\x93");
}

}  // namespace
}  // namespace autodiag
