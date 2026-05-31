#pragma once

#include "tools/ITool.h"
#include "config/AppConfig.h"
#include "config/AccountStore.h"
#include "skills/SkillRegistry.h"

namespace voice_agent {

class IUserPromptProvider;

class WebBrowserTool final : public ITool {
public:
    WebBrowserTool(const AppConfig& config,
                   const AccountStore* accountStore = nullptr,
                   const SkillRegistry* skillRegistry = nullptr);

    void SetUserPromptProvider(IUserPromptProvider* provider) { promptProvider_ = provider; }

    const ToolDefinition& Definition() const override;
    ToolResult Execute(const ToolCall& call,
                       const CancellationToken* token = nullptr) const override;

private:
    ToolDefinition definition_;
    std::string runnerScriptPath_;
    const AccountStore* accountStore_ = nullptr;
    const SkillRegistry* skillRegistry_ = nullptr;
    IUserPromptProvider* promptProvider_ = nullptr;
    int promptTimeoutSeconds_ = 180;

    friend struct WebBrowserToolImpl;
};

}  // namespace voice_agent