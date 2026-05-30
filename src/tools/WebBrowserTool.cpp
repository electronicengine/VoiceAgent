#include "tools/WebBrowserTool.h"

#include "common/StringUtils.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace voice_agent {

namespace {

struct CommandExecution {
    bool launched = false;
    int exitCode = -1;
    std::string output;
};

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
        if (output.size() > 16000) {
            output.resize(16000);
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
    const std::filesystem::path& browsersPath) {
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

    const std::string& Path() const {
        return path_;
    }

private:
    std::string path_;
};

std::string BuildRunnerScript() {
    return R"PY(
import json
import os
import sys
from pathlib import Path

from playwright.sync_api import TimeoutError as PlaywrightTimeoutError
from playwright.sync_api import sync_playwright


def resolve_locator(page, step):
    selector = step.get("selector")
    if selector:
        return page.locator(selector)

    role = step.get("role")
    if role:
        return page.get_by_role(role, name=step.get("name"))

    text = step.get("textSelector")
    if text:
        return page.get_by_text(text, exact=step.get("exact", False))

    label = step.get("label")
    if label:
        return page.get_by_label(label, exact=step.get("exact", False))

    placeholder = step.get("placeholder")
    if placeholder:
        return page.get_by_placeholder(placeholder)

    alt_text = step.get("altText")
    if alt_text:
        return page.get_by_alt_text(alt_text)

    title = step.get("titleSelector")
    if title:
        return page.get_by_title(title)

    raise ValueError("Step requires selector, role, textSelector, label, placeholder, altText or titleSelector")


def shorten(text, limit=4000):
    text = (text or "").strip()
    if len(text) <= limit:
        return text
    return text[:limit] + "\n[truncated]"


def maybe_locator(page, step):
    keys = ["selector", "role", "textSelector", "label", "placeholder", "altText", "titleSelector"]
    if any(step.get(key) for key in keys):
        return resolve_locator(page, step)
    return None


def log(message):
    print(f"[runner] {message}", flush=True)


def detect_bot_challenge(page):
    title = (page.title() or "").strip().lower()
    text = (page.locator("body").inner_text() or "").strip().lower()
    url = (page.url or "").strip().lower()

    signals = [
        "just a moment",
        "bir dakika lütfen",
        "güvenlik doğrulaması yapılıyor",
        "security check",
        "verify you are human",
        "checking your browser",
        "cloudflare",
        "bot olmadığınızı doğrularken",
    ]

    if "__cf_chl" in url:
        return True, "cloudflare_challenge_url"

    combined = "\n".join([title, text])
    for signal in signals:
        if signal in combined:
            return True, signal

    return False, ""


def main():
    config_path = Path(sys.argv[1])
    config = json.loads(config_path.read_text())
    steps = config["steps"]
    artifacts_dir = Path(config["artifactsDir"])
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    log(f"config loaded from {config_path}")
    log(f"artifacts dir: {artifacts_dir}")
    log(f"step count: {len(steps)}")

    try:
        with sync_playwright() as playwright:
            log("launching chromium")
            browser = playwright.chromium.launch(
                headless=config.get("headless", True),
                slow_mo=config.get("slowMoMs", 0),
            )
            log("chromium launched")
            context = browser.new_context(
                viewport={
                    "width": int(config.get("viewportWidth", 1365)),
                    "height": int(config.get("viewportHeight", 900)),
                },
                locale=config.get("locale", "tr-TR"),
            )
            log("browser context created")
            page = context.new_page()
            page.set_default_timeout(int(config.get("timeoutMs", 15000)))
            log(f"page created, timeout={int(config.get('timeoutMs', 15000))}ms")

            results = []
            for index, step in enumerate(steps):
                action = step.get("action")
                if not action:
                    raise ValueError("Each step requires an action field")

                log(f"starting step {index + 1}/{len(steps)} action={action}")
                entry = {"index": index, "action": action}
                locator = maybe_locator(page, step)

                if action in ("goto", "navigate"):
                    response = page.goto(step["url"], wait_until=step.get("waitUntil", "domcontentloaded"))
                    if step.get("loadState"):
                        page.wait_for_load_state(step["loadState"])
                    entry["status"] = response.status if response else None
                elif action == "click":
                    if locator is None:
                        raise ValueError("click action requires a locator")
                    locator.click(delay=step.get("delayMs", 50), force=step.get("force", False))
                elif action == "hover":
                    if locator is None:
                        raise ValueError("hover action requires a locator")
                    locator.hover()
                elif action == "type":
                    if locator is None:
                        raise ValueError("type action requires a locator")
                    locator.click()
                    locator.type(step.get("text", ""), delay=step.get("delayMs", 60))
                elif action == "fill":
                    if locator is None:
                        raise ValueError("fill action requires a locator")
                    locator.fill(step.get("text", ""))
                elif action == "press":
                    key = step.get("key")
                    if not key:
                        raise ValueError("press action requires key")
                    if locator is None:
                        page.keyboard.press(key)
                    else:
                        locator.press(key)
                elif action == "select":
                    if locator is None:
                        raise ValueError("select action requires a locator")
                    option_kwargs = {}
                    if "value" in step:
                        option_kwargs["value"] = step["value"]
                    if "labelValue" in step:
                        option_kwargs["label"] = step["labelValue"]
                    if "indexValue" in step:
                        option_kwargs["index"] = int(step["indexValue"])
                    locator.select_option(**option_kwargs)
                elif action == "wait_for_selector":
                    if locator is None:
                        raise ValueError("wait_for_selector action requires a locator")
                    locator.wait_for(state=step.get("state", "visible"), timeout=step.get("timeoutMs"))
                elif action == "wait_for_timeout":
                    page.wait_for_timeout(int(step.get("timeoutMs", 1000)))
                elif action == "extract_text":
                    target = locator if locator is not None else page.locator("body")
                    entry["text"] = shorten(target.inner_text(), int(step.get("maxLength", 4000)))
                elif action == "snapshot":
                    entry["text"] = shorten(page.locator("body").inner_text(), int(step.get("maxLength", 4000)))
                    entry["htmlLength"] = len(page.content())
                elif action == "screenshot":
                    path = artifacts_dir / step.get("path", f"step_{index + 1}.png")
                    path.parent.mkdir(parents=True, exist_ok=True)
                    page.screenshot(path=str(path), full_page=step.get("fullPage", True))
                    entry["path"] = str(path)
                else:
                    raise ValueError(f"Unsupported action: {action}")

                entry["url"] = page.url
                entry["title"] = page.title()
                results.append(entry)
                log(f"finished step {index + 1}/{len(steps)} action={action} url={page.url}")

            final_screenshot = artifacts_dir / "final.png"
            log(f"capturing final screenshot to {final_screenshot}")
            page.screenshot(path=str(final_screenshot), full_page=True)
            challenge_detected, challenge_reason = detect_bot_challenge(page)
            output = {
                "ok": True,
                "url": page.url,
                "title": page.title(),
                "results": results,
                "artifactsDir": str(artifacts_dir),
                "finalScreenshot": str(final_screenshot),
                "challengeDetected": challenge_detected,
                "challengeReason": challenge_reason,
            }
            browser.close()
            log("browser closed")
            if challenge_detected:
                output["ok"] = False
                output["error"] = "Bot korumasi sayfasi tespit edildi. Site otomatik erisimi engelledi."
            print(json.dumps(output, ensure_ascii=False))
            return 1 if challenge_detected else 0
    except PlaywrightTimeoutError as exc:
        print(json.dumps({"ok": False, "error": f"Timeout: {exc}"}, ensure_ascii=False))
        return 1
    except Exception as exc:
        print(json.dumps({"ok": False, "error": str(exc)}, ensure_ascii=False))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
)PY";
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

}  // namespace

WebBrowserTool::WebBrowserTool()
    : definition_({
          "WebBrowserTool",
          "Playwright ile gercek bir Chromium sayfasi acar; gitme, tiklama, yazma, bekleme, ekran goruntusu alma ve metin okuma gibi cok adimli web islemleri yapar.",
          {
              {"type", "object"},
              {"properties", {
                  {"steps", {
                      {"type", "array"},
                      {"description", "Her biri action alanina sahip web adimlari. Ornek action degerleri: goto, click, type, fill, press, hover, wait_for_selector, wait_for_timeout, extract_text, snapshot, screenshot, select."}
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
                  {"visionDetail", {{"type", "string"}}}
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

    LogBrowserMessage("execute", "Web islemi basladi.");
    LogBrowserMessage("execute", "rootPath=" + rootPath.string());
    LogBrowserMessage("execute", "venvPath=" + venvPath.string());
    LogBrowserMessage("execute", "browsersPath=" + browsersPath.string());
    LogBrowserMessage("execute", "artifactsPath=" + artifactsPath.string());
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
    const ToolResult playwrightReadyResult = EnsurePlaywrightReady(pythonExecutable, browsersPath);
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
        {"locale", call.arguments.value("locale", "tr-TR")}
    };

    TemporaryFile configFile("voice_agent_browser_config_", ".json", config.dump(2));
    TemporaryFile scriptFile("voice_agent_browser_runner_", ".py", BuildRunnerScript());

    LogBrowserMessage("execute", "configFile=" + configFile.Path());
    LogBrowserMessage("execute", "scriptFile=" + scriptFile.Path());

    const std::string runCommand =
        "PLAYWRIGHT_BROWSERS_PATH=" + ShellEscape(browsersPath.string()) + " " +
        ShellEscape(pythonExecutable) + " -u " +
        ShellEscape(scriptFile.Path()) + " " +
        ShellEscape(configFile.Path());
    const CommandExecution runResult = RunCommand(runCommand, "run_browser_script");
    if (!runResult.launched) {
        return ToolResult{false, false, runResult.output, {{"stage", "run_browser_script"}}};
    }

    const std::string jsonLine = ExtractLastJsonLine(runResult.output);
    const nlohmann::json payload = nlohmann::json::parse(jsonLine, nullptr, false);
    if (runResult.exitCode != 0) {
        if (!payload.is_discarded() && payload.is_object()) {
            if (payload.value("challengeDetected", false)) {
                return BuildChallengeResult(
                    payload,
                    runCommand,
                    runResult.exitCode,
                    artifactsPath,
                    venvPath,
                    pythonExecutable,
                    call.arguments
                );
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
            payload,
            runCommand,
            runResult.exitCode,
            artifactsPath,
            venvPath,
            pythonExecutable,
            call.arguments
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
    result.imageAttachments = BuildImageAttachments(payload, call.arguments);
    return result;
}

}  // namespace voice_agent