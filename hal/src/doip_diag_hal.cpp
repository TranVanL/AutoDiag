#include "doip_diag_hal.h"

#include "diag_type.h"

#include <cerrno>
#include <cstring>
#include <sys/types.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using ssize_t = SSIZE_T;
#ifndef close
#define close(s) closesocket(s)
#endif
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace autodiag {

constexpr std::uint8_t DoipProtocolVersion{0x02};
constexpr std::uint8_t DoipInverseProtocolVersion{0xFD};
constexpr std::uint16_t DoipMessageTypeDiagnostic{0x8001};
constexpr std::uint32_t DoipPayloadMaxLength{64U * 1024U};

DoipDiagnosticHal::DoipDiagnosticHal(std::string host, uint16_t port)
    : host_(std::move(host)), port_(port) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    isReady_ = connect();
}

DoipDiagnosticHal::~DoipDiagnosticHal() {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    disconnect();
}

bool DoipDiagnosticHal::isReady() const {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    return isReady_ && sockfd_ >= 0;
}

void DoipDiagnosticHal::reset() {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    disconnect();
    isReady_ = connect();
}

DoipDiagnosticHal::Result DoipDiagnosticHal::SendAndReceive(const std::vector<uint8_t> &req) {
    if (req.empty()) {
        return {false, {}, "Request is empty!"};
    }

    std::lock_guard<std::mutex> lock(socket_mutex_);
    if ((sockfd_ < 0 || !isReady_ ) && !connect()) {
        return {false, {}, "Failed to connect to DoIP server!"};
    }

    std::vector<uint8_t> doipFrame;
    const std::uint16_t PayloadLength = 4U + static_cast<std::uint16_t>(req.size());
    doipFrame.reserve(8U + PayloadLength);
    // 8 bytes DoIP header
    doipFrame.push_back(DoipProtocolVersion);
    doipFrame.push_back(DoipInverseProtocolVersion);
    appendU16(doipFrame, DoipMessageTypeDiagnostic);
    appendU32(doipFrame, PayloadLength);

    // 2 bytes source address (TesterAddress) + 2 bytes target address (EcuAddress)
    appendU16(doipFrame, TesterAddress);
    appendU16(doipFrame, EcuAddress);

    // Append the request payload
    doipFrame.insert(doipFrame.end() , req.begin() , req.end());

    if (!sendAll(doipFrame, doipFrame.size())) {
        disconnect();
        return {false, {}, "Failed to send data to DoIP server!"};
    }

    std::vector<uint8_t> responseHeader(8);
    if (!receiveAll(responseHeader, responseHeader.size())) {
        disconnect();
        return {false, {}, "Failed to receive response header from DoIP server!"};
    }
    const std::uint8_t respProtocolVersion = responseHeader[0];
    const std::uint8_t respInverseProtocolVersion = responseHeader[1];
    const std::uint16_t respMessageType = readU16(&responseHeader[2]);
    const std::uint32_t respPayloadLength = readU32(&responseHeader[4]);

    // Validate header: version must be 0x02, inverse must be 0xFD (= ~0x02 as uint8)
    if (respProtocolVersion != DoipProtocolVersion || respInverseProtocolVersion != DoipInverseProtocolVersion) {
        disconnect();
        return {false, {}, "Invalid DoIP protocol version in response!"};
    }

    if (respMessageType != DoipMessageTypeDiagnostic) {
        disconnect();
        return {false, {}, "Invalid DoIP message type in response!"};
    }

    if (respPayloadLength > DoipPayloadMaxLength || respPayloadLength < 4U) {
        disconnect();
        return {false, {}, "DoIP response payload length is not valid!"};
    }

    std::vector<uint8_t> responsePayload(respPayloadLength);
    if (!receiveAll(responsePayload, responsePayload.size())) {
        disconnect();
        return {false, {}, "Failed to receive response payload from DoIP server!"};
    }

    const std::uint16_t respSourceAddress = readU16(&responsePayload[0]);
    const std::uint16_t respTargetAddress = readU16(&responsePayload[2]);

    if (respSourceAddress != EcuAddress || respTargetAddress != TesterAddress) {
        disconnect();
        return {false, {}, "DoIP response addresses do not match!"};
    }
    std::vector<uint8_t> udsResponse(responsePayload.begin() + 4, responsePayload.end());
    return {true, udsResponse, {}};
}

bool DoipDiagnosticHal::connect() {
    if (sockfd_ >= 0) {
        return true; // Already connected
    }

#if defined(_WIN32)
    static bool wsa_started = false;
    if (!wsa_started) {
        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            isReady_ = false;
            return false;
        }
        wsa_started = true;
    }
    sockfd_ = static_cast<int>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
#else
    sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (sockfd_ < 0) {
        isReady_ = false;
        return false;
    }
    
    // Socket created - now set options and connect
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_DEBUG, "DoIP.HAL", "Connecting to %s:%u", host_.c_str(), port_);
#endif

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    if (::inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr) <= 0) {
        close(sockfd_);
        sockfd_ = -1;
        isReady_ = false;
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_ERROR, "DoIP.HAL", "inet_pton failed for host=%s", host_.c_str());
#endif
        return false;
    }

    timeval timeout{};
    timeout.tv_sec = 5; // 5 seconds timeout
    timeout.tv_usec = 0;
#if defined(_WIN32)
    setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(sockfd_, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif

    if (::connect(sockfd_, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        close(sockfd_);
        sockfd_ = -1;
        isReady_ = false;
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_ERROR, "DoIP.HAL", "connect failed to %s:%u (errno=%d)", host_.c_str(), port_, errno);
#endif
        return false;
    }
    
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "DoIP.HAL", "Connected to DoIP server %s:%u", host_.c_str(), port_);
#endif

    isReady_ = true;
    return true;
}

void DoipDiagnosticHal::disconnect() {
    if (sockfd_ >=0) {
        close(sockfd_);
        sockfd_ = -1;
    }
    isReady_ = false;
}

bool DoipDiagnosticHal::sendAll(const std::vector<uint8_t> &data , std::size_t size) {
    std::size_t totalSent = 0;
    while(totalSent < size) {
        ssize_t t = send(sockfd_, reinterpret_cast<const char*>(data.data() + totalSent), static_cast<int>(size - totalSent), 0);
        if (t < 0) {
            if (errno == EINTR) {
                continue; // Interrupted, try again
            }
            else {
                return false; // Error occurred
            }
        }
        else if (t == 0) {
            return false; // Connection closed
        }
        totalSent += static_cast<std::size_t>(t);
    }
    return true;
}

bool DoipDiagnosticHal::receiveAll(std::vector<uint8_t> &data, std::size_t size) {
    std::size_t totalReceived = 0;
    while(totalReceived < size) {
        ssize_t t = recv(sockfd_, reinterpret_cast<char*>(data.data() + totalReceived), static_cast<int>(size - totalReceived), 0);
        if (t < 0) {
            if (errno == EINTR) {
                continue; // Interrupted, try again
            }
            else {
                return false; // Error occurred
            }
        }
        else if (t == 0) {
            return false; // Connection closed
        }
        totalReceived += static_cast<std::size_t>(t);
    }
    return true;
}

void DoipDiagnosticHal::appendU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
}

void DoipDiagnosticHal::appendU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
}

std::uint16_t DoipDiagnosticHal::readU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8U) |
                                      static_cast<std::uint16_t>(p[1]));
}

std::uint32_t DoipDiagnosticHal::readU32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24U) |
           (static_cast<std::uint32_t>(p[1]) << 16U) |
           (static_cast<std::uint32_t>(p[2]) << 8U) |
           static_cast<std::uint32_t>(p[3]);
}

IDiagnosticHal::Result DoipDiagnosticHal::readProperty(uint32_t propId) {
    // Encode as ReadDataByIdentifier (0x22) request and delegate to SendAndReceive
    const uint16_t did = static_cast<uint16_t>(propId);
    const std::vector<uint8_t> req = {
        static_cast<uint8_t>(UdsService::ReadDataByIdentifier),
        static_cast<uint8_t>((did >> 8U) & 0xFFU),
        static_cast<uint8_t>(did & 0xFFU)
    };
    return SendAndReceive(req);
}

}

