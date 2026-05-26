#include "tools/ShellTool.h"

#include "common/StringUtils.h"

#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace voice_agent {

namespace {

std::string ReadCommandOutput(const std::string& command) {
    const std::string wrappedCommand = command + " 2>&1";
    std::array<char, 256> buffer{};
    std::string output;

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(wrappedCommand.c_str(), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("Failed to start shell command.");
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        output += buffer.data();
        if (output.size() > 4000) {
            output.resize(4000);
            output += "\n[truncated]";
            break;
        }
    }

    return Trim(output);
}

}  // namespace

ShellTool::ShellTool()
    : definition_({
          "ShellTool",
          "Linux shell komutu calistirir. Yikici komutlar policy tarafinda bloklanir.",
          {
              {"type", "object"},
              {"properties", {
                  {"command", {{"type", "string"}}},
                  {"commmand", {{"type", "string"}}}
              }},
              {"required", {"command"}}
          },
          {"computer.shell"},
          ToolRiskLevel::Dangerous,
      }) {}

const ToolDefinition& ShellTool::Definition() const {
    return definition_;
}

ToolResult ShellTool::Execute(const ToolCall& call) const {
    std::string command = Trim(call.arguments.value("command", ""));
    if (command.empty()) {
        command = Trim(call.arguments.value("commmand", ""));
    }

    if (command.empty()) {
        return ToolResult{false, false, "Eksik shell komutu.", {{"reason", "missing_command"}}};
    }

    const std::string output = ReadCommandOutput(command);
    return ToolResult{
        true,
        false,
        output.empty() ? "Komut calisti ancak cikti yok." : output,
        {{"command", command}, {"output", output}}
    };
}

}  // namespace voice_agent