#pragma once

#include "IAgentInterface.h"
#include "IUdpSocket.h"
#include "RobotData.h"
#include "common/llama_operator.h"

#include <string>
#include <vector>
#include <memory>

namespace voice_agent {

class RobotControllerInterface : public IAgentInterface {
public:
    RobotControllerInterface(
        LlamaOperator& llamaOperator, 
        std::unique_ptr<IUdpSocket> udpSocket,
        const std::string& robotIp, 
        int sendPort, 
        int recvPort, 
        const std::string& configFilePath);
    ~RobotControllerInterface() override;

    std::string processUserText(const std::string& userText) override;
    void onSpeakableText(const std::string& speakableText) override;

    void pollSensorData();

private:
    bool sendMessage(MessageType type, const std::string& data);
    void loadConfig(const std::string& configFilePath);
    float cosineSimilarity(const std::vector<float>& vecA, const std::vector<float>& vecB) const;
    void precomputeEmbeddings();

    LlamaOperator& llamaOperator_;
    std::unique_ptr<IUdpSocket> udpSocket_;
    int sendPort_;
    std::string robotIp_;

    std::vector<EmotionalGesture> emotions_;
    std::vector<ReactionalGesture> reactions_;
    std::vector<Directive> directives_;
    std::vector<std::vector<float>> sensorTriggerEmbeddings_;

    SensorData currentSensorData_;
};

} // namespace voice_agent
