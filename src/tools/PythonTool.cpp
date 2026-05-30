#include "tools/PythonTool.h"

#include "common/StringUtils.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>
#include <vector>
#include <iostream>


namespace voice_agent {

namespace {

struct CommandExecution {
    bool launched = false;
    int exitCode = -1;
    std::string output;
};

std::string ShellEscape(const std::string& value) {
    std::string escaped = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            escaped += "'\\''";
            continue;
        }

        escaped += ch;
    }
    escaped += "'";
    return escaped;
}

CommandExecution RunCommand(const std::string& command) {
    const std::string wrappedCommand = command + " 2>&1";
    std::array<char, 256> buffer{};
    std::string output;

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(wrappedCommand.c_str(), "r"), pclose);
    if (!pipe) {
        return CommandExecution{false, -1, "Failed to start command."};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        output += buffer.data();
        if (output.size() > 12000) {
            output.resize(12000);
            output += "\n[truncated]";
            break;
        }
    }

    const int status = pclose(pipe.release());
    int exitCode = status;
    if (WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
    }

    return CommandExecution{true, exitCode, Trim(output)};
}

std::vector<std::string> ParseStringList(const nlohmann::json& value) {
    std::vector<std::string> result;
    if (value.is_string()) {
        const std::string item = Trim(value.get<std::string>());
        if (!item.empty()) {
            result.push_back(item);
        }
        return result;
    }

    if (!value.is_array()) {
        return result;
    }

    for (const auto& entry : value) {
        if (!entry.is_string()) {
            continue;
        }

        const std::string item = Trim(entry.get<std::string>());
        if (!item.empty()) {
            result.push_back(item);
        }
    }

    return result;
}

std::string JoinEscaped(const std::vector<std::string>& values) {
    std::ostringstream builder;
    bool first = true;
    for (const auto& value : values) {
        if (!first) {
            builder << ' ';
        }
        builder << ShellEscape(value);
        first = false;
    }
    return builder.str();
}

std::filesystem::path ResolveVirtualEnvPath(const nlohmann::json& arguments) {
    if (arguments.contains("venvPath") && arguments.at("venvPath").is_string()) {
        const std::string configuredPath = Trim(arguments.at("venvPath").get<std::string>());
        if (!configuredPath.empty()) {
            return std::filesystem::path(configuredPath);
        }
    }

    return std::filesystem::current_path() / ".voice_agent_python_env";
}

std::string ResolveVirtualEnvPython(const std::filesystem::path& venvPath) {
    return (venvPath / "bin" / "python").string();
}

ToolResult CreateVirtualEnvIfNeeded(
    const std::string& basePythonExecutable,
    const std::filesystem::path& venvPath) {
    const std::string venvPython = ResolveVirtualEnvPython(venvPath);
    if (std::filesystem::exists(venvPython)) {
        return ToolResult{true, false, "Python environment hazir.", {{"venvPath", venvPath.string()}, {"pythonExecutable", venvPython}}};
    }

    std::error_code error;
    std::filesystem::create_directories(venvPath.parent_path(), error);
    if (error) {
        return ToolResult{
            false,
            false,
            "Python environment klasoru olusturulamadi.",
            {{"reason", "create_venv_parent_failed"}, {"details", error.message()}, {"venvPath", venvPath.string()}}
        };
    }

    const std::string createEnvCommand = ShellEscape(basePythonExecutable) + " -m venv " + ShellEscape(venvPath.string());
    const CommandExecution createEnvResult = RunCommand(createEnvCommand);
    if (!createEnvResult.launched) {
        return ToolResult{false, false, createEnvResult.output, {{"stage", "create_virtualenv"}, {"command", createEnvCommand}}};
    }
    if (createEnvResult.exitCode != 0) {
        return ToolResult{
            false,
            false,
            "Python environment olusturulamadi.",
            {
                {"stage", "create_virtualenv"},
                {"command", createEnvCommand},
                {"exitCode", createEnvResult.exitCode},
                {"output", createEnvResult.output},
                {"venvPath", venvPath.string()}
            }
        };
    }

    return ToolResult{true, false, "Python environment olusturuldu.", {{"venvPath", venvPath.string()}, {"pythonExecutable", venvPython}}};
}

class TemporaryScriptFile {
public:
    explicit TemporaryScriptFile(const std::string& code) {
        char fileTemplate[] = "/tmp/voice_agent_python_XXXXXX.py";
        const int fd = mkstemps(fileTemplate, 3);
        if (fd == -1) {
            throw std::runtime_error("Temporary Python script file could not be created.");
        }

        path_ = fileTemplate;

        std::ofstream stream(path_);
        if (!stream) {
            close(fd);
            std::filesystem::remove(path_);
            throw std::runtime_error("Temporary Python script file could not be opened.");
        }

        stream << code;
        stream.close();
        close(fd);
    }

    ~TemporaryScriptFile() {
        if (!path_.empty()) {
            std::error_code ignoredError;
            std::filesystem::remove(path_, ignoredError);
        }
    }

    const std::string& Path() const {
        return path_;
    }

private:
    std::string path_;
};

}  // namespace

PythonTool::PythonTool()
    : definition_({
          "PythonTool",
          "Izole bir Python environment olusturur, pip paketlerini oraya yukler ve verilen Python kodunu ya da script dosyasini calistirir.",
          {
              {"type", "object"},
              {"properties", {
                  {"code", {{"type", "string"}}},
                  {"scriptPath", {{"type", "string"}}},
                  {"packages", {
                      {"type", "array"},
                      {"items", {{"type", "string"}}}
                  }},
                  {"pythonExecutable", {{"type", "string"}}},
                  {"venvPath", {{"type", "string"}}},
                  {"args", {
                      {"type", "array"},
                      {"items", {{"type", "string"}}}
                  }}
              }}
          },
          {"python.exec", "python.run"},
          ToolRiskLevel::Dangerous,
      }) {}

const ToolDefinition& PythonTool::Definition() const {
    return definition_;
}

ToolResult PythonTool::Execute(const ToolCall& call, const CancellationToken* /*token*/) const {
    const std::string basePythonExecutable = Trim(call.arguments.value("pythonExecutable", "python3"));
    const std::string code = call.arguments.contains("code") && call.arguments.at("code").is_string()
        ? call.arguments.at("code").get<std::string>()
        : std::string();
    const std::string scriptPath = Trim(call.arguments.value("scriptPath", ""));
    const std::vector<std::string> packages = call.arguments.contains("packages")
        ? ParseStringList(call.arguments.at("packages"))
        : std::vector<std::string>{};
    const std::vector<std::string> args = call.arguments.contains("args")
        ? ParseStringList(call.arguments.at("args"))
        : std::vector<std::string>{};

    if (basePythonExecutable.empty()) {
        return ToolResult{false, false, "Python executable bilgisi bos olamaz.", {{"reason", "missing_python_executable"}}};
    }

    if (code.empty() && scriptPath.empty()) {
        return ToolResult{false, false, "Calistirilacak Python kodu ya da script yolu gerekli.", {{"reason", "missing_code_or_script"}}};
    }

    const std::filesystem::path venvPath = ResolveVirtualEnvPath(call.arguments);
    const ToolResult venvResult = CreateVirtualEnvIfNeeded(basePythonExecutable, venvPath);
    if (!venvResult.succeeded) {
        return venvResult;
    }

    const std::string pythonExecutable = ResolveVirtualEnvPython(venvPath);

    if (!packages.empty()) {
        const std::string installCommand = ShellEscape(pythonExecutable) + " -m pip install " + JoinEscaped(packages);
        const CommandExecution installResult = RunCommand(installCommand);
        if (!installResult.launched) {
            return ToolResult{false, false, installResult.output, {{"stage", "install_packages"}}};
        }
        if (installResult.exitCode != 0) {
            return ToolResult{
                false,
                false,
                "Python paketleri yuklenemedi.",
                {
                    {"stage", "install_packages"},
                    {"command", installCommand},
                    {"exitCode", installResult.exitCode},
                    {"output", installResult.output},
                    {"venvPath", venvPath.string()}
                }
            };
        }
    }

    std::unique_ptr<TemporaryScriptFile> temporaryScript;
    std::string effectiveScriptPath = scriptPath;
    if (!code.empty()) {
        temporaryScript = std::make_unique<TemporaryScriptFile>(code);
        effectiveScriptPath = temporaryScript->Path();
    }

    if (effectiveScriptPath.empty()) {
        return ToolResult{false, false, "Python script yolu cozumlenemedi.", {{"reason", "missing_effective_script_path"}}};
    }

    std::string runCommand = ShellEscape(pythonExecutable) + " " + ShellEscape(effectiveScriptPath);
    if (!args.empty()) {
        runCommand += " " + JoinEscaped(args);
    }

    const CommandExecution runResult = RunCommand(runCommand);
    if (!runResult.launched) {
        return ToolResult{false, false, runResult.output, {{"stage", "run_script"}}};
    }

    std::cout << "Python script output:\n" << runResult.output << std::endl;
    return ToolResult{
        runResult.exitCode == 0,
        false,
        runResult.exitCode == 0 ? "Python kodu calistirildi." : "Python kodu hata ile sonlandi.",
        {
            {"stage", "run_script"},
            {"command", runCommand},
            {"exitCode", runResult.exitCode},
            {"output", runResult.output},
            {"venvPath", venvPath.string()},
            {"pythonExecutable", pythonExecutable}
        }
    };
}

}  // namespace voice_agent