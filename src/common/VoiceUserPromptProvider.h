#pragma once

#include "common/IUserPromptProvider.h"

namespace voice_agent {

class ITranscriber;
class ISynthesizer;
class VoiceController;

// Speaks the question through the synthesizer + speaker, then captures the
// user's spoken reply via the VoiceController and transcribes it. While
// listening for the answer the regular agent-loop callbacks (utterance and
// barge-in) are temporarily suppressed so the running tool turn is not killed.
class VoiceUserPromptProvider final : public IUserPromptProvider {
public:
    VoiceUserPromptProvider(VoiceController* voiceController,
                            ISynthesizer* synthesizer,
                            ITranscriber* transcriber);

    PromptResult Ask(const std::string& question,
                     const PromptOptions& options,
                     const CancellationToken* token) override;

private:
    VoiceController* voiceController_;
    ISynthesizer* synthesizer_;
    ITranscriber* transcriber_;
};

}  // namespace voice_agent
