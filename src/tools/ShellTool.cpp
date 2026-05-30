#include "tools/ShellTool.h"

#include "common/StringUtils.h"

#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <sys/wait.h>

namespace voice_agent {

namespace {

struct CommandExecution {
    int exitCode = -1;
    std::string output;
};

CommandExecution ReadCommandOutput(const std::string& command) {
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

    const int status = pclose(pipe.release());
    int exitCode = status;
    if (WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
    }

    return CommandExecution{exitCode, Trim(output)};
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

ToolResult ShellTool::Execute(const ToolCall& call, const CancellationToken* /*token*/) const {
    std::string command = Trim(call.arguments.value("command", ""));
    if (command.empty()) {
        command = Trim(call.arguments.value("commmand", ""));
    }

    if (command.empty()) {
        return ToolResult{false, false, "Eksik shell komutu.", {{"reason", "missing_command"}}};
    }

    const CommandExecution execution = ReadCommandOutput(command);
    const bool succeeded = execution.exitCode == 0;
    const std::string reason = succeeded
        ? (execution.output.empty() ? "empty_output" : "ok")
        : "non_zero_exit";
    std::string summary;
    if (!succeeded) {
        summary = "Shell komutu hata ile sonlandi.";
    } else if (execution.output.empty()) {
        summary = "Komut calisti ancak cikti yok.";
    } else {
        summary = execution.output;
    }

    return ToolResult{
        succeeded,
        false,
        summary,
        {
            {"command", command},
            {"output", execution.output},
            {"exitCode", execution.exitCode},
            {"reason", reason}
        }
    };
}

}  // namespace voice_agent