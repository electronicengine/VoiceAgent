#pragma once

#include "interpreter/InterpreterTypes.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace voice_agent {

enum class ToolRiskLevel {
    Safe,
    Dangerous,
};

struct ToolCall {
    std::string name;
    nlohmann::json arguments = nlohmann::json::object();
};

struct ToolDefinition {
    std::string name;
    std::string description;
    nlohmann::json parameters = nlohmann::json::object();
    std::vector<std::string> aliases;
    ToolRiskLevel riskLevel = ToolRiskLevel::Safe;
};

struct ToolResult {
    bool succeeded = false;
    bool blockedByPolicy = false;
    std::string summary;
    nlohmann::json output = nlohmann::json::object();
};

struct AgentTurnResult {
    InterpreterResponse finalResponse;
    std::vector<ToolCall> executedCalls;
};

}  // namespace voice_agent