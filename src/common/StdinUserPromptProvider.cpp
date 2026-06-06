#include "common/StdinUserPromptProvider.h"

#include "common/StringUtils.h"
#include "common/logger.h"

#include <iostream>
#include <string>
#include <cstdio>

namespace voice_agent {

PromptResult StdinUserPromptProvider::Ask(
    const std::string& question,
    const PromptOptions& /*options*/,
    const CancellationToken* token
) {
    PromptResult result;
    if (token != nullptr && token->IsCancelled()) {
        result.cancelled = true;
        return result;
    }

    INFO("\n[BrowserPrompt] {}\nYanit: ", question);
    std::fflush(stdout);

    std::string line;
    if (!std::getline(std::cin, line)) {
        result.error = "stdin closed";
        return result;
    }

    const std::string trimmed = Trim(line);
    if (trimmed.empty()) {
        result.error = "empty answer";
        return result;
    }

    result.ok = true;
    result.answer = trimmed;
    return result;
}

}  // namespace voice_agent
