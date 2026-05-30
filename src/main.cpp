#include "agent/AgentToolOrchestrator.h"
#include <nlohmann/json.hpp>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include "common/CurlGlobalGuard.h"
#include "common/IUserPromptProvider.h"
#include "common/StdinUserPromptProvider.h"
#include "common/VoiceUserPromptProvider.h"
#include "agent/Agent.h"
#include "config/AppConfig.h"
#include "config/AccountStore.h"
#include "agent/TextAgent.h"
#include "interpreter/OpenAiInterpreter.h"
#include "tools/PythonTool.h"
#include "synthesizer/AzureRestSynthesizer.h"
#include "tools/ShellTool.h"
#include "tools/WebBrowserTool.h"
#include "transcriber/ITranscriber.h"
#include "transcriber/DeepgramTranscriber.h"
#include "transcriber/AzureTranscriber.h"
#include "agent/VoiceAgent.h"
#include "audio/AlsaMicrophone.h"
#include "audio/AlsaSpeaker.h"
#include "audio/VoiceController.h"
#include "audio/VoiceActivityDetector.h"

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
    prompt << "Kullanilabilir araclar:\n";
    prompt << toolList.dump(2) << "\n\n";

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

        voice_agent::AccountStore accountStore;
        try {
            accountStore.Load(
                config.resolvedAccountsFilePath,
                config.resolvedAccountsRootDir
            );
            if (accountStore.Loaded()) {
                std::cout << "Loaded accounts file: " << config.resolvedAccountsFilePath
                          << " (" << accountStore.AccountIds().size() << " hesap)\n";
            }
        } catch (const std::exception& ex) {
            std::cerr << "account.json yuklenemedi: " << ex.what() << "\n";
        }

        auto interpreter = std::make_unique<voice_agent::OpenAiInterpreter>(config);
        voice_agent::ShellTool shellTool;
        voice_agent::PythonTool pythonTool;
        voice_agent::WebBrowserTool webBrowserTool(config, &accountStore);
        std::vector<voice_agent::ITool*> tools{&shellTool, &pythonTool, &webBrowserTool};
        voice_agent::AgentToolOrchestrator agentOrchestrator(*interpreter, tools, config);
        const std::string systemPrompt = BuildSystemPrompt(
            config.systemPromptText,
            {shellTool.Definition(), pythonTool.Definition(), webBrowserTool.Definition()},
            config.maxAgentSteps
        );

        std::unique_ptr<voice_agent::IUserPromptProvider> promptProvider;
        std::unique_ptr<voice_agent::Agent> agent;
        if (config.agentMode == "text") {
            promptProvider = std::make_unique<voice_agent::StdinUserPromptProvider>();
            webBrowserTool.SetUserPromptProvider(promptProvider.get());
            agent = std::make_unique<voice_agent::TextAgent>(
                std::move(interpreter),
                systemPrompt,
                std::move(agentOrchestrator)
            );
        } else {
            auto transcriber = std::make_unique<voice_agent::AzureTranscriber>(config);
            auto synthesizer = std::make_unique<voice_agent::AzureRestSynthesizer>(config);

            voice_agent::VadConfig vadConfig;
            vadConfig.sampleRate = config.speechSampleRate;
            vadConfig.startSpeechMs = config.vadStartSpeechMs;
            vadConfig.endSilenceMs = config.vadEndSilenceMs;
            vadConfig.preRollMs = config.vadPreRollMs;
            vadConfig.playbackCooldownMs = config.vadPlaybackCooldownMs;
            vadConfig.aecStreamDelayMs = config.aecStreamDelayMs;

            auto microphone = std::make_unique<voice_agent::AlsaMicrophone>(config);
            auto speaker = std::make_unique<voice_agent::AlsaSpeaker>(
                config.speechSampleRate, config.alsaPlaybackDevice);
            auto voiceController = std::make_unique<voice_agent::VoiceController>(
                std::move(microphone), std::move(speaker), vadConfig);

            // Hook up the voice prompt provider before constructing VoiceAgent.
            // Raw pointers reference objects whose ownership transfers below;
            // VoiceAgent outlives the provider for the program's runtime, so
            // these references remain valid until shutdown.
            promptProvider = std::make_unique<voice_agent::VoiceUserPromptProvider>(
                voiceController.get(), synthesizer.get(), transcriber.get()
            );
            webBrowserTool.SetUserPromptProvider(promptProvider.get());

            agent = std::make_unique<voice_agent::VoiceAgent>(
                std::move(transcriber),
                std::move(interpreter),
                std::move(synthesizer),
                std::move(voiceController),
                systemPrompt,
                std::move(agentOrchestrator)
            );
        }

        agent->Run();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return 1;
    }
}