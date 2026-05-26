#pragma once

#include "interpreter/InterpreterTypes.h"

namespace voice_agent {

class ISynthesizer {
public:
    virtual ~ISynthesizer() = default;
    virtual void Synthesize(const InterpreterResponse& response) const = 0;
};

}  // namespace voice_agent