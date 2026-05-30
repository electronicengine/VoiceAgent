#pragma once

#include "common/CancellationToken.h"
#include "interpreter/InterpreterTypes.h"

#include <string>

namespace voice_agent {

class ISynthesizer {
public:
    virtual ~ISynthesizer() = default;

    // Returns synthesized PCM audio (mono S16LE @ configured speech sample rate)
    // for the speakable portion of `response`. Returns empty if cancelled or
    // there is nothing speakable.
    virtual std::string Synthesize(const InterpreterResponse& response,
                                   const CancellationToken* token = nullptr) const = 0;
};

}  // namespace voice_agent