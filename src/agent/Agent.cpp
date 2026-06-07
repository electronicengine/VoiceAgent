#include "agent/Agent.h"

#include <utility>

namespace voice_agent {

Agent::Agent(
    std::unique_ptr<IInterpreter> interpreter,
    std::string systemPrompt,
    AgentOrchestrator agentOrchestrator)
    : interpreter_(std::move(interpreter)),
      agentOrchestrator_(std::move(agentOrchestrator)) {
    interpreter_->ResetSession(std::move(systemPrompt));
}

AgentTurnResult Agent::RunTurn(
    const std::string& userText,
    const InterpreterStreamCallback& onPartialResponse,
    const CancellationToken* token,
    const AnnouncementCallback& onAnnouncement) const {
    
    std::string processedText = userText;
    for (const auto& iface : interfaces_) {
        processedText = iface->processUserText(processedText);
    }
    
    // Wrap the interpreter callback so interfaces can hook into partial sentences
    auto wrappedPartialResponse = [this, onPartialResponse](const InterpreterResponse& partialResponse) {
        if (!interfaces_.empty()) {
            std::string sentence = partialResponse.SpeakableText();
            if (!sentence.empty()) {
                for (const auto& iface : interfaces_) {
                    iface->onSpeakableText(sentence);
                }
            }
        }
        if (onPartialResponse) {
            onPartialResponse(partialResponse);
        }
    };

    AgentTurnResult result = agentOrchestrator_.RunTurn(processedText, wrappedPartialResponse, token, onAnnouncement);

    return result;
}

}  // namespace voice_agent