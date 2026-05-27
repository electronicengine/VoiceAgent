#pragma once

#include "tools/ITool.h"

namespace voice_agent {

class WebBrowserTool final : public ITool {
public:
    WebBrowserTool();

    const ToolDefinition& Definition() const override;
    ToolResult Execute(const ToolCall& call) const override;

private:
    ToolDefinition definition_;
};

}  // namespace voice_agent