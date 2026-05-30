#pragma once

#include "common/IUserPromptProvider.h"

namespace voice_agent {

// Reads the answer line from std::cin. Suitable for the text agent and as a
// fallback when no voice subsystem is available. Timeout is best-effort: stdin
// reads block, so a long-running deadline will simply continue waiting until
// the user types something.
class StdinUserPromptProvider final : public IUserPromptProvider {
public:
    PromptResult Ask(const std::string& question,
                     const PromptOptions& options,
                     const CancellationToken* token) override;
};

}  // namespace voice_agent
