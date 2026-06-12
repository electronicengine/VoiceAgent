#include "UdpClientSocket.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <sys/time.h>
#include <iostream>
#include "../common/logger.h"

namespace voice_agent {

UdpClientSocket::UdpClientSocket() : sockfd_(-1), destSet_(false) {
    std::memset(&destAddr_, 0, sizeof(destAddr_));
    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) {
        ERROR("Failed to create UDP socket.");
    }
}

UdpClientSocket::~UdpClientSocket() {
    if (sockfd_ >= 0) {
        close(sockfd_);
    }
}

bool UdpClientSocket::bindSocket(int port) {
    if (sockfd_ < 0) return false;

    struct sockaddr_in servAddr;
    std::memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = INADDR_ANY;
    servAddr.sin_port = htons(port);

    if (bind(sockfd_, (const struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) {
        ERROR("Failed to bind UDP socket to port {}", port );
        return false;
    }
    return true;
}

void UdpClientSocket::setDestination(const std::string& ip, int port) {
    std::memset(&destAddr_, 0, sizeof(destAddr_));
    destAddr_.sin_family = AF_INET;
    destAddr_.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &destAddr_.sin_addr);
    destSet_ = true;
}

bool UdpClientSocket::sendData(const std::string& data) {
    if (sockfd_ < 0 || !destSet_) return false;

    ssize_t sentBytes = sendto(sockfd_, data.c_str(), data.size(),
        0, (const struct sockaddr *)&destAddr_, sizeof(destAddr_));
    return sentBytes == static_cast<ssize_t>(data.size());
}

std::string UdpClientSocket::receiveData(int timeoutMs) {
    if (sockfd_ < 0) return "";

    if (timeoutMs > 0) {
        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    } else {
        struct timeval tv = {0, 0};
        setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    char buffer[2048];
    struct sockaddr_in cliAddr;
    socklen_t len = sizeof(cliAddr);

    ssize_t n = recvfrom(sockfd_, buffer, sizeof(buffer),
        0, (struct sockaddr *)&cliAddr, &len);

    if (n > 0) {
        return std::string(buffer, n);
    }
    return "";
}

} // namespace voice_agent
