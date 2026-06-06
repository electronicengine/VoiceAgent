#include "tools/ShellTool.h"

#include "common/StringUtils.h"

#include <array>
#include <filesystem>
#include <cstdio>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>

namespace voice_agent {

namespace {

struct CommandExecution {
    int exitCode = -1;
    std::string output;
};

bool IsContinuationByte(unsigned char byte) {
    return (byte & 0xC0) == 0x80;
}

std::size_t Utf8SequenceLength(unsigned char lead) {
    if ((lead & 0x80) == 0x00) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0) {
        return 4;
    }
    return 0;
}

std::string SanitizeUtf8(std::string_view input) {
    std::string sanitized;
    sanitized.reserve(input.size());
    for (std::size_t index = 0; index < input.size();) {
        const unsigned char lead = static_cast<unsigned char>(input[index]);
        const std::size_t length = Utf8SequenceLength(lead);
        if (length == 0 || index + length > input.size()) {
            sanitized.push_back('?');
            ++index;
            continue;
        }
        bool valid = true;
        for (std::size_t offset = 1; offset < length; ++offset) {
            if (!IsContinuationByte(static_cast<unsigned char>(input[index + offset]))) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            sanitized.push_back('?');
            ++index;
            continue;
        }
        sanitized.append(input.substr(index, length));
        index += length;
    }
    return sanitized;
}

std::size_t SafeUtf8PrefixLength(std::string_view input, std::size_t maxBytes) {
    if (input.size() <= maxBytes) {
        return input.size();
    }

    std::size_t index = 0;
    std::size_t safe = 0;
    while (index < input.size() && index < maxBytes) {
        const unsigned char lead = static_cast<unsigned char>(input[index]);
        const std::size_t length = Utf8SequenceLength(lead);
        if (length == 0 || index + length > input.size() || index + length > maxBytes) {
            break;
        }
        bool valid = true;
        for (std::size_t offset = 1; offset < length; ++offset) {
            if (!IsContinuationByte(static_cast<unsigned char>(input[index + offset]))) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            break;
        }
        safe = index + length;
        index += length;
    }
    return safe;
}

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
            output.resize(SafeUtf8PrefixLength(output, 4000));
            output += "\n[truncated]";
            break;
        }
    }

    const int status = pclose(pipe.release());
    int exitCode = status;
    if (WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
    }

    return CommandExecution{exitCode, Trim(SanitizeUtf8(output))};
}

}  // namespace

ShellTool::ShellTool(std::filesystem::path scriptsRoot)
    : definition_({
          "ShellTool",
          "Sistemde Linux shell komutu veya scripts klasorunun altindaki bir shell scriptini calistirir, sonuclarini dondurur.",
          {
              {"type", "object"},
              {"properties", {
                  {"command", {{"type", "string"}, {"description", "Calistirilacak tek satirlik Linux shell komutu."}}},
                  {"script", {{"type", "string"}, {"description", "scripts klasorune gore calistirilacak shell scriptinin relative yolu."}}},
                  {"args", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Script'e gecilecek arguman listesi."}}}
              }},
              {"anyOf", nlohmann::json::array({
                  nlohmann::json{{"required", nlohmann::json::array({"command"})}},
                  nlohmann::json{{"required", nlohmann::json::array({"script"})}}
              })}
          },
          {"computer.shell"},
          ToolRiskLevel::Dangerous,
      }),
      scriptsRoot_(scriptsRoot.empty() ? std::filesystem::absolute("scripts") : std::move(scriptsRoot)) {}

const ToolDefinition& ShellTool::Definition() const {
    return definition_;
}

ToolResult ShellTool::Execute(const ToolCall& call, const CancellationToken* /*token*/) const {
    std::string command = Trim(call.arguments.value("command", ""));
    if (command.empty()) {
        command = Trim(call.arguments.value("commmand", ""));
    }

    if (command.empty() && call.arguments.contains("script") && call.arguments.at("script").is_string()) {
        const std::string scriptReference = Trim(call.arguments.at("script").get<std::string>());
        if (!scriptReference.empty()) {
            const std::filesystem::path candidate(scriptReference);
            const std::filesystem::path resolved = candidate.is_absolute()
                ? CanonicalBestEffort(candidate)
                : CanonicalBestEffort(scriptsRoot_ / candidate);
            if (!std::filesystem::exists(resolved)) {
                return ToolResult{false, false, "Shell script bulunamadi.", {{"reason", "missing_script"}, {"scriptPath", resolved.string()}, {"scriptReference", scriptReference}}};
            }
            if (!IsPathWithin(resolved, scriptsRoot_)) {
                return ToolResult{false, true, "Shell script allowlist disinda oldugu icin calistirilmadi.", {{"reason", "script_path_outside_allowlist"}, {"scriptPath", resolved.string()}, {"allowedRoot", scriptsRoot_.string()}, {"scriptReference", scriptReference}}};
            }
            std::vector<std::string> args = call.arguments.contains("args")
                ? ParseStringList(call.arguments.at("args"))
                : std::vector<std::string>{};
            command = "bash " + ShellEscape(resolved.string());
            if (!args.empty()) {
                command += " " + JoinEscaped(args);
            }
        }
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