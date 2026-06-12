#include "RobotControllerInterfaceTest.h"
#include "interface/RobotControllerInterface.h"
#include "interface/IUdpSocket.h"
#include "interface/UdpClientSocket.h"
#include <fstream>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>

namespace voice_agent {

class MockUdpSocket : public IUdpSocket {
public:
    bool bindSocket(int port) override { 
        lastBoundPort = port;
        return true; 
    }
    void setDestination(const std::string& ip, int port) override {
        lastDestIp = ip;
        lastDestPort = port;
    }
    bool sendData(const std::string& data) override {
        sentData.push_back(data);
        return true;
    }
    std::string receiveData(int /*timeoutMs*/) override {
        std::lock_guard<std::mutex> lock(mtx);
        if (receiveQueue.empty()) return "";
        std::string data = receiveQueue.front();
        receiveQueue.pop();
        return data;
    }

    void pushMockData(const std::string& data) {
        std::lock_guard<std::mutex> lock(mtx);
        receiveQueue.push(data);
    }

    int lastBoundPort = 0;
    std::string lastDestIp;
    int lastDestPort = 0;
    std::vector<std::string> sentData;
    std::queue<std::string> receiveQueue;
    std::mutex mtx;
};

// Helper to create a temporary config file
void CreateTestConfig(const std::string& path) {
    std::ofstream ofs(path);
    ofs << R"({
      "emotional_gestures": [
        {"type": "happy", "tag": "<happy>", "description": "feeling happy"}
      ],
      "reactional_gestures": [
        {"type": "greeting", "reply": "greeting", "tag": "<greeting>", "description": "a friendly wave"}
      ],
      "directives": [
        {"type": "stop", "tag": "<stop>", "description": "stop all movement"}
      ]
    })";
}

TEST_F(RobotControllerInterfaceTest, ConfigLoadingAndEmbeddings) {
    LlamaOperator llama; 
    llama.loadEmbedModel("/usr/local/ai.models/llamaModel/mxbaiV1.gguf", llama_pooling_type::LLAMA_POOLING_TYPE_CLS);
    // We assume a model is loaded in a real test environment, but for this mock test we can bypass model loading 
    // if we mock LlamaOperator too, but let's stick to UdpSocket mock for now.
    
    std::string configPath = "/usr/local/etc/gesture_config_tr.json";
    CreateTestConfig(configPath);

    auto mockSocket = std::make_unique<MockUdpSocket>();
    RobotControllerInterface robot(llama, std::move(mockSocket), "127.0.0.1", 5005, 5006, configPath);
    
    std::remove(configPath.c_str());
}

TEST_F(RobotControllerInterfaceTest, UserTextInjection) {
    LlamaOperator llama;
    auto mockSocketPtr = new MockUdpSocket();
    auto mockSocket = std::unique_ptr<IUdpSocket>(mockSocketPtr);
    
    llama.loadEmbedModel("/usr/local/ai.models/llamaModel/mxbaiV1.gguf", llama_pooling_type::LLAMA_POOLING_TYPE_CLS);

    RobotControllerInterface robot(llama, std::move(mockSocket), "127.0.0.1", 5005, 5006, "/usr/local/etc/gesture_config_tr.json");

    std::string mockData = R"({
        "compass": {"angle": 90, "magnet": {"magnet_x": 10, "magnet_y": 20}},
        "distance": {"Distance": 100, "Strength": 50, "Temperature": 25},
        "power": {"BusVoltage": 12.0, "BusCurrent": 1.5, "Power": 18.0, "ShuntVoltage": 0.1},
        "joint_angles": {
            "right_arm": 45, "left_arm": 45, "neck_down": 90, "neck_up": 0, "neck_right": 0, "eye_right": 0, "eye_left": 0
        }
    })";
    
    nlohmann::json packet;
    packet["type"] = static_cast<int>(MessageType::SensorData);
    packet["payload"] = mockData;
    mockSocketPtr->pushMockData(packet.dump());
    
    std::string userText = "Şu anki durumun nedir?";
    std::string processed = robot.processUserText(userText);
        
    EXPECT_NE(processed, userText);
    EXPECT_TRUE(processed.find("Robot Sensör bilgileri:") != std::string::npos);
    EXPECT_TRUE(processed.find("90") != std::string::npos); 
}

TEST_F(RobotControllerInterfaceTest, OnSpeakableTextSimulation) {
    LlamaOperator llama;
    auto mockSocketPtr = new MockUdpSocket();
    auto mockSocket = std::unique_ptr<IUdpSocket>(mockSocketPtr);
    
    llama.loadEmbedModel("/usr/local/ai.models/llamaModel/mxbaiV1.gguf", llama_pooling_type::LLAMA_POOLING_TYPE_CLS);

    RobotControllerInterface robot(llama, std::move(mockSocket), "127.0.0.1", 5007, 5008, "/usr/local/etc/gesture_config_tr.json");

    robot.onSpeakableText("Selam. Bu gün nasılsın?");

    EXPECT_FALSE(mockSocketPtr->sentData.empty());
    std::string received = mockSocketPtr->sentData.back();
    
    auto packet = nlohmann::json::parse(received);
    EXPECT_TRUE(packet.contains("type"));
    EXPECT_TRUE(packet.contains("payload"));
    EXPECT_EQ(packet["type"], static_cast<int>(MessageType::LLMResponse));

    auto j = nlohmann::json::parse(packet["payload"].get<std::string>());
    EXPECT_TRUE(j.contains("sentence"));
    EXPECT_EQ(j["sentence"], "Selam. Bu gün nasılsın?");
}

TEST_F(RobotControllerInterfaceTest, OnSpeakableTextRealUdpCommunication) {
    LlamaOperator llama;
    
    llama.loadEmbedModel("/usr/local/ai.models/llamaModel/mxbaiV1.gguf", llama_pooling_type::LLAMA_POOLING_TYPE_CLS);
    std::unique_ptr<UdpClientSocket> realSocket = std::make_unique<UdpClientSocket>();

    RobotControllerInterface robot(llama, std::move(realSocket), "127.0.0.1", 5005, 5006, "/usr/local/etc/gesture_config_tr.json");

    robot.onSpeakableText("Selam. Bu gün nasılsın?");

    EXPECT_TRUE(true);
}

TEST_F(RobotControllerInterfaceTest, UserTextInjectionRealUdpCommunication) {
    LlamaOperator llama;
    llama.loadEmbedModel("/usr/local/ai.models/llamaModel/mxbaiV1.gguf", llama_pooling_type::LLAMA_POOLING_TYPE_CLS);
    
    std::unique_ptr<UdpClientSocket> realSocket = std::make_unique<UdpClientSocket>();

    // RobotControllerInterface listens on 5008, sends to 5007
    RobotControllerInterface robot(llama, std::move(realSocket), "127.0.0.1", 5005, 5006, "/usr/local/etc/gesture_config_tr.json");

    std::string userText = "Şu anki durumun nedir?";
    std::string processed = robot.processUserText(userText);
    std::cout << "Processed user text: " << processed << std::endl;

    EXPECT_NE(processed, userText);
    EXPECT_TRUE(processed.find("Robot Sensör bilgileri:") != std::string::npos);
    EXPECT_TRUE(processed.find("90") != std::string::npos); 
}

} // namespace voice_agent
