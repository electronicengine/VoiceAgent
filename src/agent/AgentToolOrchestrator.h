#pragma once

#include "config/AppConfig.h"
#include "interpreter/IInterpreter.h"
#include "tools/ITool.h"

#include <vector>

namespace voice_agent {

class AgentToolOrchestrator {
public:
    AgentToolOrchestrator(IInterpreter& interpreter, const std::vector<ITool*>& tools, const AppConfig& config);

    AgentTurnResult RunTurn(
        const std::string& userText,
        const InterpreterStreamCallback& onPartialResponse) const;

private:
    ToolCall ParseToolCall(const InterpreterResponse& response) const;
    InterpreterInput FormatToolResultMessage(const ToolCall& call, const ToolResult& result) const;
    const ITool* ResolveTool(const std::string& toolName) const;

    IInterpreter& interpreter_;
    std::vector<ITool*> tools_;
    int maxAgentSteps_ = 1;
};

}  // namespace voice_agent