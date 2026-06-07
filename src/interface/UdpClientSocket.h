#pragma once

#include "IUdpSocket.h"
#include <string>
#include <vector>
#include <netinet/in.h>

namespace voice_agent {

class UdpClientSocket : public IUdpSocket {
public:
    UdpClientSocket();
    ~UdpClientSocket() override;

    bool bindSocket(int port) override;
    void setDestination(const std::string& ip, int port) override;

    bool sendData(const std::string& data) override;
    std::string receiveData(int timeoutMs = 0) override;

private:
    int sockfd_;
    struct sockaddr_in destAddr_;
    bool destSet_;
};

} // namespace voice_agent
