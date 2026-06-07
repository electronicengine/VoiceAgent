#pragma once

#include "AgentOrchestrator.h"
#include "common/CancellationToken.h"
#include "interpreter/IInterpreter.h"
#include "interface/IAgentInterface.h"

#include <memory>
#include <string>
#include <vector>

namespace voice_agent {

class Agent {
    public:
        Agent(
            std::unique_ptr<IInterpreter> interpreter,
            std::string systemPrompt,
            AgentOrchestrator agentOrchestrator
        );

        virtual ~Agent() = default;

        virtual void Run() = 0;

        void AddInterface(std::shared_ptr<IAgentInterface> agentInterface) {
            interfaces_.push_back(std::move(agentInterface));
        }

    protected:
        AgentTurnResult RunTurn(
            const std::string& userText,
            const InterpreterStreamCallback& onPartialResponse,
            const CancellationToken* token = nullptr,
            const AnnouncementCallback& onAnnouncement = {}) const;

    private:
        std::unique_ptr<IInterpreter> interpreter_;
        AgentOrchestrator agentOrchestrator_;
    protected:
        std::vector<std::shared_ptr<IAgentInterface>> interfaces_;
};

}  // namespace voice_agent