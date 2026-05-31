#include "tools/WebBrowserTool.h"

#include "common/IUserPromptProvider.h"
#include "common/StringUtils.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>
#include <vector>
#include <nlohmann/json.hpp>

namespace voice_agent {

namespace {

struct CommandExecution {
    bool launched = false;
    int exitCode = -1;
    std::string output;
};

struct AccountLoginConfig {
    std::string loginUrl;
    std::string loggedInUrl;
    std::string loginCheckSelector;
};

std::filesystem::path ResolveRepoRootFromRunner(const std::string& runnerPath) {
    std::filesystem::path runner = std::filesystem::absolute(std::filesystem::path(runnerPath));
    return runner.parent_path().parent_path().parent_path();
}

nlohmann::json BuildAccountRecoveryOutput(const nlohmann::json& payload,
                                         const std::string& accountId,
                                         const std::string& runCommand,
                                         int exitCode) {
    nlohmann::json out = {
        {"stage", "run_browser_script"},
        {"command", runCommand},
        {"exitCode", exitCode},
        {"output", payload},
        {"accountId", accountId},
        {"reason", "account_login_required"},
        {"recoveryCommand",
         std::string("cd /home/kufi/workspace/voiceAgent && KEEP_NOVNC=1 bash scripts/account-login.sh ") + accountId}
    };

    if (payload.contains("loginStatus") && payload.at("loginStatus").is_object()) {
        out["loginStatus"] = payload.at("loginStatus");
        const auto& loginStatus = payload.at("loginStatus");
        if (loginStatus.contains("reason") && loginStatus.at("reason").is_string()) {
            out["reason"] = loginStatus.at("reason").get<std::string>();
        }
    }

    out["recoveryHint"] =
        "Bu hesap icin once recoveryCommand'i ShellTool ile calistir; kullanici manuel girisi tamamlayinca ayni WebBrowserTool cagrisini tekrar dene.";
    return out;
}

void LogBrowserMessage(const std::string& stage, const std::string& message) {
    std::cout << "[WebBrowserTool][" << stage << "] " << message << "\n";
    std::cout.flush();
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

nlohmann::json BuildSessionRecoveryOutput(const nlohmann::json& payload,
                                         const std::string& sessionId,
                                         const std::string& displayName,
                                         const std::string& loginUrl,
                                         const std::string& loggedInUrl,
                                         const std::string& loginCheckSelector,
                                         const std::filesystem::path& repoRoot,
                                         const std::string& runCommand,
                                         int exitCode) {
    std::string recoveryCommand =
        "cd " + ShellEscape(repoRoot.string()) +
        " && KEEP_NOVNC=1 bash scripts/site-login.sh" +
        " --session-id " + ShellEscape(sessionId) +
        " --display-name " + ShellEscape(displayName.empty() ? sessionId : displayName) +
        " --login-url " + ShellEscape(loginUrl) +
        " --logged-in-url " + ShellEscape(loggedInUrl.empty() ? loginUrl : loggedInUrl);
    if (!loginCheckSelector.empty()) {
        recoveryCommand += " --login-check-selector " + ShellEscape(loginCheckSelector);
    }

    nlohmann::json out = {
        {"stage", "run_browser_script"},
        {"command", runCommand},
        {"exitCode", exitCode},
        {"output", payload},
        {"sessionId", sessionId},
        {"displayName", displayName},
        {"loginUrl", loginUrl},
        {"loggedInUrl", loggedInUrl},
        {"loginCheckSelector", loginCheckSelector},
        {"reason", "session_login_required"},
        {"recoveryCommand", recoveryCommand}
    };

    if (payload.contains("loginStatus") && payload.at("loginStatus").is_object()) {
        out["loginStatus"] = payload.at("loginStatus");
        const auto& loginStatus = payload.at("loginStatus");
        if (loginStatus.contains("reason") && loginStatus.at("reason").is_string()) {
            out["reason"] = loginStatus.at("reason").get<std::string>();
        }
    }

    out["recoveryHint"] =
        "Bu site icin once recoveryCommand'i ShellTool ile calistir; kullanici manuel girisi tamamlayinca ayni WebBrowserTool cagrisini tekrar dene.";
    return out;
}

AccountLoginConfig ResolveAccountLoginConfig(const AccountRecord& record,
                                             const SkillRegistry* skillRegistry) {
    AccountLoginConfig resolved{
        record.loginUrl,
        record.loggedInUrl,
        record.loginCheckSelector,
    };
    if (skillRegistry == nullptr) {
        if (resolved.loggedInUrl.empty()) {
            resolved.loggedInUrl = resolved.loginUrl;
        }
        return resolved;
    }

    const SkillAccountConfig* skillAccount = skillRegistry->FindAccountConfig(record.id);
    if (skillAccount != nullptr) {
        if (!skillAccount->loginUrl.empty()) {
            resolved.loginUrl = skillAccount->loginUrl;
        }
        if (!skillAccount->loggedInUrl.empty()) {
            resolved.loggedInUrl = skillAccount->loggedInUrl;
        }
        if (!skillAccount->loginCheckSelector.empty()) {
            resolved.loginCheckSelector = skillAccount->loginCheckSelector;
        }
    }
    if (resolved.loggedInUrl.empty()) {
        resolved.loggedInUrl = resolved.loginUrl;
    }
    return resolved;
}

CommandExecution RunCommand(const std::string& command, const std::string& stage) {
    const std::string wrappedCommand = command + " 2>&1";
    std::array<char, 256> buffer{};
    std::string output;

    LogBrowserMessage(stage, "Komut baslatiliyor.");
    LogBrowserMessage(stage, "Command: " + command);

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(wrappedCommand.c_str(), "r"), pclose);
    if (!pipe) {
        return CommandExecution{false, -1, "Failed to start command."};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        output += buffer.data();
        std::string chunk = buffer.data();
        if (!Trim(chunk).empty()) {
            LogBrowserMessage(stage, Trim(chunk));
        }
        if (output.size() > 32000) {
            output.resize(32000);
            output += "\n[truncated]";
            break;
        }
    }

    const int status = pclose(pipe.release());
    int exitCode = status;
    if (WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
    }

    LogBrowserMessage(stage, "Komut tamamlandi. exitCode=" + std::to_string(exitCode));
    return CommandExecution{true, exitCode, Trim(output)};
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

std::filesystem::path ResolveRootPath(const nlohmann::json& arguments) {
    if (arguments.contains("rootPath") && arguments.at("rootPath").is_string()) {
        const std::string configuredPath = Trim(arguments.at("rootPath").get<std::string>());
        if (!configuredPath.empty()) {
            return std::filesystem::path(configuredPath);
        }
    }
    return std::filesystem::current_path() / ".voice_agent_browser";
}

std::filesystem::path ResolveVirtualEnvPath(const nlohmann::json& arguments, const std::filesystem::path& rootPath) {
    if (arguments.contains("venvPath") && arguments.at("venvPath").is_string()) {
        const std::string configuredPath = Trim(arguments.at("venvPath").get<std::string>());
        if (!configuredPath.empty()) {
            return std::filesystem::path(configuredPath);
        }
    }
    return rootPath / "venv";
}

std::string ResolveVirtualEnvPython(const std::filesystem::path& venvPath) {
    return (venvPath / "bin" / "python").string();
}

ToolResult CreateVirtualEnvIfNeeded(
    const std::string& basePythonExecutable,
    const std::filesystem::path& venvPath) {
    const std::string venvPython = ResolveVirtualEnvPython(venvPath);
    if (std::filesystem::exists(venvPython)) {
        return ToolResult{true, false, "Browser Python environment hazir.", {{"venvPath", venvPath.string()}, {"pythonExecutable", venvPython}}};
    }

    std::error_code error;
    std::filesystem::create_directories(venvPath.parent_path(), error);
    if (error) {
        return ToolResult{
            false,
            false,
            "Browser Python environment klasoru olusturulamadi.",
            {{"reason", "create_venv_parent_failed"}, {"details", error.message()}, {"venvPath", venvPath.string()}}
        };
    }

    const std::string createEnvCommand = ShellEscape(basePythonExecutable) + " -m venv " + ShellEscape(venvPath.string());
    const CommandExecution createEnvResult = RunCommand(createEnvCommand, "create_virtualenv");
    if (!createEnvResult.launched) {
        return ToolResult{false, false, createEnvResult.output, {{"stage", "create_virtualenv"}, {"command", createEnvCommand}}};
    }
    if (createEnvResult.exitCode != 0) {
        return ToolResult{
            false,
            false,
            "Browser Python environment olusturulamadi.",
            {
                {"stage", "create_virtualenv"},
                {"command", createEnvCommand},
                {"exitCode", createEnvResult.exitCode},
                {"output", createEnvResult.output},
                {"venvPath", venvPath.string()}
            }
        };
    }

    return ToolResult{true, false, "Browser Python environment olusturuldu.", {{"venvPath", venvPath.string()}, {"pythonExecutable", venvPython}}};
}

ToolResult EnsurePlaywrightReady(
    const std::string& pythonExecutable,
    const std::filesystem::path& browsersPath,
    bool installBundledChromium) {
    const std::string installPackageCommand = ShellEscape(pythonExecutable) + " -m pip install playwright";
    const CommandExecution installPackageResult = RunCommand(installPackageCommand, "install_playwright_package");
    if (!installPackageResult.launched) {
        return ToolResult{false, false, installPackageResult.output, {{"stage", "install_playwright_package"}}};
    }
    if (installPackageResult.exitCode != 0) {
        return ToolResult{
            false,
            false,
            "Playwright paketi yuklenemedi.",
            {
                {"stage", "install_playwright_package"},
                {"command", installPackageCommand},
                {"exitCode", installPackageResult.exitCode},
                {"output", installPackageResult.output}
            }
        };
    }

    if (!installBundledChromium) {
        return ToolResult{true, false, "Playwright paketi hazir (CDP modu).", {{"pythonExecutable", pythonExecutable}}};
    }

    std::error_code error;
    std::filesystem::create_directories(browsersPath, error);
    if (error) {
        return ToolResult{false, false, "Browser cache klasoru olusturulamadi.", {{"reason", "create_browser_cache_failed"}, {"details", error.message()}}};
    }

    const std::string installBrowserCommand =
        "PLAYWRIGHT_BROWSERS_PATH=" + ShellEscape(browsersPath.string()) + " " +
        ShellEscape(pythonExecutable) + " -m playwright install chromium";
    const CommandExecution installBrowserResult = RunCommand(installBrowserCommand, "install_chromium_browser");
    if (!installBrowserResult.launched) {
        return ToolResult{false, false, installBrowserResult.output, {{"stage", "install_chromium_browser"}}};
    }
    if (installBrowserResult.exitCode != 0) {
        return ToolResult{
            false,
            false,
            "Playwright Chromium browser kurulumu basarisiz oldu.",
            {
                {"stage", "install_chromium_browser"},
                {"command", installBrowserCommand},
                {"exitCode", installBrowserResult.exitCode},
                {"output", installBrowserResult.output}
            }
        };
    }

    return ToolResult{true, false, "Playwright hazir.", {{"pythonExecutable", pythonExecutable}, {"browsersPath", browsersPath.string()}}};
}

class TemporaryFile {
public:
    TemporaryFile(const std::string& prefix, const std::string& suffix, const std::string& content) {
        std::string fileTemplate = "/tmp/" + prefix + "XXXXXX" + suffix;
        std::vector<char> buffer(fileTemplate.begin(), fileTemplate.end());
        buffer.push_back('\0');

        const int suffixLength = static_cast<int>(suffix.size());
        const int fd = mkstemps(buffer.data(), suffixLength);
        if (fd == -1) {
            throw std::runtime_error("Temporary file could not be created.");
        }

        path_ = buffer.data();

        std::ofstream stream(path_);
        if (!stream) {
            close(fd);
            std::filesystem::remove(path_);
            throw std::runtime_error("Temporary file could not be opened.");
        }

        stream << content;
        stream.close();
        close(fd);
    }

    ~TemporaryFile() {
        if (!path_.empty()) {
            std::error_code ignoredError;
            std::filesystem::remove(path_, ignoredError);
        }
    }

    const std::string& Path() const { return path_; }

private:
    std::string path_;
};

std::string BuildRunnerScript(const std::string& runnerPath) {
    std::ifstream in(runnerPath);
    if (!in) throw std::runtime_error("webbrowser_runner.py bulunamadı! (" + runnerPath + ")");
    std::string code((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return code;
}

std::vector<ToolImageAttachment> BuildImageAttachments(const nlohmann::json& payload, const nlohmann::json& arguments) {
    std::vector<ToolImageAttachment> attachments;
    const std::string detail = Trim(arguments.value("visionDetail", "high"));
    const std::string normalizedDetail = detail.empty() ? "high" : detail;

    auto appendIfFileExists = [&attachments, &normalizedDetail](const nlohmann::json& value) {
        if (!value.is_string()) {
            return;
        }
        const std::string path = Trim(value.get<std::string>());
        if (path.empty() || !std::filesystem::exists(path)) {
            return;
        }
        const bool alreadyAdded = std::any_of(attachments.begin(), attachments.end(), [&path](const ToolImageAttachment& item) {
            return item.filePath == path;
        });
        if (!alreadyAdded) {
            attachments.push_back(ToolImageAttachment{path, normalizedDetail});
        }
    };

    if (payload.contains("results") && payload.at("results").is_array()) {
        for (const auto& item : payload.at("results")) {
            if (!item.is_object()) {
                continue;
            }
            if (item.value("action", "") == "screenshot") {
                appendIfFileExists(item.value("path", ""));
            }
        }
    }

    if (payload.contains("finalScreenshot")) {
        appendIfFileExists(payload.at("finalScreenshot"));
    }

    return attachments;
}

ToolResult BuildChallengeResult(
    const nlohmann::json& payload,
    const std::string& runCommand,
    int exitCode,
    const std::filesystem::path& artifactsPath,
    const std::filesystem::path& venvPath,
    const std::string& pythonExecutable,
    const nlohmann::json& arguments) {
    ToolResult result{
        false,
        false,
        payload.value("error", "Bot korumasi sayfasi tespit edildi."),
        {
            {"stage", "run_browser_script"},
            {"command", runCommand},
            {"exitCode", exitCode},
            {"output", payload},
            {"reason", "challenge_detected"},
            {"artifactsDir", artifactsPath.string()},
            {"venvPath", venvPath.string()},
            {"pythonExecutable", pythonExecutable}
        }
    };
    result.imageAttachments = BuildImageAttachments(payload, arguments);
    return result;
}

namespace {
constexpr const char* kEventPrefix = "__VA_EVENT__";
}

CommandExecution RunInteractiveCommand(
    const std::string& command,
    const std::string& stage,
    IUserPromptProvider* promptProvider,
    int promptDefaultTimeoutSeconds,
    const CancellationToken* token
) {
    int inPipe[2] = {-1, -1};   // parent -> child stdin
    int outPipe[2] = {-1, -1};  // child stdout -> parent
    if (pipe(inPipe) != 0 || pipe(outPipe) != 0) {
        return CommandExecution{false, -1, std::string("pipe() failed: ") + std::strerror(errno)};
    }

    LogBrowserMessage(stage, "Komut baslatiliyor (interactive).");
    LogBrowserMessage(stage, "Command: " + command);

    pid_t pid = fork();
    if (pid < 0) {
        ::close(inPipe[0]); ::close(inPipe[1]);
        ::close(outPipe[0]); ::close(outPipe[1]);
        return CommandExecution{false, -1, std::string("fork() failed: ") + std::strerror(errno)};
    }
    if (pid == 0) {
        // child
        ::dup2(inPipe[0], STDIN_FILENO);
        ::dup2(outPipe[1], STDOUT_FILENO);
        ::dup2(outPipe[1], STDERR_FILENO);
        ::close(inPipe[0]); ::close(inPipe[1]);
        ::close(outPipe[0]); ::close(outPipe[1]);
        ::execlp("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }

    // parent
    ::close(inPipe[0]);
    ::close(outPipe[1]);
    const int childStdin = inPipe[1];
    const int childStdout = outPipe[0];

    // Read child stdout line by line; intercept event lines.
    std::string output;
    std::string lineBuffer;
    std::array<char, 1024> readBuf{};
    bool truncated = false;

    auto handleLine = [&](const std::string& line) {
        const std::string trimmed = Trim(line);
        if (trimmed.rfind(kEventPrefix, 0) == 0) {
            const std::string payload = Trim(trimmed.substr(std::strlen(kEventPrefix)));
            LogBrowserMessage(stage, "[event] " + payload);
            nlohmann::json event = nlohmann::json::parse(payload, nullptr, false);
            if (event.is_discarded() || !event.is_object()) {
                return;
            }
            if (event.value("type", "") == "prompt") {
                const std::string question = event.value("question", "");
                int timeoutSec = event.value("timeoutSeconds", promptDefaultTimeoutSeconds);
                if (timeoutSec <= 0) timeoutSec = promptDefaultTimeoutSeconds;
                const std::string mode = event.value("mode", "text");

                nlohmann::json response;
                if (promptProvider == nullptr) {
                    response = {{"ok", false}, {"error", "no prompt provider available"}};
                } else {
                    PromptOptions options;
                    options.timeoutSeconds = timeoutSec;
                    options.mode = mode;
                    const PromptResult result = promptProvider->Ask(question, options, token);
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
                    const ssize_t n = ::write(childStdin, responseLine.data() + written,
                                              responseLine.size() - written);
                    if (n <= 0) {
                        if (errno == EINTR) continue;
                        break;
                    }
                    written += n;
                }
                LogBrowserMessage(stage, "[event] response sent (ok=" +
                                  std::string(response.value("ok", false) ? "true" : "false") + ")");
            }
            return;
        }
        if (!truncated) {
            output += line;
            output += '\n';
            if (output.size() > 64000) {
                output.resize(64000);
                output += "\n[truncated]";
                truncated = true;
            }
        }
        if (!trimmed.empty()) {
            LogBrowserMessage(stage, trimmed);
        }
    };

    while (true) {
        const ssize_t n = ::read(childStdout, readBuf.data(), readBuf.size());
        if (n < 0) {
            if (errno == EINTR) continue;
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
        if (token != nullptr && token->IsCancelled()) {
            ::kill(pid, SIGTERM);
        }
    }
    if (!lineBuffer.empty()) {
        handleLine(lineBuffer);
    }

    ::close(childStdin);
    ::close(childStdout);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) break;
    }
    int exitCode = -1;
    if (WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exitCode = 128 + WTERMSIG(status);
    }

    LogBrowserMessage(stage, "Komut tamamlandi (interactive). exitCode=" + std::to_string(exitCode));
    return CommandExecution{true, exitCode, Trim(output)};
}

}  // namespace

WebBrowserTool::WebBrowserTool(const AppConfig& config,
                                                             const AccountStore* accountStore,
                                                             const SkillRegistry* skillRegistry)
    : runnerScriptPath_(config.webBrowserRunnerPath),
      accountStore_(accountStore),
            skillRegistry_(skillRegistry),
      promptTimeoutSeconds_(config.browserPromptTimeoutSeconds > 0 ? config.browserPromptTimeoutSeconds : 180),
      definition_({
          "WebBrowserTool",
          "Playwright ile gercek bir Chromium sayfasi acar; gitme, tiklama, yazma, bekleme, ekran goruntusu alma ve metin okuma gibi cok adimli web islemleri yapar. accountId verilirse o hesabin kalici Chromium profili acilir; oturum yoksa kullaniciya gorunur bir pencere acilip oturum acmasi (ve gerekirse 2FA tamamlamasi) istenir, sonraki cagrilarda oturum hazirdir. account.json'da olmayan siteler icin sessionId + sessionLoginUrl/sessionLoggedInUrl verilerek ayni mantikta kalici bir site oturumu da tutulabilir. useChromeProfile=true verildiginde sistem Chrome'u kullanicinin profili (izole bir kopyaya alinarak) uzerinden CDP ile surulur.",
          {
              {"type", "object"},
              {"properties", {
                  {"accountId", {{"type", "string"}, {"description", "account.json icindeki hesap kimligi. Verildiginde tool o hesabin kalici Chromium profilini kullanir; oturum yoksa kullanicidan gorunur pencerede oturum acmasi istenir."}}},
                  {"sessionId", {{"type", "string"}, {"description", "account.json disindaki bir site icin kalici login oturumu kimligi. Ornek: 'vapi_main'. sessionLoginUrl/sessionLoggedInUrl ile birlikte kullanilir."}}},
                  {"sessionDisplayName", {{"type", "string"}, {"description", "Kullaniciya gosterilecek site/hesap adi. Ornek: 'Vapi.ai'."}}},
                  {"sessionLoginUrl", {{"type", "string"}, {"description", "Manuel giriste acilacak login URL'si."}}},
                  {"sessionLoggedInUrl", {{"type", "string"}, {"description", "Oturum acik kontrolu icin gidilecek URL. Genelde dashboard/home sayfasi."}}},
                  {"sessionLoginCheckSelector", {{"type", "string"}, {"description", "Opsiyonel login kontrol selector'u. Bilinmiyorsa bos birakilabilir."}}},
                  {"steps", {
                      {"type", "array"},
                      {"description", "Her biri 'action' alanli web adimlari. Action: goto, click, click_first, click_close, dismiss_popups, type, fill, press, hover, wait_for_selector, wait_for_clickable, wait_for_load_state, wait_for_timeout, extract_text, extract_attribute, snapshot, screenshot, select, evaluate, mouse_click, keyboard_press, scroll. Locator icin selector / role+name / textSelector / label / placeholder / altText / titleSelector kullanilabilir; ayrica selectors dizisi ile fallback verilebilir. force, delayMs, timeoutMs, requireSuccess gibi alanlar opsiyoneldir. goto adiminda cerez/popup pencereleri otomatik kapatilir (dismissPopups=false ile devre disi birakilabilir); 'dismiss_popups' action'u ile manuel olarak da tetiklenebilir."}
                  }},
                  {"pythonExecutable", {{"type", "string"}}},
                  {"venvPath", {{"type", "string"}}},
                  {"rootPath", {{"type", "string"}}},
                  {"headless", {{"type", "boolean"}}},
                  {"timeoutMs", {{"type", "integer"}}},
                  {"slowMoMs", {{"type", "integer"}}},
                  {"viewportWidth", {{"type", "integer"}}},
                  {"viewportHeight", {{"type", "integer"}}},
                  {"locale", {{"type", "string"}}},
                  {"visionDetail", {{"type", "string"}}},
                  {"useChromeProfile", {{"type", "boolean"}, {"description", "true ise sistem Chrome'u --remote-debugging-port ile baslatilip CDP uzerinden baglanir; gercek kullanici profili izole bir kopyaya alinarak kullanilir."}}},
                  {"chromeExecutablePath", {{"type", "string"}}},
                  {"chromeUserDataDir", {{"type", "string"}, {"description", "Kaynak Chrome user-data klasoru."}}},
                  {"chromeProfileName", {{"type", "string"}, {"description", "Hedef profilin gorunen adi (orn: 'Yusuf - Kisi 2'); profile_dir bulmak icin ipucu."}}},
                  {"chromeProfileDir", {{"type", "string"}, {"description", "Profil klasoru adi (Default, Profile 1, ...)."}}},
                  {"automationUserDataDir", {{"type", "string"}, {"description", "Profilin kopyalanacagi izole klasor."}}},
                  {"chromeDebugPort", {{"type", "integer"}, {"description", "0 / verilmezse otomatik bos port."}}},
                  {"refreshProfileCopy", {{"type", "boolean"}}},
                  {"storageStatePath", {{"type", "string"}, {"description", "Bundled chromium icin yuklenecek storage_state JSON yolu."}}},
                  {"saveStorageStatePath", {{"type", "string"}, {"description", "Bundled chromium icin kaydedilecek storage_state JSON yolu."}}}
              }},
              {"required", {"steps"}}
          },
          {"browser.web", "playwright.run", "web.interact"},
          ToolRiskLevel::Dangerous,
      }) {}

const ToolDefinition& WebBrowserTool::Definition() const {
    return definition_;
}

ToolResult WebBrowserTool::Execute(const ToolCall& call, const CancellationToken* token) const {
    if (!call.arguments.contains("steps") || !call.arguments.at("steps").is_array() || call.arguments.at("steps").empty()) {
        return ToolResult{false, false, "steps dizisi gerekli.", {{"reason", "missing_steps"}}};
    }

    const std::string basePythonExecutable = Trim(call.arguments.value("pythonExecutable", "python3"));
    if (basePythonExecutable.empty()) {
        return ToolResult{false, false, "Python executable bilgisi bos olamaz.", {{"reason", "missing_python_executable"}}};
    }

    const std::filesystem::path rootPath = ResolveRootPath(call.arguments);
    const std::filesystem::path venvPath = ResolveVirtualEnvPath(call.arguments, rootPath);
    const std::filesystem::path browsersPath = rootPath / "browsers";
    const std::filesystem::path artifactsPath = rootPath / "artifacts";
    const bool useChromeProfile = call.arguments.value("useChromeProfile", false);

    LogBrowserMessage("execute", "Web islemi basladi.");
    LogBrowserMessage("execute", "rootPath=" + rootPath.string());
    LogBrowserMessage("execute", "venvPath=" + venvPath.string());
    LogBrowserMessage("execute", "useChromeProfile=" + std::string(useChromeProfile ? "true" : "false"));
    LogBrowserMessage("execute", "stepCount=" + std::to_string(call.arguments.at("steps").size()));

    std::error_code error;
    std::filesystem::create_directories(artifactsPath, error);
    if (error) {
        return ToolResult{false, false, "Browser artifact klasoru olusturulamadi.", {{"reason", "create_artifacts_failed"}, {"details", error.message()}}};
    }

    const ToolResult venvResult = CreateVirtualEnvIfNeeded(basePythonExecutable, venvPath);
    if (!venvResult.succeeded) {
        return venvResult;
    }

    const std::string pythonExecutable = ResolveVirtualEnvPython(venvPath);
    LogBrowserMessage("execute", "pythonExecutable=" + pythonExecutable);

    // Bundled chromium her zaman kurulu olsun: useChromeProfile=true ve sistem Chrome bulunamadiginda
    // launch_persistent_context fallback'i icin gereklidir.
    const ToolResult playwrightReadyResult =
        EnsurePlaywrightReady(pythonExecutable, browsersPath, /*installBundledChromium=*/true);
    if (!playwrightReadyResult.succeeded) {
        return playwrightReadyResult;
    }

    nlohmann::json config = {
        {"steps", call.arguments.at("steps")},
        {"artifactsDir", artifactsPath.string()},
        {"headless", call.arguments.value("headless", true)},
        {"timeoutMs", call.arguments.value("timeoutMs", 15000)},
        {"slowMoMs", call.arguments.value("slowMoMs", 0)},
        {"viewportWidth", call.arguments.value("viewportWidth", 1365)},
        {"viewportHeight", call.arguments.value("viewportHeight", 900)},
        {"locale", call.arguments.value("locale", "tr-TR")},
        {"useChromeProfile", useChromeProfile}
    };

    const std::filesystem::path repoRoot = ResolveRepoRootFromRunner(runnerScriptPath_);

    // Account injection: when accountId is provided, look it up locally and
    // inject the persistent profile dir + a public-safe account record. The
    // LLM cannot override these values directly.
    std::string resolvedAccountId;
    std::string resolvedSessionId;
    std::string resolvedSessionDisplayName;
    std::string resolvedSessionLoginUrl;
    std::string resolvedSessionLoggedInUrl;
    std::string resolvedSessionLoginCheckSelector;
    bool accountInjected = false;
    if (call.arguments.contains("accountId") && call.arguments.at("accountId").is_string()) {
        const std::string accountId = Trim(call.arguments.at("accountId").get<std::string>());
        if (!accountId.empty()) {
            if (accountStore_ == nullptr || !accountStore_->Loaded()) {
                return ToolResult{
                    false, false,
                    "Hesap deposu yuklenmemis (account.json bulunamadi).",
                    {{"reason", "accounts_not_loaded"}, {"accountId", accountId}}
                };
            }
            const auto record = accountStore_->Find(accountId);
            if (!record.has_value()) {
                return ToolResult{
                    false, false,
                    "Bilinmeyen hesap kimligi: " + accountId,
                    {{"reason", "unknown_account"}, {"accountId", accountId}}
                };
            }
            std::error_code profileError;
            std::filesystem::create_directories(record->profileDir, profileError);
            if (profileError) {
                return ToolResult{
                    false, false,
                    "Hesap profil klasoru olusturulamadi.",
                    {{"reason", "create_account_profile_failed"},
                     {"accountId", accountId},
                     {"profileDir", record->profileDir.string()},
                     {"details", profileError.message()}}
                };
            }
            const AccountLoginConfig loginConfig = ResolveAccountLoginConfig(*record, skillRegistry_);
            if (loginConfig.loginUrl.empty() || loginConfig.loggedInUrl.empty()) {
                return ToolResult{
                    false,
                    false,
                    "Hesap login metadata'si eksik. Ilgili skill frontmatter'inda account.loginUrl ve account.loggedInUrl tanimlanmali.",
                    {{"reason", "missing_account_login_metadata"}, {"accountId", accountId}}
                };
            }
            config["accountProfileDir"] = record->profileDir.string();
            config["account"] = {
                {"id", record->id},
                {"displayName", record->displayName},
                {"provider", record->provider},
                {"loginUrl", loginConfig.loginUrl},
                {"loggedInUrl", loginConfig.loggedInUrl},
                {"loginCheckSelector", loginConfig.loginCheckSelector},
                {"manualLoginTimeoutSeconds", promptTimeoutSeconds_},
            };
            // headless'i runner DISPLAY varligina gore karar verir; burada zorlama yok.
            resolvedAccountId = record->id;
            accountInjected = true;
            LogBrowserMessage("execute", "accountId=" + accountId + " profileDir=" + record->profileDir.string());
        }
    }
    if (!accountInjected && call.arguments.contains("sessionId") && call.arguments.at("sessionId").is_string()) {
        const std::string sessionId = Trim(call.arguments.at("sessionId").get<std::string>());
        const std::string sessionLoginUrl = Trim(call.arguments.value("sessionLoginUrl", ""));
        const std::string sessionLoggedInUrl = Trim(call.arguments.value("sessionLoggedInUrl", ""));
        const std::string sessionDisplayName = Trim(call.arguments.value("sessionDisplayName", sessionId));
        const std::string sessionLoginCheckSelector = Trim(call.arguments.value("sessionLoginCheckSelector", ""));

        if (!sessionId.empty()) {
            if (sessionLoginUrl.empty() || sessionLoggedInUrl.empty()) {
                return ToolResult{
                    false,
                    false,
                    "sessionId kullaniliyorsa sessionLoginUrl ve sessionLoggedInUrl gerekli.",
                    {{"reason", "missing_session_login_fields"}, {"sessionId", sessionId}}
                };
            }

            std::filesystem::path sessionProfileDir = repoRoot / ".voice_agent_browser" / "sessions" / sessionId;
            std::error_code profileError;
            std::filesystem::create_directories(sessionProfileDir, profileError);
            if (profileError) {
                return ToolResult{
                    false,
                    false,
                    "Site oturum profil klasoru olusturulamadi.",
                    {{"reason", "create_session_profile_failed"},
                     {"sessionId", sessionId},
                     {"profileDir", sessionProfileDir.string()},
                     {"details", profileError.message()}}
                };
            }

            config["accountProfileDir"] = sessionProfileDir.string();
            config["account"] = {
                {"id", sessionId},
                {"displayName", sessionDisplayName.empty() ? sessionId : sessionDisplayName},
                {"provider", "generic"},
                {"loginUrl", sessionLoginUrl},
                {"loggedInUrl", sessionLoggedInUrl},
                {"loginCheckSelector", sessionLoginCheckSelector},
                {"manualLoginTimeoutSeconds", promptTimeoutSeconds_},
            };

            resolvedSessionId = sessionId;
            resolvedSessionDisplayName = sessionDisplayName.empty() ? sessionId : sessionDisplayName;
            resolvedSessionLoginUrl = sessionLoginUrl;
            resolvedSessionLoggedInUrl = sessionLoggedInUrl;
            resolvedSessionLoginCheckSelector = sessionLoginCheckSelector;
            accountInjected = true;
            LogBrowserMessage("execute", "sessionId=" + sessionId + " profileDir=" + sessionProfileDir.string());
        }
    }

    for (const char* key : {
            "chromeExecutablePath", "chromeUserDataDir", "chromeProfileName",
            "chromeProfileDir", "automationUserDataDir", "chromeChannel",
            "storageStatePath", "saveStorageStatePath"
        }) {
        if (call.arguments.contains(key) && call.arguments.at(key).is_string()) {
            const std::string value = Trim(call.arguments.at(key).get<std::string>());
            if (!value.empty()) {
                config[key] = value;
            }
        }
    }
    if (call.arguments.contains("proxy") && call.arguments.at("proxy").is_string()) {
        const std::string proxy = Trim(call.arguments.at("proxy").get<std::string>());
        if (!proxy.empty()) {
            config["proxy"] = proxy;
        }
    }
    // useChromeProfile ve chromeUserDataDir desteği zaten mevcut
    if (call.arguments.contains("chromeDebugPort") && call.arguments.at("chromeDebugPort").is_number_integer()) {
        config["chromeDebugPort"] = call.arguments.at("chromeDebugPort").get<int>();
    }
    if (call.arguments.contains("refreshProfileCopy") && call.arguments.at("refreshProfileCopy").is_boolean()) {
        config["refreshProfileCopy"] = call.arguments.at("refreshProfileCopy").get<bool>();
    }

    std::string userAgent;
    if (call.arguments.contains("userAgent") && call.arguments.at("userAgent").is_string()) {
        userAgent = Trim(call.arguments.at("userAgent").get<std::string>());
        if (!userAgent.empty()) {
            config["userAgent"] = userAgent;
        }
    }

    TemporaryFile configFile("voice_agent_browser_config_", ".json", config.dump(2));
    TemporaryFile scriptFile("voice_agent_browser_runner_", ".py", BuildRunnerScript(runnerScriptPath_));

    LogBrowserMessage("execute", "configFile=" + configFile.Path());
    LogBrowserMessage("execute", "scriptFile=" + scriptFile.Path());

    const std::string envPrefix =
        "PLAYWRIGHT_BROWSERS_PATH=" + ShellEscape(browsersPath.string()) + " ";

    const std::string runCommand =
        envPrefix +
        ShellEscape(pythonExecutable) + " -u " +
        ShellEscape(scriptFile.Path()) + " " +
        ShellEscape(configFile.Path());
    const CommandExecution runResult = RunInteractiveCommand(
        runCommand, "run_browser_script",
        promptProvider_, promptTimeoutSeconds_, token
    );
    if (!runResult.launched) {
        return ToolResult{false, false, runResult.output, {{"stage", "run_browser_script"}}};
    }

    const std::string jsonLine = ExtractLastJsonLine(runResult.output);
    const nlohmann::json payload = nlohmann::json::parse(jsonLine, nullptr, false);
    if (runResult.exitCode != 0) {
        if (!payload.is_discarded() && payload.is_object()) {
            if (payload.value("challengeDetected", false)) {
                return BuildChallengeResult(
                    payload, runCommand, runResult.exitCode,
                    artifactsPath, venvPath, pythonExecutable, call.arguments
                );
            }
            if (!resolvedAccountId.empty() && payload.contains("loginStatus") && payload.at("loginStatus").is_object()) {
                return ToolResult{
                    false,
                    false,
                    payload.value("error", "Hesap oturumu dogrulanamadi."),
                    BuildAccountRecoveryOutput(payload, resolvedAccountId, runCommand, runResult.exitCode)
                };
            }
            if (!resolvedSessionId.empty() && payload.contains("loginStatus") && payload.at("loginStatus").is_object()) {
                return ToolResult{
                    false,
                    false,
                    payload.value("error", "Site oturumu dogrulanamadi."),
                    BuildSessionRecoveryOutput(
                        payload,
                        resolvedSessionId,
                        resolvedSessionDisplayName,
                        resolvedSessionLoginUrl,
                        resolvedSessionLoggedInUrl,
                        resolvedSessionLoginCheckSelector,
                        repoRoot,
                        runCommand,
                        runResult.exitCode
                    )
                };
            }
            return ToolResult{
                false,
                false,
                payload.value("error", "Web browser araci hata verdi."),
                {
                    {"stage", "run_browser_script"},
                    {"command", runCommand},
                    {"exitCode", runResult.exitCode},
                    {"output", payload}
                }
            };
        }

        return ToolResult{
            false,
            false,
            "Web browser araci hata verdi.",
            {
                {"stage", "run_browser_script"},
                {"command", runCommand},
                {"exitCode", runResult.exitCode},
                {"output", runResult.output},
                {"jsonLine", jsonLine}
            }
        };
    }

    if (payload.is_discarded() || !payload.is_object()) {
        return ToolResult{
            false,
            false,
            "Web browser araci beklenen JSON sonucunu donmedi.",
            {
                {"stage", "run_browser_script"},
                {"command", runCommand},
                {"exitCode", runResult.exitCode},
                {"output", runResult.output},
                {"jsonLine", jsonLine}
            }
        };
    }

    if (payload.value("challengeDetected", false)) {
        return BuildChallengeResult(
            payload, runCommand, runResult.exitCode,
            artifactsPath, venvPath, pythonExecutable, call.arguments
        );
    }

    ToolResult result{
        true,
        false,
        payload.value("title", "Web islemi tamamlandi."),
        {
            {"stage", "run_browser_script"},
            {"command", runCommand},
            {"exitCode", runResult.exitCode},
            {"output", payload},
            {"artifactsDir", artifactsPath.string()},
            {"venvPath", venvPath.string()},
            {"pythonExecutable", pythonExecutable}
        }
    };
    if (!resolvedAccountId.empty()) {
        result.output["accountId"] = resolvedAccountId;
        if (payload.contains("loginStatus")) {
            result.output["loginStatus"] = payload.at("loginStatus");
        }
    }
    if (!resolvedSessionId.empty()) {
        result.output["sessionId"] = resolvedSessionId;
        result.output["sessionDisplayName"] = resolvedSessionDisplayName;
        result.output["sessionLoginUrl"] = resolvedSessionLoginUrl;
        result.output["sessionLoggedInUrl"] = resolvedSessionLoggedInUrl;
        result.output["sessionLoginCheckSelector"] = resolvedSessionLoginCheckSelector;
        if (payload.contains("loginStatus")) {
            result.output["loginStatus"] = payload.at("loginStatus");
        }
    }
    result.imageAttachments = BuildImageAttachments(payload, call.arguments);
    return result;
}

}  // namespace voice_agent
