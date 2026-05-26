#pragma once

#include "tools/ToolTypes.h"

namespace voice_agent {

class ITool {
public:
    virtual ~ITool() = default;

    virtual const ToolDefinition& Definition() const = 0;
    virtual ToolResult Execute(const ToolCall& call) const = 0;
};

}  // namespace voice_agent