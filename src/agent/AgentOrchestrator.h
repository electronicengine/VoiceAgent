#pragma once

#include "common/CancellationToken.h"
#include "config/AppConfig.h"
#include "interpreter/IInterpreter.h"
#include "registry/RegistryController.h"
#include "tools/ITool.h"

#include <vector>

namespace voice_agent {

class AgentOrchestrator {
public:
    AgentOrchestrator(
        IInterpreter& interpreter,
        const std::vector<ITool*>& tools,
        const AppConfig& config,
        RegistryController* registryController = nullptr);

    AgentTurnResult RunTurn(
        const std::string& userText,
        const InterpreterStreamCallback& onPartialResponse,
        const CancellationToken* token = nullptr,
        const AnnouncementCallback& onAnnouncement = {}) const;

private:
    std::vector<ToolCall> ParseToolCalls(const InterpreterResponse& response) const;
    InterpreterInput FormatToolResultMessage(
        const std::vector<std::pair<ToolCall, ToolResult>>& stepResults,
        const std::string& originalUserText) const;
    const ITool* ResolveTool(const std::string& toolName) const;

    IInterpreter& interpreter_;
    std::vector<ITool*> tools_;
    RegistryController* registryController_ = nullptr;
    int maxAgentSteps_ = 1;
    std::size_t maxSkillsPerTurn_ = 3;
};

}  // namespace voice_agent