#pragma once

#include "agent/Agent.h"
#include "synthesizer/ISynthesizer.h"
#include "transcriber/ITranscriber.h"

#include <memory>
#include <string>

namespace voice_agent {

class VoiceAgent : public Agent {
public:
    VoiceAgent(
        std::unique_ptr<ITranscriber> transcriber,
        std::unique_ptr<IInterpreter> interpreter,
        std::unique_ptr<ISynthesizer> synthesizer,
        std::string systemPrompt,
        AgentToolOrchestrator agentOrchestrator
    );

    void Run();

private:
    std::unique_ptr<ITranscriber> transcriber_;
    std::unique_ptr<ISynthesizer> synthesizer_;
};

}  // namespace voice_agent