#pragma once

#include "agent/Agent.h"

#include <memory>
#include <string>

namespace voice_agent {

class TextAgent : public Agent {
public:
    TextAgent(
        std::unique_ptr<IInterpreter> interpreter,
        std::string systemPrompt,
        AgentOrchestrator agentOrchestrator
    );

    void Run() override;
};

}  // namespace voice_agent