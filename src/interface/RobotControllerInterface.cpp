#include "RobotControllerInterface.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <nlohmann/json.hpp>
#include "../common/logger.h"

namespace voice_agent {

using Json = nlohmann::json;

RobotControllerInterface::RobotControllerInterface(
    LlamaOperator& llamaOperator,
    std::unique_ptr<IUdpSocket> udpSocket,
    const std::string& robotIp,
    int sendPort,
    int recvPort,
    const std::string& configFilePath)
    : llamaOperator_(llamaOperator),
      udpSocket_(std::move(udpSocket)),
      sendPort_(sendPort),
      robotIp_(robotIp) {
    
    // Setup UDP socket
    udpSocket_->bindSocket(recvPort);
    udpSocket_->setDestination(robotIp_, sendPort_);

    loadConfig(configFilePath);
    precomputeEmbeddings();
}

RobotControllerInterface::~RobotControllerInterface() = default;

void RobotControllerInterface::loadConfig(const std::string& configFilePath) {
    std::ifstream file(configFilePath);
    if (!file.is_open()) {
        ERROR("Failed to open config file: {}", configFilePath);
        return;
    }

    Json configJson;
    try {
        file >> configJson;

        if (configJson.contains("emotional_gestures")) {
            for (const auto& item : configJson["emotional_gestures"]) {
                EmotionalGesture eg;
                std::string emotionString = item.value("type", "");
                eg.emotion = stringToEmotionType(emotionString);
                eg.symbol = item.value("tag", "");
                eg.description = item.value("description", "");
                emotions_.push_back(eg);
            }
        }

        if (configJson.contains("reactional_gestures")) {
            for (const auto& item : configJson["reactional_gestures"]) {
                ReactionalGesture rg;
                std::string reactionString = item.value("type", "");
                rg.reaction = stringToReactionType(reactionString);
                rg.antiReaction = stringToReactionType(item.value("anti_reaction", ""));
                rg.symbol = item.value("tag", "");
                rg.description = item.value("description", "");
                reactions_.push_back(rg);
            }
        }

        if (configJson.contains("directives")) {
            for (const auto& item : configJson["directives"]) {
                Directive d;
                std::string directiveString = item.value("type", "");
                d.directive = stringToDirectiveType(directiveString);
                d.symbol = item.value("tag", "");
                d.description = item.value("description", "");
                directives_.push_back(d);
            }
        }

    } catch (const std::exception& e) {
        ERROR("Error parsing config file: {}",  e.what());
    }
}

void RobotControllerInterface::precomputeEmbeddings() {
    for (auto& eg : emotions_) {
        if (!eg.description.empty()) {
            eg.embedding = llamaOperator_.calculateEmbeddings(eg.description);
        }
    }
    for (auto& rg : reactions_) {
        if (!rg.description.empty()) {
            rg.embedding = llamaOperator_.calculateEmbeddings(rg.description);
        }
    }
    for (auto& d : directives_) {
        if (!d.description.empty()) {
            d.embedding = llamaOperator_.calculateEmbeddings(d.description);
        }
    }

    std::vector<std::string> sensorPhrases = {
        "Sensör değerleri ve robot bilgileri",
        "Engel tespiti ve çevre haritası",
        "Motor pozisyon bilgileri ve robot durum bilgisi."
    };
    for (const auto& phrase : sensorPhrases) {
        sensorTriggerEmbeddings_.push_back(llamaOperator_.calculateEmbeddings(phrase));
    }
}

float RobotControllerInterface::cosineSimilarity(const std::vector<float>& vecA, const std::vector<float>& vecB) const {
    if (vecA.empty() || vecB.empty() || vecA.size() != vecB.size()) {
        return 0.0f;
    }

    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    for (size_t i = 0; i < vecA.size(); ++i) {
        dot += vecA[i] * vecB[i];
        normA += vecA[i] * vecA[i];
        normB += vecB[i] * vecB[i];
    }
    
    if (normA == 0.0f || normB == 0.0f) {
        return 0.0f;
    }
    return dot / (std::sqrt(normA) * std::sqrt(normB));
}

std::string RobotControllerInterface::processUserText(const std::string& userText) {
    std::vector<float> sentenceEmbedding = llamaOperator_.calculateEmbeddings(userText);
    INFO("sentenceEmbedding.size: {}", sentenceEmbedding.size());
    bool shouldPoll = false;
    float threshold = 0.6f;

    for (const auto& triggerEmbedding : sensorTriggerEmbeddings_) {
        auto sim = llamaOperator_.getSimilarity(sentenceEmbedding, triggerEmbedding);
        INFO("triggerEmbedding.size: {}", triggerEmbedding.size());
        INFO("sim: {}", sim);
        if (sim > threshold) {
            shouldPoll = true;
            break;
        }
    }

    if (!shouldPoll) {
        return userText;
    }

    pollSensorData();

    std::string sensorInfo = currentSensorData_.to_json();
    if (sensorInfo.empty()) {
        return userText;
    }

    std::string processd = "Robot Sensör bilgileri: " + sensorInfo + "\n Kullanıcı Sorusu: " + userText;
    INFO("processed: {}", processd);

    // Inject sensor data into user text so the language model is aware
    return processd;
}

void RobotControllerInterface::onSpeakableText(const std::string& speakableText) {
    if (speakableText.empty()) return;

    std::vector<float> sentenceEmbedding = llamaOperator_.calculateEmbeddings(speakableText);
    
    LLMResponseData responseData;
    responseData.sentence = speakableText;
    responseData.emotionSimilarity = -1.0f;
    responseData.reactionSimilarity = -1.0f;
    responseData.directiveSimilarity = -1.0f;
    responseData.endMarker = false;

    // Find best emotion
    for (const auto& eg : emotions_) {
        float sim = llamaOperator_.getSimilarity(sentenceEmbedding, eg.embedding);
        if (sim > responseData.emotionSimilarity) {
            responseData.emotionSimilarity = sim;
            responseData.emotionalGesture = eg;
        }
    }

    // Find best reaction
    for (const auto& rg : reactions_) {
        float sim = llamaOperator_.getSimilarity(sentenceEmbedding, rg.embedding);
        if (sim > responseData.reactionSimilarity) {
            responseData.reactionSimilarity = sim;
            responseData.reactionalGesture = rg;
        }
    }

    // Find best directive
    for (const auto& d : directives_) {
        float sim = llamaOperator_.getSimilarity(sentenceEmbedding, d.embedding);
        if (sim > responseData.directiveSimilarity) {
            responseData.directiveSimilarity = sim;
            responseData.directive = d;
        }
    }

    //DEBUG("Sending message MessageType::LLMResponse: {}", responseData.to_json());

    // Send the control directive to the robot
    sendMessage(MessageType::LLMResponse, responseData.to_json());
}

bool RobotControllerInterface::sendMessage(MessageType type, const std::string& data) {
    Json packet;
    packet["type"] = static_cast<int>(type);
    packet["payload"] = data;
    return udpSocket_->sendData(packet.dump());
}

void RobotControllerInterface::pollSensorData() {
    // Attempt to receive data, non-blocking
    sendMessage(MessageType::SensorReadRequest, "");
    std::string rawPacket = udpSocket_->receiveData(10);
    INFO("Received raw packet: {}", rawPacket);
    if (!rawPacket.empty()) {
        try {
            Json packet = Json::parse(rawPacket);
            if (!packet.contains("type") || !packet.contains("payload")) {
                WARNING("Received malformed packet: {}", rawPacket);
                return;
            }

            MessageType type = static_cast<MessageType>(packet["type"].get<int>());
            if (type != MessageType::SensorData) {
                WARNING("Received unexpected message type: {}", static_cast<int>(type));
                WARNING("Received  payload: {}", packet["payload"].get<std::string>());
                return;
            }

            std::string data = packet["payload"];
            Json j = Json::parse(data);
            
            // Assume the json structure matches what we parse below
            if (j.contains("compass")) {
                currentSensorData_.compassData = CompassData{
                    j["compass"].value("angle", (uint16_t)0),
                    j["compass"]["magnet"].value("magnet_x", (int16_t)0),
                    j["compass"]["magnet"].value("magnet_y", (int16_t)0)
                };
            }
            if (j.contains("distance")) {
                currentSensorData_.distanceData = DistanceData{
                    j["distance"].value("Distance", 0),
                    j["distance"].value("Strength", 0),
                    j["distance"].value("Temperature", 0)
                };
            }
            if (j.contains("power")) {
                currentSensorData_.powerData = PowerData{
                    j["power"].value("BusVoltage", 0.0f),
                    j["power"].value("ShuntVoltage", 0.0f),
                    j["power"].value("BusCurrent", 0.0f),
                    j["power"].value("Power", 0.0f)
                };
            }
            if (j.contains("joint_angles")) {
                std::map<ServoMotorJoint, uint8_t> joints;
                joints[ServoMotorJoint::rightArm] = j["joint_angles"].value("right_arm", 0);
                joints[ServoMotorJoint::leftArm] = j["joint_angles"].value("left_arm", 0);
                joints[ServoMotorJoint::neck] = j["joint_angles"].value("neck_down", 0);
                joints[ServoMotorJoint::headUpDown] = j["joint_angles"].value("neck_up", 0);
                joints[ServoMotorJoint::headLeftRight] = j["joint_angles"].value("neck_right", 0);
                joints[ServoMotorJoint::eyeRight] = j["joint_angles"].value("eye_right", 0);
                joints[ServoMotorJoint::eyeLeft] = j["joint_angles"].value("eye_left", 0);
                currentSensorData_.currentJointAngles = joints;
            }
        } catch (...) {
            ERROR("Error parsing sensor data packet: {}", rawPacket);
        }
        
    }
}

} // namespace voice_agent
