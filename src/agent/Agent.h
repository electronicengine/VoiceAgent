#pragma once

#include "agent/AgentToolOrchestrator.h"
#include "interpreter/IInterpreter.h"

#include <memory>
#include <string>

namespace voice_agent {

class Agent {
public:
    Agent(
        std::unique_ptr<IInterpreter> interpreter,
        std::string systemPrompt,
        AgentToolOrchestrator agentOrchestrator
    );

    virtual ~Agent() = default;

    virtual void Run() = 0;

protected:
    AgentTurnResult RunTurn(
        const std::string& userText,
        const InterpreterStreamCallback& onPartialResponse) const;

private:
    std::unique_ptr<IInterpreter> interpreter_;
    AgentToolOrchestrator agentOrchestrator_;
};

}  // namespace voice_agent