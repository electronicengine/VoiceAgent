#pragma once

#include "common/CancellationToken.h"

#include <string>

namespace voice_agent {

struct PromptOptions {
    int timeoutSeconds = 180;
    std::string mode;  // freeform: "text", "digits"; advisory only.
};

struct PromptResult {
    bool ok = false;
    bool cancelled = false;
    bool timedOut = false;
    std::string answer;
    std::string error;
};

class IUserPromptProvider {
public:
    virtual ~IUserPromptProvider() = default;

    // Ask the human user a question and block until they answer, the timeout
    // elapses, or the cancellation token fires. Implementations decide how
    // they collect the answer (voice, stdin, ...).
    virtual PromptResult Ask(const std::string& question,
                             const PromptOptions& options,
                             const CancellationToken* token) = 0;
};

}  // namespace voice_agent
