#pragma once

#include "tools/ITool.h"

namespace voice_agent {

class PythonTool final : public ITool {
public:
    PythonTool();

    const ToolDefinition& Definition() const override;
    ToolResult Execute(const ToolCall& call) const override;

private:
    ToolDefinition definition_;
};

}  // namespace voice_agent