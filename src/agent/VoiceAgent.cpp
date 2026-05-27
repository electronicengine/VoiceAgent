#include "agent/VoiceAgent.h"

#include "common/StringUtils.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace voice_agent {

VoiceAgent::VoiceAgent(
    std::unique_ptr<ITranscriber> transcriber,
    std::unique_ptr<IInterpreter> interpreter,
    std::unique_ptr<ISynthesizer> synthesizer,
    std::string systemPrompt,
    AgentToolOrchestrator agentOrchestrator)
    : Agent(std::move(interpreter), std::move(systemPrompt), std::move(agentOrchestrator)),
      transcriber_(std::move(transcriber)),
      synthesizer_(std::move(synthesizer)) {}

void VoiceAgent::Run() {
    std::cout << "Voice agent is ready. Speak into your microphone.\n";
    std::cout << "Say 'exit' or 'quit' to stop.\n\n";

    while (true) {
        std::cout << "Listening...\n";
        const std::string userText = Trim(transcriber_->ListenOnce());
        if (userText.empty()) {
            std::cout << "Speech could not be recognized.\n\n";
            continue;
        }

        const std::string lowered = ToLower(userText);
        if (lowered == "exit" || lowered == "quit") {
            break;
        }

        std::cout << "You: " << userText << "\n";
        std::cout << "Agent: " << std::flush;
        bool streamedAnyText = false;

        const AgentTurnResult turnResult = RunTurn(
            userText,
            [this, &streamedAnyText](const InterpreterResponse& partialResponse) {
                const std::string streamedText = partialResponse.SpeakableText();
                if (streamedText.empty()) {
                    return;
                }
                streamedAnyText = true;
                std::cout << streamedText << ' ' << std::flush;
                synthesizer_->Synthesize(partialResponse);
            }
        );
        const InterpreterResponse& response = turnResult.finalResponse;
        if (response.Empty()) {
            throw std::runtime_error("Interpreter returned an empty response.");
        }

        const std::string displayText = response.DisplayText();
        if (!streamedAnyText && !response.SpeakableText().empty()) {
            std::cout << response.SpeakableText();
        }
        std::cout << "\n\n";
        if (!response.SpeakableText().empty() && Trim(displayText) != Trim(response.SpeakableText())) {
            std::cout << "Agent details:\n" << displayText << "\n\n";
        }
    }
}

}  // namespace voice_agent