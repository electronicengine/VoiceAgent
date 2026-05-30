#pragma once

#include "common/CancellationToken.h"
#include "interpreter/InterpreterTypes.h"

#include <string>

namespace voice_agent {

class IInterpreter {
public:
    virtual ~IInterpreter() = default;
    virtual void ResetSession(std::string systemPrompt) = 0;
    virtual InterpreterResponse Interpret(
        const InterpreterInput& input,
        const InterpreterStreamCallback& onPartialResponse,
        const CancellationToken* token = nullptr) = 0;
};

}  // namespace voice_agent