#include "isotp_codec.h"

#include <cstdint>
#include <cstring>
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

void expectEqUint(unsigned actual, unsigned expected, const char* testName) {
    ++g_tests;
    if (actual != expected) {
        ++g_failures;
        std::cerr << "[FAIL] " << testName
                  << " expected=" << expected
                  << " actual=" << actual << "\n";
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

void testSingleFrameRoundTrip() {
    autodiag::can::IsotpCodec codec;
    std::vector<uint8_t> payload = {0x01, 0x05}; // SID 01 PID 05
    auto frames = codec.segment(payload, 0x7DF);

    expectEqUint(frames.size(), 1u, "isotp_sf_frame_count");
    if (frames.empty()) return;
    expectEqUint(frames[0].id, 0x7DFu, "isotp_sf_id");
    expectEqByte(frames[0].data[0], 0x02, "isotp_sf_pci");
    expectEqByte(frames[0].data[1], 0x01, "isotp_sf_byte1");
    expectEqByte(frames[0].data[2], 0x05, "isotp_sf_byte2");

    autodiag::can::IsotpCodec receiver;
    expectTrue(receiver.feedFrame(frames[0]), "isotp_sf_feed_ok");
    expectTrue(receiver.isComplete(), "isotp_sf_complete");
    expectVecEq(receiver.getPayload(), payload, "isotp_sf_payload");
}

void testSingleFrameSevenBytes() {
    autodiag::can::IsotpCodec codec;
    std::vector<uint8_t> payload = {0x22, 0xF1, 0x90, 0xAA, 0xBB, 0xCC, 0xDD};
    auto frames = codec.segment(payload, 0x7E0);

    expectEqUint(frames.size(), 1u, "isotp_sf7_frame_count");
    if (frames.empty()) return;
    expectEqByte(frames[0].data[0], 0x07, "isotp_sf7_pci");

    autodiag::can::IsotpCodec receiver;
    expectTrue(receiver.feedFrame(frames[0]), "isotp_sf7_feed_ok");
    expectVecEq(receiver.getPayload(), payload, "isotp_sf7_payload");
}

void testMultiFrameTwentyBytes() {
    autodiag::can::IsotpCodec codec;
    std::vector<uint8_t> payload(20);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i);

    auto frames = codec.segment(payload, 0x7E0);
    expectEqUint(frames.size(), 3u, "isotp_mf20_frame_count");
    if (frames.size() < 3) return;

    expectEqByte(frames[0].data[0], 0x10, "isotp_mf20_ff_pci");
    expectEqByte(frames[0].data[1], 0x14, "isotp_mf20_ff_len");
    expectEqByte(frames[1].data[0], 0x21, "isotp_mf20_cf1_seq");
    expectEqByte(frames[2].data[0], 0x22, "isotp_mf20_cf2_seq");

    autodiag::can::IsotpCodec receiver;
    expectTrue(receiver.feedFrame(frames[0]), "isotp_mf20_ff_feed");
    expectTrue(!receiver.isComplete(), "isotp_mf20_not_complete_after_ff");
    expectTrue(receiver.feedFrame(frames[1]), "isotp_mf20_cf1_feed");
    expectTrue(!receiver.isComplete(), "isotp_mf20_not_complete_after_cf1");
    expectTrue(receiver.feedFrame(frames[2]), "isotp_mf20_cf2_feed");
    expectTrue(receiver.isComplete(), "isotp_mf20_complete");
    expectVecEq(receiver.getPayload(), payload, "isotp_mf20_payload");
}

void testFlowControlFormat() {
    autodiag::can::IsotpCodec codec;
    autodiag::can::CanFrame fc = codec.makeFlowControl(0x7E8, 8, 20);

    expectEqUint(fc.id, 0x7E8u, "isotp_fc_id");
    expectEqUint(fc.len, 8u, "isotp_fc_len");
    expectEqByte(fc.data[0], 0x30, "isotp_fc_pci_cts");
    expectEqByte(fc.data[1], 0x08, "isotp_fc_block_size");
    expectEqByte(fc.data[2], 0x14, "isotp_fc_st_min");
    for (int i = 3; i < 8; ++i) {
        expectEqByte(fc.data[i], 0x00, "isotp_fc_padding");
    }
}

void testIncompleteSequenceNotComplete() {
    autodiag::can::IsotpCodec codec;
    autodiag::can::CanFrame ff{};
    ff.id = 0x7E8;
    ff.len = 8;
    ff.data[0] = 0x10;
    ff.data[1] = 0x14; // expecting 20 bytes
    std::memset(&ff.data[2], 0xAB, 6);

    expectTrue(codec.feedFrame(ff), "isotp_incomplete_ff_feed");
    expectTrue(!codec.isComplete(), "isotp_incomplete_not_complete");
    expectTrue(codec.getState() == autodiag::can::IsotpCodec::State::ReceivingMultiFrame,
               "isotp_incomplete_state_receiving");
}

void testOversizedPayloadRejected() {
    autodiag::can::IsotpCodec codec;
    std::vector<uint8_t> payload(4096, 0xAA);
    auto frames = codec.segment(payload, 0x7E0);
    expectTrue(frames.empty(), "isotp_oversized_rejected");
}

void testNibbleDecoding() {
    using namespace autodiag::can;
    expectEqByte(getIsoTpType(0x02), ISO_TP_SF, "isotp_nibble_sf");
    expectEqByte(getIsoTpType(0x10), ISO_TP_FF, "isotp_nibble_ff");
    expectEqByte(getIsoTpType(0x21), ISO_TP_CF, "isotp_nibble_cf");
    expectEqByte(getIsoTpType(0x30), ISO_TP_FC, "isotp_nibble_fc");

    expectEqByte(getSingleFrameLength(0x07), 7u, "isotp_nibble_sf_len");
    expectEqUint(getFirstFrameLength(0x10, 0x14), 20u, "isotp_nibble_ff_len");
    expectEqByte(getConsecutiveSequence(0x2A), 0xAu, "isotp_nibble_cf_seq");
    expectEqByte(getFlowStatus(0x32), 0x02u, "isotp_nibble_fc_fs");
}

void testConsecutiveFrameDlcTooShort() {
    autodiag::can::IsotpCodec codec;
    std::vector<uint8_t> payload(20, 0);
    auto frames = codec.segment(payload, 0x7E0);
    if (frames.size() < 2) return;

    autodiag::can::IsotpCodec receiver;
    expectTrue(receiver.feedFrame(frames[0]), "isotp_cf_short_ff_feed");

    autodiag::can::CanFrame badCf = frames[1];
    badCf.len = 1;
    expectTrue(!receiver.feedFrame(badCf), "isotp_cf_short_rejected");
    expectTrue(receiver.hasError(), "isotp_cf_short_error");
}

void testBackToBackReassembly() {
    autodiag::can::IsotpCodec codec;
    std::vector<uint8_t> payload1 = {0x01, 0x0C};       // RPM request
    std::vector<uint8_t> payload2 = {0x22, 0xF1, 0x90}; // readDataByIdentifier VIN

    auto frames1 = codec.segment(payload1, 0x7DF);
    auto frames2 = codec.segment(payload2, 0x7DF);

    autodiag::can::IsotpCodec receiver;
    expectTrue(receiver.feedFrame(frames1[0]), "isotp_back2back_first_feed");
    expectVecEq(receiver.getPayload(), payload1, "isotp_back2back_first_payload");

    receiver.reset();
    expectTrue(receiver.feedFrame(frames2[0]), "isotp_back2back_second_feed");
    expectVecEq(receiver.getPayload(), payload2, "isotp_back2back_second_payload");
}

}  // namespace

int main() {
    testSingleFrameRoundTrip();
    testSingleFrameSevenBytes();
    testMultiFrameTwentyBytes();
    testFlowControlFormat();
    testIncompleteSequenceNotComplete();
    testOversizedPayloadRejected();
    testNibbleDecoding();
    testConsecutiveFrameDlcTooShort();
    testBackToBackReassembly();

    if (g_failures == 0) {
        std::cout << "All ISO-TP codec tests passed. tests=" << g_tests << "\n";
        return 0;
    }

    std::cerr << "ISO-TP codec tests failed. failures=" << g_failures << " tests=" << g_tests << "\n";
    return 1;
}
