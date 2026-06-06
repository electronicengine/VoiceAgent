#pragma once

#include "config/AppConfig.h"
#include "config/AccountStore.h"
#include "tools/ITool.h"

#include <filesystem>

namespace voice_agent {

class IUserPromptProvider;

class PythonTool final : public ITool {
public:
    PythonTool(const AppConfig& config,
               const AccountStore* accountStore = nullptr);

    void SetUserPromptProvider(IUserPromptProvider* provider) { promptProvider_ = provider; }

    const ToolDefinition& Definition() const override;
    ToolResult Execute(const ToolCall& call,
                       const CancellationToken* token = nullptr) const override;

private:
    ToolDefinition definition_;
    std::filesystem::path scriptRoot_;
    const AccountStore* accountStore_ = nullptr;
    IUserPromptProvider* promptProvider_ = nullptr;
    int promptTimeoutSeconds_ = 180;
};

}  // namespace voice_agent