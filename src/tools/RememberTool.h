#pragma once

#include "tools/ITool.h"

#include <string>

namespace voice_agent {

class RememberTool final : public ITool {
public:
    RememberTool(std::string experiencesFilePath, int maxLines);

    const ToolDefinition& Definition() const override;
    ToolResult Execute(const ToolCall& call,
                       const CancellationToken* token = nullptr) const override;

private:
    ToolDefinition definition_;
    std::string experiencesFilePath_;
    int maxLines_ = 100;
};

}  // namespace voice_agent
