#include "agent/AgentOrchestrator.h"
#include <nlohmann/json.hpp>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include "common/CurlGlobalGuard.h"
#include "config/AppConfig.h"
#include "interpreter/OpenAiInterpreter.h"
#include "tools/PythonTool.h"
#include "synthesizer/AzureRestSynthesizer.h"
#include "tools/ShellTool.h"
#include "transcriber/ITranscriber.h"
#include "transcriber/DeepgramTranscriber.h"
#include "transcriber/AzureTranscriber.h"
#include "voice/VoiceAgent.h"

#include <vector>

namespace {

std::string BuildSystemPrompt(
    const std::string& staticPromptText,
    const std::vector<voice_agent::ToolDefinition>& toolDefinitions,
    int maxAgentSteps) {
    nlohmann::json toolList = nlohmann::json::array();
    for (const auto& toolDefinition : toolDefinitions) {
        toolList.push_back({
            {"name", toolDefinition.name},
            {"description", toolDefinition.description},
            {"parameters", toolDefinition.parameters},
            {"aliases", toolDefinition.aliases},
        });
    }

    std::ostringstream prompt;
    prompt << "* En fazla " << maxAgentSteps << " adimda sonuca git." << "\n\n";
    prompt << staticPromptText << "\n\n";
    prompt << "Mevcut araclar:\n" << toolList.dump(2);

    std::cout << "System prompt:\n" << prompt.str() << "\n\n";
    return prompt.str();
}


}  // namespace

int main() {
    try {
        voice_agent::CurlGlobalGuard curlGlobalGuard;
        const voice_agent::AppConfig config = voice_agent::LoadConfig();

        if (!config.resolvedSystemPromptFilePath.empty()) {
            std::cout << "Loaded system prompt file: " << config.resolvedSystemPromptFilePath << "\n";
        }

        auto transcriber = std::make_unique<voice_agent::AzureTranscriber>(config);
        auto interpreter = std::make_unique<voice_agent::OpenAiInterpreter>(config);
        auto synthesizer = std::make_unique<voice_agent::AzureRestSynthesizer>(config);
        voice_agent::ShellTool shellTool;
        voice_agent::PythonTool pythonTool;
        std::vector<voice_agent::ITool*> tools{&shellTool, &pythonTool};
        voice_agent::AgentOrchestrator agentOrchestrator(*interpreter, tools, config);
        const std::string systemPrompt = BuildSystemPrompt(
            config.systemPromptText,
            {shellTool.Definition(), pythonTool.Definition()},
            config.maxAgentSteps
        );

        voice_agent::VoiceAgent agent(
            std::move(transcriber),
            std::move(interpreter),
            std::move(synthesizer),
            systemPrompt,
            std::move(agentOrchestrator)
        );

        agent.Run();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return 1;
    }
}