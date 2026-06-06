#pragma once

#include "tools/ITool.h"
#include "registry/RegistryController.h"
#include <filesystem>

namespace voice_agent {

class RegistryTool final : public ITool {
public:
    RegistryTool(RegistryController& registryController, 
                 std::filesystem::path binaryDir);

    const ToolDefinition& Definition() const override;
    ToolResult Execute(const ToolCall& call, const CancellationToken* token = nullptr) const override;

private:
    ToolDefinition definition_;
    RegistryController& registryController_;
    std::filesystem::path binaryDir_;
    std::filesystem::path skillsDir_;
    std::filesystem::path notesDir_;
    std::filesystem::path experiencesDir_;

    ToolResult RecordExperience(const nlohmann::json& args) const;
    ToolResult RecordNote(const nlohmann::json& args) const;
    ToolResult RecordSkill(const nlohmann::json& args) const;
    ToolResult Query(const nlohmann::json& args) const;
};

}  // namespace voice_agent
