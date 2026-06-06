#include "agent/VoiceAgent.h"

#include "common/StringUtils.h"
#include "common/logger.h"

#include <chrono>
#include <stdexcept>
#include <utility>
#include <cstdio>

namespace voice_agent {

VoiceAgent::VoiceAgent(
    std::unique_ptr<ITranscriber> transcriber,
    std::unique_ptr<IInterpreter> interpreter,
    std::unique_ptr<ISynthesizer> synthesizer,
    std::unique_ptr<VoiceController> voiceController,
    std::string systemPrompt,
    AgentToolOrchestrator agentOrchestrator)
    : Agent(std::move(interpreter), std::move(systemPrompt), std::move(agentOrchestrator)),
      transcriber_(std::move(transcriber)),
      synthesizer_(std::move(synthesizer)),
      voiceController_(std::move(voiceController)) {}

VoiceAgent::~VoiceAgent() {
    {
        std::lock_guard<std::mutex> lock(turnMutex_);
        if (currentTurn_) {
            currentTurn_->token->Cancel();
        }
    }
    if (voiceController_) {
        voiceController_->StopSpeaking();
        voiceController_->Stop();
    }
    {
        std::unique_ptr<Turn> turn;
        {
            std::lock_guard<std::mutex> lock(turnMutex_);
            turn = std::move(currentTurn_);
        }
        if (turn && turn->thread.joinable()) {
            turn->thread.join();
        }
    }
    JoinFinishedTurn();
}

void VoiceAgent::Run() {
    INFO("Voice agent is ready. Speak into your microphone.");
    INFO("Press Ctrl+C to stop.\n");

    voiceController_->SetOnUtterance(
        [this](std::vector<char> wavBytes) { HandleUtterance(std::move(wavBytes)); });
    voiceController_->SetOnBargeIn([this]() { HandleBargeIn(); });
    voiceController_->Start();

    std::unique_lock<std::mutex> lock(exitMutex_);
    exitCv_.wait(lock, [this]() { return shouldExit_.load(); });

    voiceController_->Stop();
}

void VoiceAgent::HandleUtterance(std::vector<char> wavBytes) {
    {
        std::lock_guard<std::mutex> lock(turnMutex_);
        if (currentTurn_) {
            INFO("[VoiceAgent] new utterance arrived; cancelling previous turn");
        }
    }
    CancelCurrentTurn();
    JoinFinishedTurn();

    auto turn = std::make_unique<Turn>();
    turn->token = std::make_shared<CancellationToken>();

    auto token = turn->token;
    auto wav = std::move(wavBytes);
    turn->thread = std::thread([this, wav = std::move(wav), token]() mutable {
        RunTurnThread(std::move(wav), token);
    });

    std::lock_guard<std::mutex> lock(turnMutex_);
    currentTurn_ = std::move(turn);
}

void VoiceAgent::HandleBargeIn() {
    INFO("[VoiceAgent] HandleBargeIn -> cancelling current turn");
    CancelCurrentTurn();
}

void VoiceAgent::CancelCurrentTurn() {
    std::unique_ptr<Turn> turn;
    {
        std::lock_guard<std::mutex> lock(turnMutex_);
        if (!currentTurn_) {
            return;
        }
        currentTurn_->token->Cancel();
        turn = std::move(currentTurn_);
    }
    if (voiceController_) {
        voiceController_->StopSpeaking();
        voiceController_->SetBusy(false);
    }
    {
        std::lock_guard<std::mutex> lock(turnMutex_);
        if (finishedTurn_ && finishedTurn_->thread.joinable()) {
            finishedTurn_->thread.join();
        }
        finishedTurn_ = std::move(turn);
    }
}

void VoiceAgent::JoinFinishedTurn() {
    std::unique_ptr<Turn> turn;
    {
        std::lock_guard<std::mutex> lock(turnMutex_);
        turn = std::move(finishedTurn_);
    }
    if (turn && turn->thread.joinable()) {
        turn->thread.join();
    }
}

void VoiceAgent::RunTurnThread(std::vector<char> wavBytes, CancellationTokenPtr token) {
    try {
        if (token->IsCancelled()) {
            return;
        }

        voiceController_->SetBusy(true);

        const std::string userText = Trim(transcriber_->Transcribe(wavBytes, token.get()));
        if (token->IsCancelled() || userText.empty()) {
            voiceController_->SetBusy(false);
            return;
        }

        INFO("You: {}", userText);
        LOG_RAW("Agent: ");

        const AgentTurnResult turnResult = RunTurn(
            userText,
            [this, &token](const InterpreterResponse& partialResponse) {
                if (token->IsCancelled()) {
                    return;
                }
                const std::string streamedText = partialResponse.SpeakableText();
                if (streamedText.empty()) {
                    return;
                }
                LOG_RAW("{} ", streamedText);
                std::string pcm = synthesizer_->Synthesize(partialResponse, token.get());
                if (!pcm.empty() && !token->IsCancelled()) {
                    voiceController_->Speak(std::move(pcm));
                }
            },
            token.get(),
            [this, &token](const std::string& announcement) {
                if (token->IsCancelled()) {
                    return;
                }
                INFO("[announcement] {}", announcement);
                InterpreterResponse speakResponse;
                speakResponse.segments.push_back(
                    ResponseSegment{ResponseSegmentType::Speech, announcement, true});
                std::string pcm = synthesizer_->Synthesize(speakResponse, token.get());
                if (!pcm.empty() && !token->IsCancelled()) {
                    voiceController_->Speak(std::move(pcm));
                }
            });

        LOG_NL();

        if (!token->IsCancelled()) {
            voiceController_->WaitUntilSpeakerIdle();
        }
        voiceController_->SetBusy(false);
    } catch (const std::exception& ex) {
        if (!token->IsCancelled()) {
            ERROR("Voice agent turn failed: {}", ex.what());
        }
        voiceController_->SetBusy(false);
    }
}

}  // namespace voice_agent
