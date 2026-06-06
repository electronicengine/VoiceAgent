#include "tools/PythonTool.h"

#include "common/IUserPromptProvider.h"
#include "common/StringUtils.h"
#include "common/logger.h"

#include <algorithm>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace voice_agent {

namespace {

constexpr const char* kEventPrefix = "__VA_EVENT__";
constexpr std::size_t kOutputLimit = 64000;

struct CommandExecution {
    bool launched = false;
    int exitCode = -1;
    std::string output;
};

std::filesystem::path CanonicalBestEffort(const std::filesystem::path& path);
bool IsPathWithin(const std::filesystem::path& child, const std::filesystem::path& parent);

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

void LogPythonMessage(const std::string& stage, const std::string& message) {
    DEBUG("[PythonTool][{}] {}", stage, message);
    std::fflush(stdout);
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

std::optional<std::filesystem::path> ResolveScriptPath(
    const nlohmann::json& arguments,
    const std::filesystem::path& scriptRoot) {
    std::string configuredPath;
    if (arguments.contains("runner") && arguments.at("runner").is_string()) {
        configuredPath = Trim(arguments.at("runner").get<std::string>());
    }
    if (arguments.contains("script") && arguments.at("script").is_string()) {
        const std::string scriptValue = Trim(arguments.at("script").get<std::string>());
        if (!scriptValue.empty()) {
            configuredPath = scriptValue;
        }
    }
    if (configuredPath.empty() && arguments.contains("scriptPath") && arguments.at("scriptPath").is_string()) {
        configuredPath = Trim(arguments.at("scriptPath").get<std::string>());
    }
    if (configuredPath.empty()) {
        return std::nullopt;
    }

    const std::filesystem::path candidate(configuredPath);
    const std::filesystem::path resolved = candidate.is_absolute()
        ? CanonicalBestEffort(candidate)
        : CanonicalBestEffort(scriptRoot / candidate);
    if (!IsPathWithin(resolved, scriptRoot)) {
        return std::nullopt;
    }
    return resolved;
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

std::string ExtractLastJsonLine(const std::string& output) {
    std::istringstream stream(output);
    std::string line;
    std::string lastJsonLine;
    while (std::getline(stream, line)) {
        const std::string trimmed = Trim(line);
        if (!trimmed.empty() && (trimmed.front() == '{' || trimmed.front() == '[')) {
            lastJsonLine = trimmed;
        }
    }
    return lastJsonLine;
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

std::filesystem::path CanonicalBestEffort(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) {
        return canonical;
    }
    return std::filesystem::absolute(path).lexically_normal();
}

bool IsPathWithin(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const std::filesystem::path normalizedChild = CanonicalBestEffort(child);
    const std::filesystem::path normalizedParent = CanonicalBestEffort(parent);
    auto childIt = normalizedChild.begin();
    auto parentIt = normalizedParent.begin();
    for (; parentIt != normalizedParent.end(); ++parentIt, ++childIt) {
        if (childIt == normalizedChild.end() || *childIt != *parentIt) {
            return false;
        }
    }
    return true;
}


CommandExecution RunSimpleCommand(const std::string& command) {
    const std::string wrappedCommand = command + " 2>&1";
    std::array<char, 256> buffer{};
    std::string output;

    LogPythonMessage("command", "Komut baslatiliyor.");
    LogPythonMessage("command", "Command: " + command);

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(wrappedCommand.c_str(), "r"), pclose);
    if (!pipe) {
        return CommandExecution{false, -1, "Failed to start command."};
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        output += buffer.data();
        const std::string chunk = Trim(buffer.data());
        if (!chunk.empty()) {
            LogPythonMessage("command", chunk);
        }
        if (output.size() > kOutputLimit) {
            output.resize(kOutputLimit);
            output += "\n[truncated]";
            break;
        }
    }
    const int status = pclose(pipe.release());
    int exitCode = status;
    if (WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
    }
    LogPythonMessage("command", "Komut tamamlandi. exitCode=" + std::to_string(exitCode));
    return CommandExecution{true, exitCode, Trim(output)};
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
    const CommandExecution createEnvResult = RunSimpleCommand(createEnvCommand);
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

CommandExecution RunInteractiveCommand(
    const std::string& command,
    IUserPromptProvider* promptProvider,
    int promptDefaultTimeoutSeconds,
    const CancellationToken* token) {
    int inPipe[2] = {-1, -1};
    int outPipe[2] = {-1, -1};
    if (pipe(inPipe) != 0 || pipe(outPipe) != 0) {
        return CommandExecution{false, -1, std::string("pipe() failed: ") + std::strerror(errno)};
    }

    LogPythonMessage("run_script", "Komut baslatiliyor (interactive).");
    LogPythonMessage("run_script", "Command: " + command);

    pid_t pid = fork();
    if (pid < 0) {
        ::close(inPipe[0]);
        ::close(inPipe[1]);
        ::close(outPipe[0]);
        ::close(outPipe[1]);
        return CommandExecution{false, -1, std::string("fork() failed: ") + std::strerror(errno)};
    }
    if (pid == 0) {
        ::dup2(inPipe[0], STDIN_FILENO);
        ::dup2(outPipe[1], STDOUT_FILENO);
        ::dup2(outPipe[1], STDERR_FILENO);
        ::close(inPipe[0]);
        ::close(inPipe[1]);
        ::close(outPipe[0]);
        ::close(outPipe[1]);
        ::execlp("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }

    ::close(inPipe[0]);
    ::close(outPipe[1]);
    const int childStdin = inPipe[1];
    const int childStdout = outPipe[0];

    std::string output;
    std::string lineBuffer;
    std::array<char, 1024> readBuf{};
    bool truncated = false;
    bool terminationSent = false;

    auto appendOutput = [&](const std::string& line) {
        if (truncated) {
            return;
        }
        output += line;
        output += '\n';
        if (output.size() > kOutputLimit) {
            output.resize(kOutputLimit);
            output += "\n[truncated]";
            truncated = true;
        }
    };

    auto handleLine = [&](const std::string& line) {
        const std::string trimmed = Trim(line);
        if (trimmed.rfind(kEventPrefix, 0) == 0) {
            const std::string payload = Trim(trimmed.substr(std::strlen(kEventPrefix)));
            LogPythonMessage("run_script", "[event] " + payload);
            nlohmann::json event = nlohmann::json::parse(payload, nullptr, false);
            if (event.is_discarded() || !event.is_object()) {
                return;
            }
            if (event.value("type", "") == "prompt") {
                int timeoutSec = event.value("timeoutSeconds", promptDefaultTimeoutSeconds);
                if (timeoutSec <= 0) {
                    timeoutSec = promptDefaultTimeoutSeconds;
                }
                nlohmann::json response;
                if (promptProvider == nullptr) {
                    response = {{"ok", false}, {"error", "no prompt provider available"}};
                } else {
                    PromptOptions options;
                    options.timeoutSeconds = timeoutSec;
                    options.mode = event.value("mode", "text");
                    const PromptResult result = promptProvider->Ask(event.value("question", ""), options, token);
                    response = {
                        {"ok", result.ok},
                        {"cancelled", result.cancelled},
                        {"timedOut", result.timedOut},
                        {"answer", result.answer},
                        {"error", result.error},
                    };
                }
                const std::string responseLine = response.dump() + "\n";
                ssize_t written = 0;
                while (written < static_cast<ssize_t>(responseLine.size())) {
                    const ssize_t n = ::write(
                        childStdin,
                        responseLine.data() + written,
                        responseLine.size() - written
                    );
                    if (n <= 0) {
                        if (errno == EINTR) {
                            continue;
                        }
                        break;
                    }
                    written += n;
                }
                LogPythonMessage("run_script", "[event] response sent (ok=" + std::string(response.value("ok", false) ? "true" : "false") + ")");
            }
            return;
        }
        if (!trimmed.empty()) {
            LogPythonMessage("run_script", trimmed);
        }
        appendOutput(line);
    };

    while (true) {
        if (token != nullptr && token->IsCancelled() && !terminationSent) {
            ::kill(pid, SIGTERM);
            terminationSent = true;
        }
        const ssize_t n = ::read(childStdout, readBuf.data(), readBuf.size());
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        for (ssize_t i = 0; i < n; ++i) {
            const char ch = readBuf[i];
            if (ch == '\n') {
                handleLine(lineBuffer);
                lineBuffer.clear();
            } else if (ch != '\r') {
                lineBuffer.push_back(ch);
            }
        }
    }
    if (!lineBuffer.empty()) {
        handleLine(lineBuffer);
    }

    ::close(childStdin);
    ::close(childStdout);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            break;
        }
    }
    int exitCode = -1;
    if (WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exitCode = 128 + WTERMSIG(status);
    }
    LogPythonMessage("run_script", "Komut tamamlandi (interactive). exitCode=" + std::to_string(exitCode));
    return CommandExecution{true, exitCode, Trim(output)};
}

std::string BuildEnvPrefix(const nlohmann::json& arguments) {
    std::ostringstream prefix;
    if (!arguments.contains("env") || !arguments.at("env").is_object()) {
        return {};
    }
    for (auto it = arguments.at("env").begin(); it != arguments.at("env").end(); ++it) {
        if (!it.value().is_string()) {
            continue;
        }
        const std::string key = Trim(it.key());
        if (!key.empty()) {
            prefix << key << "=" << ShellEscape(it.value().get<std::string>()) << " ";
        }
    }
    return prefix.str();
}

}  // namespace

PythonTool::PythonTool(
    const AppConfig& config,
    const AccountStore* accountStore)
    : definition_({
        "PythonTool",
        "scripts klasoru altinda bulunan Python scriptlerini venv ortaminda calistirir, argumanlari iletir ve ciktilari dondurur.", {
            {"type", "object"},
            {"properties", {
                {"script", {{"type", "string"}, {"description", "scripts klasorune gore calistirilacak scriptin relative yolu."}}},
                {"packages", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Script calismadan once pip ile kurulmasi gereken paketlerin listesi."}}},
                {"pythonExecutable", {{"type", "string"}, {"description", "Temel Python executable komutu (varsayilan: python3)."}}},
                {"venvPath", {{"type", "string"}, {"description", "Kullanilacak venv yolu (istege bagli)."}}},
                {"env", {{"type", "object"}, {"description", "Script calisirken aktarilacak cevre degiskenleri."}}},
                {"args", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Script'e gecilecek arguman listesi."}}}
            }},
            {"required", { "script" }}
        },
        {"python.run"},
        ToolRiskLevel::Dangerous,
    }),
    scriptRoot_(config.resolvedPythonToolScriptRoot.empty()
        ? std::filesystem::absolute("scripts")
        : std::filesystem::path(config.resolvedPythonToolScriptRoot)),
    accountStore_(accountStore),
    promptTimeoutSeconds_(config.browserPromptTimeoutSeconds > 0 ? config.browserPromptTimeoutSeconds : 180) {}

const ToolDefinition& PythonTool::Definition() const {
    return definition_;
}

ToolResult PythonTool::Execute(const ToolCall& call, const CancellationToken* token) const {
    const std::string basePythonExecutable = Trim(call.arguments.value("pythonExecutable", "python3"));
    const std::filesystem::path venvPath = ResolveVirtualEnvPath(call.arguments);

    if (basePythonExecutable.empty()) {
        return ToolResult{false, false, "Python executable bilgisi bos olamaz.", {{"reason", "missing_python_executable"}}};
    }

    const ToolResult venvResult = CreateVirtualEnvIfNeeded(basePythonExecutable, venvPath);
    if (!venvResult.succeeded) {
        return venvResult;
    }
    const std::string pythonExecutable = ResolveVirtualEnvPython(venvPath);

    std::vector<std::string> packages = call.arguments.contains("packages")
        ? ParseStringList(call.arguments.at("packages"))
        : std::vector<std::string>{};
    if (!packages.empty()) {
        const std::string installCommand = ShellEscape(pythonExecutable) + " -m pip install " + JoinEscaped(packages);
        const CommandExecution installResult = RunSimpleCommand(installCommand);
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

    std::filesystem::path effectiveScriptPath;
    std::vector<std::string> args = call.arguments.contains("args")
        ? ParseStringList(call.arguments.at("args"))
        : std::vector<std::string>{};

    const std::optional<std::filesystem::path> resolvedScriptPath = ResolveScriptPath(call.arguments, scriptRoot_);
    if (!resolvedScriptPath.has_value()) {
        return ToolResult{
            false,
            true,
            "Python script yolu scripts allowlist'i icinde olmali.",
            {{"reason", "script_path_outside_allowlist"}, {"allowedRoot", scriptRoot_.string()}}
        };
    }
    effectiveScriptPath = *resolvedScriptPath;
    if (!std::filesystem::exists(effectiveScriptPath)) {
        return ToolResult{false, false, "Python script bulunamadi.", {{"reason", "missing_script"}, {"scriptPath", effectiveScriptPath.string()}}};
    }

    std::string commandPrefix = BuildEnvPrefix(call.arguments);
    std::string runCommand = commandPrefix + ShellEscape(pythonExecutable) + " -u " + ShellEscape(effectiveScriptPath.string());
    if (!args.empty()) {
        runCommand += " " + JoinEscaped(args);
    }

    const CommandExecution runResult = RunInteractiveCommand(
        runCommand,
        promptProvider_,
        promptTimeoutSeconds_,
        token
    );
    if (!runResult.launched) {
        return ToolResult{false, false, runResult.output, {{"stage", "run_script"}}};
    }

    const std::string jsonLine = ExtractLastJsonLine(runResult.output);
    const nlohmann::json payload = nlohmann::json::parse(jsonLine, nullptr, false);

    nlohmann::json output = {
        {"stage", "run_script"},
        {"exitCode", runResult.exitCode},
    };

    if (!payload.is_discarded()) {
        output["output"] = payload;
    } else {
        output["output"] = runResult.output;
    }

    // Include venv/script info but keep it minimal
    output["scriptPath"] = effectiveScriptPath.string();

    ToolResult result{
        runResult.exitCode == 0,
        false,
        runResult.exitCode == 0
            ? (!payload.is_discarded() && payload.is_object()
                ? payload.value("title", "Python scripti calistirildi.")
                : "Python scripti calistirildi.")
            : "Python scripti hata ile sonlandi.",
        output
    };

    if (!payload.is_discarded() && payload.is_object()) {
        if (payload.contains("finalScreenshot") && payload.at("finalScreenshot").is_string()) {
            const std::string screenshotPath = payload.at("finalScreenshot").get<std::string>();
            if (!screenshotPath.empty() && std::filesystem::exists(screenshotPath)) {
                result.imageAttachments.push_back({screenshotPath, "high"});
            }
        }
    }

    return result;
}

}  // namespace voice_agent
