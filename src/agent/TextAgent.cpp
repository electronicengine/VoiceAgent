#include "agent/TextAgent.h"

#include "common/StringUtils.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace voice_agent {

TextAgent::TextAgent(
    std::unique_ptr<IInterpreter> interpreter,
    std::string systemPrompt,
    AgentToolOrchestrator agentOrchestrator)
    : Agent(std::move(interpreter), std::move(systemPrompt), std::move(agentOrchestrator)) {}

void TextAgent::Run() {
    std::cout << "Text agent is ready. Type your message.\n";
    std::cout << "Type 'exit' or 'quit' to stop.\n\n";

    std::string userText;
    while (true) {
        std::cout << "You: " << std::flush;
        if (!std::getline(std::cin, userText)) {
            break;
        }

        userText = Trim(userText);
        if (userText.empty()) {
            std::cout << "Input cannot be empty.\n\n";
            continue;
        }

        const std::string lowered = ToLower(userText);
        if (lowered == "exit" || lowered == "quit") {
            break;
        }

        std::cout << "Agent: " << std::flush;
        bool streamedAnyText = false;

        const AgentTurnResult turnResult = RunTurn(
            userText,
            [&streamedAnyText](const InterpreterResponse& partialResponse) {
                const std::string streamedText = partialResponse.SpeakableText();
                if (streamedText.empty()) {
                    return;
                }

                streamedAnyText = true;
                std::cout << streamedText << ' ' << std::flush;
            },
            nullptr,
            [](const std::string& announcement) {
                std::cout << "[announcement] " << announcement << "\n";
            }
        );
        const InterpreterResponse& response = turnResult.finalResponse;
        if (response.Empty()) {
            std::cout << "Model bu turda bos bir cevap dondurdu. Program acik kaldi; tekrar deneyebilirsiniz.\n\n";
            continue;
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