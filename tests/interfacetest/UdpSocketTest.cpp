#include <gtest/gtest.h>
#include "interface/UdpClientSocket.h"
#include "interface/UdpServerSocket.h"
#include <thread>
#include <chrono>

using namespace voice_agent;

TEST(UdpSocketTest, ClientServerCommunication) {
    UdpServerSocket server;
    UdpClientSocket client;

    int port = 12345;
    ASSERT_TRUE(server.bindSocket(port));

    client.setDestination("127.0.0.1", port);

    std::string testMsg = "Hello from Client";
    bool sent = client.sendData(testMsg);
    EXPECT_TRUE(sent);

    // Give some time for packet delivery (local loopback should be fast)
    std::string received = server.receiveData(100); 
    EXPECT_EQ(received, testMsg);

    // Server should now have lastClientAddr_ set and be able to reply
    std::string replyMsg = "Hello from Server";
    bool replied = server.sendData(replyMsg);
    EXPECT_TRUE(replied);

    std::string clientReceived = client.receiveData(100);
    EXPECT_EQ(clientReceived, replyMsg);
}

TEST(UdpSocketTest, ServerExplicitDestination) {
    UdpServerSocket server;
    UdpClientSocket client;

    int serverPort = 12346;
    int clientPort = 12347;

    ASSERT_TRUE(server.bindSocket(serverPort));
    ASSERT_TRUE(client.bindSocket(clientPort));

    // Force server to send to client's bound port
    server.setDestination("127.0.0.1", clientPort);
    
    std::string msg = "Server Initiated";
    server.sendData(msg);

    std::string received = client.receiveData(100);
    EXPECT_EQ(received, msg);
}
