#pragma once

#include "common/CancellationToken.h"
#include "config/AppConfig.h"
#include "interpreter/IInterpreter.h"
#include "skills/SkillRegistry.h"
#include "tools/ITool.h"

#include <vector>

namespace voice_agent {

class AgentToolOrchestrator {
public:
    AgentToolOrchestrator(
        IInterpreter& interpreter,
        const std::vector<ITool*>& tools,
        const AppConfig& config,
        const SkillRegistry* skillRegistry = nullptr);

    AgentTurnResult RunTurn(
        const std::string& userText,
        const InterpreterStreamCallback& onPartialResponse,
        const CancellationToken* token = nullptr,
        const AnnouncementCallback& onAnnouncement = {}) const;

private:
    ToolCall ParseToolCall(const InterpreterResponse& response) const;
    InterpreterInput FormatToolResultMessage(const ToolCall& call, const ToolResult& result) const;
    const ITool* ResolveTool(const std::string& toolName) const;

    IInterpreter& interpreter_;
    std::vector<ITool*> tools_;
    const SkillRegistry* skillRegistry_ = nullptr;
    int maxAgentSteps_ = 1;
    std::size_t maxSkillsPerTurn_ = 3;
};

}  // namespace voice_agent