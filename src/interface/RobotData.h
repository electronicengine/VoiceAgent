#pragma once

#include <string>
#include <map>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace voice_agent {

using Json = nlohmann::json;

enum class DCMotorDirection {
    forward,
    backward
};

enum class DCMotor {
    left,
    right
};

struct DCMotorState {
    int leftMotorMagnitude;
    DCMotorDirection leftMotorDirection;
    int rightMotorMagnitude;
    DCMotorDirection rightMotorDirection;
};

struct CompassData {
    uint16_t angle;
    int16_t magnetX;
    int16_t magnetY;
};

struct DistanceData {
    int distance;
    int strength;
    int temperature;
};

struct PowerData {
    float busVoltage;
    float shuntVoltage;
    float current;
    float power;
};

enum class ServoMotorJoint {
    rightArm = 0,
    leftArm,
    neck,
    headUpDown,
    headLeftRight,
    eyeRight,
    eyeLeft,
};

struct GestureJointState {
    uint8_t angle;
    int speed;
};

struct MotionSequenceItem {
    int delay;
    std::map<ServoMotorJoint, GestureJointState> joints;
};

struct MessageData {
    virtual ~MessageData() = default;
};

struct SensorData : public MessageData {
    std::optional<CompassData> compassData;
    std::optional<DistanceData> distanceData;
    std::optional<PowerData> powerData;
    std::optional<std::map<ServoMotorJoint, uint8_t>> currentJointAngles;
    std::optional<DCMotorState> dcMotorState;

    [[nodiscard]] std::string to_json() const {
        if (!compassData.has_value() || !distanceData.has_value() || !powerData.has_value() || !currentJointAngles.has_value()) {
            return "";
        }

        Json joint_angles = {
            {"right_arm", currentJointAngles->at(ServoMotorJoint::rightArm)},
            {"left_arm", currentJointAngles->at(ServoMotorJoint::leftArm)},
            {"neck_down", currentJointAngles->at(ServoMotorJoint::neck)},
            {"neck_up", currentJointAngles->at(ServoMotorJoint::headUpDown)},
            {"neck_right", currentJointAngles->at(ServoMotorJoint::headLeftRight)},
            {"eye_right", currentJointAngles->at(ServoMotorJoint::eyeRight)},
            {"eye_left", currentJointAngles->at(ServoMotorJoint::eyeLeft)}
        };

        Json compass = {
            {"angle", compassData->angle},
            {"magnet", {
                {"magnet_x", compassData->magnetX},
                {"magnet_y", compassData->magnetY}
            }}
        };

        Json distance = {
            {"Distance", distanceData->distance},
            {"Strength", distanceData->strength},
            {"Temperature", distanceData->temperature}
        };

        Json power = {
            {"BusVoltage", powerData->busVoltage},
            {"BusCurrent", powerData->current},
            {"Power", powerData->power},
            {"ShuntVoltage", powerData->shuntVoltage}
        };

        Json metadata = {
            {"joint_angles", joint_angles},
            {"compass", compass},
            {"distance", distance},
            {"power", power}
        };

        return metadata.dump();
    }
};

struct DirectiveMotion {
    std::string name;  
    std::string description;
    int duration; // milliseconds
    std::map<ServoMotorJoint, GestureJointState> joints;
    std::vector<MotionSequenceItem> sequence;
};

using EmotionType = std::string;
using ReactionType = std::string;
using DirectiveType = std::string;

struct EmotionalGesture {
    EmotionType emotion;
    std::string symbol;
    std::string description;
    std::map<ServoMotorJoint, uint8_t> motorPos;
    std::vector<float> embedding;
};

struct ReactionalGesture {
    ReactionType reaction;
    ReactionType antiReaction;
    std::string symbol;
    std::string description;
    std::vector<float> embedding;
};

struct Directive {
    DirectiveType directive;
    std::string symbol;
    std::string description;
    std::vector<float> embedding;
};

struct LLMResponseData : public MessageData {
    std::string sentence;   // bu interpreterden gelen speakabletext. 
    EmotionalGesture emotionalGesture;
    ReactionalGesture reactionalGesture;
    Directive directive;
    bool endMarker;

    float emotionSimilarity;
    float reactionSimilarity;
    float directiveSimilarity;

    [[nodiscard]] std::string to_json() const {
        Json j;
        j["sentence"] = sentence;
        j["emotional_gesture"] = emotionalGesture.symbol;
        j["reactional_gesture"] = reactionalGesture.symbol;
        j["directive"] = directive.symbol;
        j["end_marker"] = endMarker;
        j["emotion_similarity"] = emotionSimilarity;
        j["reaction_similarity"] = reactionSimilarity;
        j["directive_similarity"] = directiveSimilarity;
        return j.dump();
    }
};

} // namespace voice_agent
