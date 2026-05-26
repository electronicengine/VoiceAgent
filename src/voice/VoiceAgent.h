#pragma once

#include "interpreter/IInterpreter.h"
#include "synthesizer/ISynthesizer.h"
#include "transcriber/ITranscriber.h"
#include "agent/AgentOrchestrator.h"

#include <memory>
#include <string>

namespace voice_agent {

class VoiceAgent {
public:
    VoiceAgent(
        std::unique_ptr<ITranscriber> transcriber,
        std::unique_ptr<IInterpreter> interpreter,
        std::unique_ptr<ISynthesizer> synthesizer,
        std::string systemPrompt,
        AgentOrchestrator agentOrchestrator
    );

    void Run();

private:
    std::unique_ptr<ITranscriber> transcriber_;
    std::unique_ptr<IInterpreter> interpreter_;
    std::unique_ptr<ISynthesizer> synthesizer_;
    AgentOrchestrator agentOrchestrator_;
};

}  // namespace voice_agent