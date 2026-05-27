#include "agent/Agent.h"

#include <utility>

namespace voice_agent {

Agent::Agent(
    std::unique_ptr<IInterpreter> interpreter,
    std::string systemPrompt,
    AgentToolOrchestrator agentOrchestrator)
    : interpreter_(std::move(interpreter)),
      agentOrchestrator_(std::move(agentOrchestrator)) {
    interpreter_->ResetSession(std::move(systemPrompt));
}

AgentTurnResult Agent::RunTurn(
    const std::string& userText,
    const InterpreterStreamCallback& onPartialResponse) const {
    return agentOrchestrator_.RunTurn(userText, onPartialResponse);
}

}  // namespace voice_agent