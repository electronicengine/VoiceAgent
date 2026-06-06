#include "agent/TextAgent.h"

#include "common/StringUtils.h"
#include "common/logger.h"

#include <iostream>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace voice_agent {

TextAgent::TextAgent(
    std::unique_ptr<IInterpreter> interpreter,
    std::string systemPrompt,
    AgentToolOrchestrator agentOrchestrator)
    : Agent(std::move(interpreter), std::move(systemPrompt), std::move(agentOrchestrator)) {}

void TextAgent::Run() {
    INFO("Text agent is ready. Type your message.");
    INFO("Type 'exit' or 'quit' to stop.\n");

    std::string userText;
    while (true) {
        LOG_RAW("You: ");
        if (!std::getline(std::cin, userText)) {
            break;
        }

        userText = Trim(userText);
        if (userText.empty()) {
            WARNING("Input cannot be empty.\n");
            continue;
        }

        const std::string lowered = ToLower(userText);
        if (lowered == "exit" || lowered == "quit") {
            break;
        }

        LOG_RAW("Agent: ");
        bool streamedAnyText = false;

        const AgentTurnResult turnResult = RunTurn(
            userText,
            [&streamedAnyText](const InterpreterResponse& partialResponse) {
                const std::string streamedText = partialResponse.SpeakableText();
                if (streamedText.empty()) {
                    return;
                }

                streamedAnyText = true;
                LOG_RAW("{} ", streamedText);
            },
            nullptr,
            [](const std::string& announcement) {
                INFO("[announcement] {}", announcement);
            }
        );
        const InterpreterResponse& response = turnResult.finalResponse;
        if (response.Empty()) {
            WARNING("Model bu turda bos bir cevap dondurdu. Program acik kaldi; tekrar deneyebilirsiniz.");
            continue;
        }

        const std::string displayText = response.DisplayText();
        if (!streamedAnyText && !response.SpeakableText().empty()) {
            LOG_RAW("{}", response.SpeakableText());
        }
        LOG_RAW("\n\n");
        if (!response.SpeakableText().empty() && Trim(displayText) != Trim(response.SpeakableText())) {
            INFO("Agent details:\n{}", displayText);
        }
    }
}

}  // namespace voice_agent