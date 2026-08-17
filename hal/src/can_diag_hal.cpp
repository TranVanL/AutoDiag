#include "can_diag_hal.h"

#include "diag_type.h"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>

namespace autodiag {
namespace can {

namespace {

constexpr int kDefaultTimeoutMs = 2000;

} // namespace

CanDiagnosticHal::CanDiagnosticHal(std::string ifname, uint32_t txId, uint32_t rxId)
    : ifname_(std::move(ifname)), txId_(txId), rxId_(rxId) {}

CanDiagnosticHal::~CanDiagnosticHal() {
    close();
}

bool CanDiagnosticHal::open() {
    if (sockFd_ >= 0) {
        isReady_ = true;
        return true;
    }

    sockFd_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sockFd_ < 0) {
        std::cerr << "[CanDiagnosticHal] socket() failed: " << std::strerror(errno) << '\n';
        isReady_ = false;
        return false;
    }

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname_.c_str(), IFNAMSIZ - 1);
    if (ioctl(sockFd_, SIOCGIFINDEX, &ifr) < 0) {
        std::cerr << "[CanDiagnosticHal] ioctl(SIOCGIFINDEX) failed for " << ifname_
                  << ": " << std::strerror(errno) << '\n';
        close();
        return false;
    }

    struct sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sockFd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[CanDiagnosticHal] bind() failed: " << std::strerror(errno) << '\n';
        close();
        return false;
    }

    isReady_ = true;
    return true;
}

void CanDiagnosticHal::close() {
    if (sockFd_ >= 0) {
        ::close(sockFd_);
        sockFd_ = -1;
    }
    isReady_ = false;
}

bool CanDiagnosticHal::isReady() const {
    return isReady_ && sockFd_ >= 0;
}

void CanDiagnosticHal::reset() {
    close();
    open();
}

void CanDiagnosticHal::flashFirmware(const uint8_t* data, size_t len) {
    // TODO: real implementation using ISO-TP transfer services
    (void)data;
    (void)len;
}

IDiagnosticHal::Result CanDiagnosticHal::SendAndReceive(const std::vector<uint8_t>& req) {
    if (sockFd_ < 0 && !open()) {
        return IDiagnosticHal::Result{false, {}, "Socket not open"};
    }

    codec_.reset();

    // 1. Segment UDS payload into ISO-TP CAN frames.
    auto txFrames = codec_.segment(req, txId_);
    if (txFrames.empty()) {
        return IDiagnosticHal::Result{false, {}, "ISO-TP segmentation failed"};
    }

    // 2. Transmit all frames.
    for (const auto& frame : txFrames) {
        if (!sendFrame(frame)) {
            return IDiagnosticHal::Result{false, {}, "Failed to write CAN frame"};
        }
    }

    // 3. Receive response frames until ISO-TP reassembly is complete.
    bool sentFlowControl = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kDefaultTimeoutMs);

    while (true) {
        int remainingMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count());
        if (remainingMs <= 0) {
            return IDiagnosticHal::Result{false, {}, "Timeout waiting for CAN response"};
        }

        CanFrame rx{};
        if (!receiveFrame(rx, remainingMs)) {
            return IDiagnosticHal::Result{false, {}, "No CAN response received"};
        }

        // Ignore frames not from the expected ECU response ID.
        if (rx.id != rxId_) {
            continue;
        }

        if (!codec_.feedFrame(rx)) {
            return IDiagnosticHal::Result{false, {}, "ISO-TP reassembly error"};
        }

        // After First Frame, send Flow Control to authorize Consecutive Frames.
        if (codec_.getState() == IsotpCodec::State::ReceivingMultiFrame && !sentFlowControl) {
            CanFrame fc = codec_.makeFlowControl(txId_, 0, 0); // CTS, no block limit, no separation
            if (!sendFrame(fc)) {
                return IDiagnosticHal::Result{false, {}, "Failed to send Flow Control"};
            }
            sentFlowControl = true;
        }

        if (codec_.isComplete()) {
            return IDiagnosticHal::Result{true, codec_.getPayload(), {}};
        }

        if (codec_.hasError()) {
            return IDiagnosticHal::Result{false, {}, "ISO-TP protocol error"};
        }
    }
}

IDiagnosticHal::Result CanDiagnosticHal::readProperty(uint32_t propId, uint32_t /*areaId*/) {
    // Map DiagProperty DID to UDS ReadDataByIdentifier request.
    std::vector<uint8_t> req = {
        static_cast<uint8_t>(UdsService::ReadDataByIdentifier),
        static_cast<uint8_t>((propId >> 8) & 0xFF),
        static_cast<uint8_t>(propId & 0xFF)
    };
    return SendAndReceive(req);
}

bool CanDiagnosticHal::sendFrame(const CanFrame& frame) {
    struct can_frame cf{};
    cf.can_id = frame.id;
    cf.can_dlc = frame.len;
    std::memcpy(cf.data, frame.data, 8);

    ssize_t n = write(sockFd_, &cf, sizeof(cf));
    if (n != sizeof(cf)) {
        std::cerr << "[CanDiagnosticHal] write() failed: " << std::strerror(errno) << '\n';
        return false;
    }
    return true;
}

bool CanDiagnosticHal::receiveFrame(CanFrame& frame, int timeoutMs) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sockFd_, &rfds);

    struct timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int ret = select(sockFd_ + 1, &rfds, nullptr, nullptr, &tv);
    if (ret <= 0) {
        return false;
    }

    struct can_frame cf{};
    ssize_t n = read(sockFd_, &cf, sizeof(cf));
    if (n != sizeof(cf)) {
        return false;
    }

    frame.id = cf.can_id;
    frame.len = cf.can_dlc;
    std::memcpy(frame.data, cf.data, 8);
    return true;
}

} // namespace can
} // namespace autodiag
