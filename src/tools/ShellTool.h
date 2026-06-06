#pragma once

#include "tools/ITool.h"

#include <filesystem>

namespace voice_agent {

class ShellTool final : public ITool {
public:
    explicit ShellTool(std::filesystem::path scriptsRoot = {});

    const ToolDefinition& Definition() const override;
    ToolResult Execute(const ToolCall& call,
                       const CancellationToken* token = nullptr) const override;

private:
    ToolDefinition definition_;
    std::filesystem::path scriptsRoot_;
};

}  // namespace voice_agent