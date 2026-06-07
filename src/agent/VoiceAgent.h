#pragma once

#include "agent/Agent.h"
#include "audio/VoiceController.h"
#include "common/CancellationToken.h"
#include "synthesizer/ISynthesizer.h"
#include "transcriber/ITranscriber.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace voice_agent {

class VoiceAgent : public Agent {
public:
    VoiceAgent(
        std::unique_ptr<ITranscriber> transcriber,
        std::unique_ptr<IInterpreter> interpreter,
        std::unique_ptr<ISynthesizer> synthesizer,
        std::unique_ptr<VoiceController> voiceController,
        std::string systemPrompt,
        AgentOrchestrator agentOrchestrator
    );
    ~VoiceAgent() override;

    void Run() override;

private:
    struct Turn {
        CancellationTokenPtr token;
        std::thread thread;
    };

    void HandleUtterance(std::vector<char> wavBytes);
    void HandleBargeIn();
    void CancelCurrentTurn();
    void JoinFinishedTurn();
    void RunTurnThread(std::vector<char> wavBytes, CancellationTokenPtr token);

    std::unique_ptr<ITranscriber> transcriber_;
    std::unique_ptr<ISynthesizer> synthesizer_;
    std::unique_ptr<VoiceController> voiceController_;

    std::mutex turnMutex_;
    std::unique_ptr<Turn> currentTurn_;
    std::unique_ptr<Turn> finishedTurn_;

    std::mutex exitMutex_;
    std::condition_variable exitCv_;
    std::atomic<bool> shouldExit_{false};
};

}  // namespace voice_agent
