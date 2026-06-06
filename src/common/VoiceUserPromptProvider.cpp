#include "common/VoiceUserPromptProvider.h"

#include "audio/VoiceController.h"
#include "common/StringUtils.h"
#include "interpreter/InterpreterTypes.h"
#include "synthesizer/ISynthesizer.h"
#include "transcriber/ITranscriber.h"

#include <iostream>
#include <utility>

namespace voice_agent {

VoiceUserPromptProvider::VoiceUserPromptProvider(
    VoiceController* voiceController,
    ISynthesizer* synthesizer,
    ITranscriber* transcriber
)
    : voiceController_(voiceController),
      synthesizer_(synthesizer),
      transcriber_(transcriber) {}

PromptResult VoiceUserPromptProvider::Ask(
    const std::string& question,
    const PromptOptions& options,
    const CancellationToken* token
) {
    PromptResult result;
    if (voiceController_ == nullptr || synthesizer_ == nullptr || transcriber_ == nullptr) {
        result.error = "voice prompt provider missing dependencies";
        return result;
    }
    if (token != nullptr && token->IsCancelled()) {
        result.cancelled = true;
        return result;
    }

    std::cout << "[BrowserPrompt][prompt] " << question << "\n" << std::flush;

    InterpreterResponse response;
    response.segments.push_back({ResponseSegmentType::Speech, question, true});
    std::string pcm = synthesizer_->Synthesize(response, token);
    if (token != nullptr && token->IsCancelled()) {
        result.cancelled = true;
        return result;
    }
    if (!pcm.empty()) {
        voiceController_->Speak(std::move(pcm));
        voiceController_->WaitUntilSpeakerIdle();
    }
    if (token != nullptr && token->IsCancelled()) {
        result.cancelled = true;
        return result;
    }

    const int timeoutMs = std::max(options.timeoutSeconds, 1) * 1000;
    std::vector<char> wavBytes;
    const bool captured = voiceController_->CaptureNextUtterance(timeoutMs, wavBytes, token);
    if (token != nullptr && token->IsCancelled()) {
        result.cancelled = true;
        return result;
    }
    if (!captured || wavBytes.empty()) {
        result.timedOut = true;
        result.error = "no speech detected before timeout";
        return result;
    }

    const std::string transcript = Trim(transcriber_->Transcribe(wavBytes, token));
    if (transcript.empty()) {
        result.error = "transcription empty";
        return result;
    }

    std::cout << "[BrowserPrompt][prompt-answer] " << transcript << "\n" << std::flush;
    result.ok = true;
    result.answer = transcript;
    return result;
}

}  // namespace voice_agent
