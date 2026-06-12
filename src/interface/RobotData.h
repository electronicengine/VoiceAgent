#pragma once

#include <string>
#include <map>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace voice_agent {

using Json = nlohmann::json;

enum class MessageType {
    VideoFrame,
    SensorData,
    ControlData,
    DatabaseInsertData,
    LLMQuery,
    RecognizedSpeech,
    LLMResponse,
    EngageReaction,
    RecognizedGesture,
    GesturePerformanceCompleted,
    InteractiveChatStarted,
    SensorReadRequest,
    SpeakRequest,
    UpdateRAGDatabaseRequest,
    ClearRAGDatabaseRequest,
    ShowRAGDatabaseRequest,
    AIModeOnCall,
    AIModeOffCall,
    StopPerceptionRequest,
    StartPerceptionRequest,
    CameraSnapShotRequest,
    CameraSnapShotResponse,
};

struct CameraSnapShotResponseData : public MessageData {
    CameraSnapShotResponseData(const std::string& path) {
        imagePath = path;
    }
  std::string imagePath;
};

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




enum class DirectiveType {
    followFinger,
    stopFollow,
};

std::map<std::string, DirectiveType> directiveStringMap = {
    {"follow_finger", DirectiveType::followFinger},
    {"stop_follow", DirectiveType::stopFollow}
};

inline DirectiveType stringToDirectiveType(const std::string& directiveString) {
    auto it = directiveStringMap.find(directiveString);
    if (it != directiveStringMap.end()) {
        return it->second;
    }
    return static_cast<DirectiveType>(255); // Unknown directive
}

enum class EmotionType{
    happy,
    angry,
    funny,
    serious,
    curious,
    worried,
    surprised,
    confident,
};

std::map<std::string, EmotionType> emotionStringMap = {
    {"happy", EmotionType::happy},
    {"angry", EmotionType::angry},
    {"funny", EmotionType::funny},
    {"serious", EmotionType::serious},
    {"curious", EmotionType::curious},
    {"worried", EmotionType::worried},
    {"surprised", EmotionType::surprised},
    {"confident", EmotionType::confident}
};

inline EmotionType stringToEmotionType(const std::string& emotionString) {
    auto it = emotionStringMap.find(emotionString);
    if (it != emotionStringMap.end()) {
        return it->second;
    }
    return static_cast<EmotionType>(255); // Unknown emotion
}

enum class ReactionType{
    greeting,
    listening,
    talking,
    accepting,
    rejecting,
    thinking,
    agreeing
};


struct EmotionalGesture {
    EmotionType emotion;
    std::string symbol;
    std::string description;
    std::map<ServoMotorJoint, uint8_t> motorPos;
    std::vector<float> embedding;
};

std::map<std::string, ReactionType> reactionStringMap = {
    {"greeting", ReactionType::greeting},
    {"listening", ReactionType::listening},
    {"talking", ReactionType::talking},
    {"accepting", ReactionType::accepting},
    {"rejecting", ReactionType::rejecting},
    {"thinking", ReactionType::thinking},
    {"agreeing", ReactionType::agreeing}
};

inline ReactionType stringToReactionType(const std::string& reactionString) {
    auto it = reactionStringMap.find(reactionString);
    if (it != reactionStringMap.end()) {
        return it->second;
    }
    return static_cast<ReactionType>(255); // Unknown reaction
}


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

    LLMResponseData() = default;

    LLMResponseData(const std::string& jsonStr) {

        Json j = Json::parse(jsonStr);

        if (!j.is_object()) {
            return;
        }

        // Zorunlu alanlar (to_json tarafında yazılanlar)
        sentence = j.value("sentence", std::string{});

        emotionalGesture.emotion = (EmotionType) j.value("emotional_gesture", 255);
        reactionalGesture.reaction = (ReactionType) j.value("reactional_gesture", 255);
        directive.symbol = j.value("directive", std::string{});

        endMarker = j.value("end_marker", false);

        // Similarity değerleri yoksa 0.0f default
        emotionSimilarity = j.value("emotion_similarity", 0.0f);
        reactionSimilarity = j.value("reaction_similarity", 0.0f);
        directiveSimilarity = j.value("directive_similarity", 0.0f);

    }


    [[nodiscard]] std::string to_json() const {
        Json j;
        j["sentence"] = sentence;
        j["emotional_gesture"] = emotionalGesture.emotion;
        j["reactional_gesture"] = reactionalGesture.reaction;
        j["directive"] = directive.symbol;
        j["end_marker"] = endMarker;
        j["emotion_similarity"] = emotionSimilarity;
        j["reaction_similarity"] = reactionSimilarity;
        j["directive_similarity"] = directiveSimilarity;
        return j.dump();
    }
};

} // namespace voice_agent
