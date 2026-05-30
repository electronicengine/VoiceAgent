#pragma once

#include "common/CancellationToken.h"
#include "tools/ToolTypes.h"

namespace voice_agent {

class ITool {
public:
    virtual ~ITool() = default;

    virtual const ToolDefinition& Definition() const = 0;
    virtual ToolResult Execute(const ToolCall& call,
                               const CancellationToken* token = nullptr) const = 0;
};

}  // namespace voice_agent