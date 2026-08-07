#include "can_diag_hal.h"
#include "isotp_codec.h"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
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

void expectEqInt(int actual, int expected, const char* testName) {
    ++g_tests;
    if (actual != expected) {
        ++g_failures;
        std::cerr << "[FAIL] " << testName
                  << " expected=" << expected
                  << " actual=" << actual << "\n";
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

int openRawCan(const std::string& iface) {
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) return -1;

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        close(sock);
        return -1;
    }

    struct sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

bool sendRawFrame(int sock, const autodiag::can::CanFrame& frame) {
    struct can_frame cf{};
    cf.can_id = frame.id;
    cf.can_dlc = frame.len;
    std::memcpy(cf.data, frame.data, 8);
    return write(sock, &cf, sizeof(cf)) == sizeof(cf);
}

bool vcan0Available() {
    int probe = openRawCan("vcan0");
    if (probe < 0) return false;
    close(probe);
    return true;
}

void testOpenBindsSocket() {
    autodiag::can::CanDiagnosticHal hal("vcan0");
    expectTrue(hal.open(), "can_open_success");
    expectTrue(hal.sockFd() >= 0, "can_open_fd_valid");
    hal.close();
    expectEqInt(hal.sockFd(), -1, "can_close_fd_released");
}

void testSendAndReceiveSingleFrame() {
    autodiag::can::CanDiagnosticHal hal("vcan0", 0x7DF, 0x7E8);
    if (!hal.open()) {
        expectTrue(false, "can_sendrecv_open");
        return;
    }

    int peer = openRawCan("vcan0");
    if (peer < 0) {
        expectTrue(false, "can_sendrecv_peer_socket");
        hal.close();
        return;
    }

    std::thread replier([peer]() {
        struct can_frame cf{};
        ssize_t n = read(peer, &cf, sizeof(cf));
        if (n != sizeof(cf)) return;
        if ((cf.can_id != 0x7DF) || cf.can_dlc < 4) return;

        autodiag::can::CanFrame reply{};
        reply.id = 0x7E8;
        reply.len = 8;
        reply.data[0] = 0x07; // SF + 7 bytes
        reply.data[1] = 0x62;
        reply.data[2] = 0xF1;
        reply.data[3] = 0x90;
        reply.data[4] = 0x31; // '1'
        reply.data[5] = 0x56; // 'V'
        reply.data[6] = 0x44; // 'D'
        reply.data[7] = 0x49; // 'I'
        sendRawFrame(peer, reply);
    });

    std::vector<uint8_t> req = {0x22, 0xF1, 0x90};
    auto res = hal.SendAndReceive(req);

    replier.join();
    close(peer);
    hal.close();

    expectTrue(res.success, "can_sendrecv_success");
    expectTrue(res.data.size() >= 4, "can_sendrecv_min_size");
    if (res.data.size() >= 4) {
        expectEqByte(res.data[0], 0x62, "can_sendrecv_sid");
        expectEqByte(res.data[1], 0xF1, "can_sendrecv_did_hi");
        expectEqByte(res.data[2], 0x90, "can_sendrecv_did_lo");
    }
}

void testCloseReleasesFd() {
    autodiag::can::CanDiagnosticHal hal("vcan0");
    expectTrue(hal.open(), "can_close_open");
    hal.close();
    expectTrue(!hal.isReady(), "can_close_not_ready");
}

}  // namespace

int main() {
    if (!vcan0Available()) {
        std::cerr << "[SKIP] vcan0 not available; run scripts/setup_vcan0.sh first\n";
        return 77; // conventional skip exit code
    }

    testOpenBindsSocket();
    testSendAndReceiveSingleFrame();
    testCloseReleasesFd();

    if (g_failures == 0) {
        std::cout << "All CAN diagnostic HAL tests passed. tests=" << g_tests << "\n";
        return 0;
    }

    std::cerr << "CAN diagnostic HAL tests failed. failures=" << g_failures << " tests=" << g_tests << "\n";
    return 1;
}
