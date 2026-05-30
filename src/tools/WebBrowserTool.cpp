#include "tools/WebBrowserTool.h"

#include "common/StringUtils.h"

#include <algorithm>
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

std::string BuildRunnerScript() {
    return R"PY(
import asyncio
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

from playwright.async_api import TimeoutError as PlaywrightTimeoutError
from playwright.async_api import async_playwright

SKIP_COPY_NAMES = {
    "Cache", "Code Cache", "GPUCache", "GrShaderCache", "GraphiteDawnCache",
    "ShaderCache", "DawnGraphiteCache", "Crashpad", "BrowserMetrics",
    "BrowserMetrics-spare.pma", "SingletonCookie", "SingletonLock",
    "SingletonSocket", "LOCK", "lockfile",
}


def log(msg):
    print(f"[runner] {msg}", flush=True)


def normalize_profile_value(value):
    return re.sub(r"[^a-z0-9]+", "", str(value).casefold())


def get_default_chrome_user_data_dir():
    if env_path := os.environ.get("CHROME_USER_DATA_DIR"):
        return Path(env_path)
    if os.name == "nt":
        local_app_data = os.environ.get("LOCALAPPDATA")
        if local_app_data:
            return Path(local_app_data) / "Google/Chrome/User Data"
    return Path.home() / ".config/google-chrome"


def get_default_chrome_executable_path():
    if env_path := os.environ.get("CHROME_EXECUTABLE_PATH"):
        return env_path
    for candidate in ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser"):
        path = shutil.which(candidate)
        if path:
            return path
    if os.name == "nt":
        candidates = [
            Path(os.environ.get("PROGRAMFILES", "")) / "Google/Chrome/Application/chrome.exe",
            Path(os.environ.get("PROGRAMFILES(X86)", "")) / "Google/Chrome/Application/chrome.exe",
            Path(os.environ.get("LOCALAPPDATA", "")) / "Google/Chrome/Application/chrome.exe",
        ]
        for candidate in candidates:
            if str(candidate) and candidate.exists():
                return str(candidate)
    return None


def get_available_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def load_profile_metadata(user_data_dir):
    local_state_path = Path(user_data_dir) / "Local State"
    if not local_state_path.exists():
        return {}
    try:
        data = json.loads(local_state_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return data.get("profile", {}).get("info_cache", {})


def resolve_chrome_profile_dir(user_data_dir, profile_hint):
    if env_dir := os.environ.get("CHROME_PROFILE_DIR"):
        return env_dir
    profiles = load_profile_metadata(user_data_dir)
    if not profiles:
        return "Default"
    hint = normalize_profile_value(profile_hint or "")
    if hint:
        exact_p, partial_p, exact_u, partial_u = [], [], [], []
        for directory, metadata in profiles.items():
            pn = normalize_profile_value(metadata.get("name", ""))
            un = normalize_profile_value(metadata.get("user_name", ""))
            if hint == pn: exact_p.append(directory)
            elif hint in pn: partial_p.append(directory)
            elif hint == un: exact_u.append(directory)
            elif hint in un: partial_u.append(directory)
        for matches in (exact_p, partial_p, exact_u, partial_u):
            if matches:
                return matches[0]
    return "Default"


def copy_profile_tree(source_dir, target_dir):
    if target_dir.exists():
        shutil.rmtree(target_dir)
    def ignore_entries(_, names):
        return [n for n in names if n in SKIP_COPY_NAMES]
    shutil.copytree(source_dir, target_dir, ignore=ignore_entries)


def prepare_automation_profile(source_user_data_dir, profile_dir, target_user_data_dir, refresh):
    source_resolved = source_user_data_dir.resolve()
    target_resolved = target_user_data_dir.resolve()
    if source_resolved == target_resolved:
        return source_user_data_dir

    target_profile_dir = target_user_data_dir / profile_dir
    target_preferences = target_profile_dir / "Preferences"
    if not refresh and target_preferences.exists():
        log(f"Mevcut otomasyon profili kullaniliyor: {target_profile_dir}")
        return target_user_data_dir

    target_user_data_dir.mkdir(parents=True, exist_ok=True)
    local_state_path = source_user_data_dir / "Local State"
    if local_state_path.exists():
        shutil.copy2(local_state_path, target_user_data_dir / "Local State")

    source_profile_dir = source_user_data_dir / profile_dir
    if not source_profile_dir.exists():
        raise FileNotFoundError(f"Chrome profile not found: {source_profile_dir}")

    log(f"Chrome profili kopyalaniyor: {source_profile_dir} -> {target_profile_dir}")
    copy_profile_tree(source_profile_dir, target_profile_dir)
    return target_user_data_dir


def launch_debug_chrome(executable, user_data_dir, profile_dir, port, headless):
    args = [
        executable,
        f"--remote-debugging-port={port}",
        f"--user-data-dir={user_data_dir}",
        f"--profile-directory={profile_dir}",
        "--no-first-run",
        "--no-default-browser-check",
        "--disable-blink-features=AutomationControlled",
    ]
    if headless:
        args.append("--headless=new")
    args.append("about:blank")
    return subprocess.Popen(args)


def wait_for_cdp_endpoint(port, timeout=25):
    deadline = time.monotonic() + timeout
    endpoint = f"http://127.0.0.1:{port}/json/version"
    last_error = None
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(endpoint, timeout=1) as response:
                if response.status == 200:
                    return
        except Exception as exc:
            last_error = exc
            time.sleep(0.5)
    raise TimeoutError(f"Chrome CDP endpoint was not ready: {last_error}")


def stop_chrome_process(proc):
    if not proc or proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


# ---------- Locator / human-like helpers ----------

def shorten(text, limit=4000):
    text = (text or "").strip()
    if len(text) <= limit:
        return text
    return text[:limit] + "\n[truncated]"


def _build_locator(page, spec):
    if spec.get("selector"):
        return page.locator(spec["selector"])
    if spec.get("role"):
        kwargs = {}
        if spec.get("name") is not None:
            kwargs["name"] = spec["name"]
        if spec.get("exact") is not None:
            kwargs["exact"] = bool(spec["exact"])
        return page.get_by_role(spec["role"], **kwargs)
    if spec.get("textSelector"):
        return page.get_by_text(spec["textSelector"], exact=bool(spec.get("exact", False)))
    if spec.get("label"):
        return page.get_by_label(spec["label"], exact=bool(spec.get("exact", False)))
    if spec.get("placeholder"):
        return page.get_by_placeholder(spec["placeholder"])
    if spec.get("altText"):
        return page.get_by_alt_text(spec["altText"])
    if spec.get("titleSelector"):
        return page.get_by_title(spec["titleSelector"])
    raise ValueError("Locator spec requires selector / role / textSelector / label / placeholder / altText / titleSelector")


def _step_to_specs(step):
    """Return list of locator-spec dicts. Supports inline keys or `selectors` array fallback."""
    specs = []
    if isinstance(step.get("selectors"), list):
        for entry in step["selectors"]:
            if isinstance(entry, str):
                specs.append({"selector": entry})
            elif isinstance(entry, dict):
                specs.append(entry)
    keys = ("selector", "role", "textSelector", "label", "placeholder", "altText", "titleSelector")
    if any(step.get(k) for k in keys):
        primary = {k: step.get(k) for k in keys if step.get(k) is not None}
        if step.get("name") is not None:
            primary["name"] = step.get("name")
        if step.get("exact") is not None:
            primary["exact"] = step.get("exact")
        specs.append(primary)
    return specs


def resolve_locator(page, step):
    specs = _step_to_specs(step)
    if not specs:
        raise ValueError("Step requires a locator")
    return _build_locator(page, specs[0])


def maybe_locator(page, step):
    specs = _step_to_specs(step)
    if not specs:
        return None
    return _build_locator(page, specs[0])


async def click_first(page, specs, *, force=False, timeout=3000, delay=50):
    for spec in specs:
        try:
            loc = _build_locator(page, spec).first
        except Exception:
            continue
        try:
            if await loc.count() == 0:
                continue
            await loc.click(timeout=timeout, force=force, delay=delay)
            return True
        except Exception:
            continue
    return False


async def wait_for_clickable(page, specs, timeout=8000):
    deadline = asyncio.get_running_loop().time() + (timeout / 1000)
    while asyncio.get_running_loop().time() < deadline:
        for spec in specs:
            try:
                loc = _build_locator(page, spec).first
                if await loc.count() == 0:
                    continue
                if not await loc.is_visible():
                    continue
                disabled = await loc.get_attribute("disabled")
                aria_disabled = await loc.get_attribute("aria-disabled")
                if disabled is None and aria_disabled not in {"true", "True"}:
                    return True
            except Exception:
                continue
        await page.wait_for_timeout(250)
    return False


async def click_close_button(page):
    selectors = [
        "button[aria-label='Close']", "button[aria-label='close']",
        "button[title='Close']", "button[title='close']",
        "[data-testid='close']", "[data-testid*='close']",
        "button:has-text('×')", "button:has-text('✕')", "button:has-text('X')",
        "[role='button']:has-text('×')", "[role='button']:has-text('✕')", "[role='button']:has-text('X')",
        "svg.lucide-x", "button:has(svg)",
    ]
    specs = [{"selector": s} for s in selectors]
    if await click_first(page, specs, timeout=2000):
        return True
    if await click_first(page, specs, force=True, timeout=2000):
        return True
    try:
        viewport = page.viewport_size
        if viewport:
            await page.mouse.click(viewport["width"] - 28, 26)
            return True
    except Exception:
        pass
    try:
        width = await page.evaluate("window.innerWidth")
        await page.mouse.click(width - 28, 26)
        return True
    except Exception:
        return False


def detect_bot_challenge(page_title, page_text, page_url):
    title = (page_title or "").strip().lower()
    text = (page_text or "").strip().lower()
    url = (page_url or "").strip().lower()
    signals = [
        "just a moment", "bir dakika lütfen", "güvenlik doğrulaması yapılıyor",
        "security check", "verify you are human", "checking your browser",
        "cloudflare", "bot olmadığınızı doğrularken",
    ]
    if "__cf_chl" in url:
        return True, "cloudflare_challenge_url"
    combined = "\n".join([title, text])
    for signal in signals:
        if signal in combined:
            return True, signal
    return False, ""


# ---------- Step executor ----------

async def execute_step(page, step, index, artifacts_dir):
    action = step.get("action")
    if not action:
        raise ValueError("Each step requires an action field")

    log(f"step {index + 1} action={action}")
    entry = {"index": index, "action": action}
    specs = _step_to_specs(step)

    if action in ("goto", "navigate"):
        response = await page.goto(step["url"], wait_until=step.get("waitUntil", "domcontentloaded"))
        if step.get("loadState"):
            await page.wait_for_load_state(step["loadState"])
        entry["status"] = response.status if response else None
    elif action == "click":
        if not specs:
            raise ValueError("click action requires a locator")
        clicked = await click_first(
            page, specs,
            force=bool(step.get("force", False)),
            timeout=int(step.get("timeoutMs", 5000)),
            delay=int(step.get("delayMs", 50)),
        )
        if not clicked and step.get("requireSuccess", True):
            raise RuntimeError(f"click failed for step {index + 1}")
        entry["clicked"] = clicked
    elif action == "click_first":
        if not specs:
            raise ValueError("click_first action requires selectors")
        clicked = await click_first(
            page, specs,
            force=bool(step.get("force", False)),
            timeout=int(step.get("timeoutMs", 5000)),
            delay=int(step.get("delayMs", 50)),
        )
        if not clicked and step.get("requireSuccess", False):
            raise RuntimeError(f"click_first failed for step {index + 1}")
        entry["clicked"] = clicked
    elif action == "click_close":
        entry["clicked"] = await click_close_button(page)
    elif action == "wait_for_clickable":
        if not specs:
            raise ValueError("wait_for_clickable action requires a locator")
        ready = await wait_for_clickable(page, specs, timeout=int(step.get("timeoutMs", 8000)))
        entry["ready"] = ready
        if not ready and step.get("requireSuccess", True):
            raise RuntimeError(f"wait_for_clickable timed out for step {index + 1}")
    elif action == "hover":
        loc = resolve_locator(page, step)
        await loc.hover()
    elif action == "type":
        loc = resolve_locator(page, step)
        if step.get("clear", True):
            try:
                await loc.click()
                await loc.fill("")
            except Exception:
                pass
        await loc.type(step.get("text", ""), delay=int(step.get("delayMs", 60)))
    elif action == "fill":
        loc = resolve_locator(page, step)
        await loc.fill(step.get("text", ""))
    elif action == "press":
        key = step.get("key")
        if not key:
            raise ValueError("press action requires key")
        loc = maybe_locator(page, step)
        if loc is None:
            await page.keyboard.press(key)
        else:
            await loc.press(key)
    elif action == "select":
        loc = resolve_locator(page, step)
        kwargs = {}
        if "value" in step: kwargs["value"] = step["value"]
        if "labelValue" in step: kwargs["label"] = step["labelValue"]
        if "indexValue" in step: kwargs["index"] = int(step["indexValue"])
        await loc.select_option(**kwargs)
    elif action == "wait_for_selector":
        loc = resolve_locator(page, step)
        await loc.wait_for(state=step.get("state", "visible"), timeout=step.get("timeoutMs"))
    elif action == "wait_for_load_state":
        await page.wait_for_load_state(step.get("state", "networkidle"), timeout=step.get("timeoutMs", 10000))
    elif action == "wait_for_timeout":
        await page.wait_for_timeout(int(step.get("timeoutMs", 1000)))
    elif action == "extract_text":
        loc = maybe_locator(page, step) or page.locator("body")
        entry["text"] = shorten(await loc.inner_text(), int(step.get("maxLength", 4000)))
    elif action == "extract_attribute":
        loc = resolve_locator(page, step)
        entry["value"] = await loc.get_attribute(step.get("attribute", "value"))
    elif action == "snapshot":
        entry["text"] = shorten(await page.locator("body").inner_text(), int(step.get("maxLength", 4000)))
        entry["htmlLength"] = len(await page.content())
    elif action == "screenshot":
        path = artifacts_dir / step.get("path", f"step_{index + 1}.png")
        path.parent.mkdir(parents=True, exist_ok=True)
        await page.screenshot(path=str(path), full_page=bool(step.get("fullPage", True)))
        entry["path"] = str(path)
    elif action == "evaluate":
        script = step.get("script")
        if not script:
            raise ValueError("evaluate action requires script")
        result = await page.evaluate(script)
        if isinstance(result, (str, int, float, bool, list, dict)) or result is None:
            entry["result"] = result
        else:
            entry["result"] = str(result)
    elif action == "mouse_click":
        await page.mouse.click(int(step["x"]), int(step["y"]))
    elif action == "keyboard_press":
        key = step.get("key")
        if not key:
            raise ValueError("keyboard_press action requires key")
        await page.keyboard.press(key)
    elif action == "scroll":
        await page.evaluate(
            "([x, y]) => window.scrollTo(x, y)",
            [int(step.get("x", 0)), int(step.get("y", 0))],
        )
    else:
        raise ValueError(f"Unsupported action: {action}")

    try:
        entry["url"] = page.url
        entry["title"] = await page.title()
    except Exception:
        pass
    log(f"finished step {index + 1} action={action}")
    return entry


# ---------- Main ----------

async def run_with_config(config):
    steps = config["steps"]
    artifacts_dir = Path(config["artifactsDir"])
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    log(f"step count: {len(steps)} artifacts={artifacts_dir}")

    use_chrome_profile = bool(config.get("useChromeProfile", False))
    headless = bool(config.get("headless", True))
    timeout_ms = int(config.get("timeoutMs", 15000))
    viewport_w = int(config.get("viewportWidth", 1365))
    viewport_h = int(config.get("viewportHeight", 900))
    locale = config.get("locale", "tr-TR")
    slow_mo = int(config.get("slowMoMs", 0))

    chrome_process = None
    browser = None
    context = None
    own_browser = False

    async with async_playwright() as p:
        if use_chrome_profile:
            chrome_exe = config.get("chromeExecutablePath") or get_default_chrome_executable_path()
            source_user_data_dir = Path(config.get("chromeUserDataDir") or get_default_chrome_user_data_dir())
            profile_dir = config.get("chromeProfileDir") or resolve_chrome_profile_dir(
                source_user_data_dir, config.get("chromeProfileName", "")
            )
            automation_user_data_dir = Path(
                config.get("automationUserDataDir") or (Path.cwd() / ".voice_agent_browser" / "chrome-profile")
            )
            try:
                launch_user_data_dir = prepare_automation_profile(
                    source_user_data_dir, profile_dir, automation_user_data_dir,
                    refresh=bool(config.get("refreshProfileCopy", False)),
                )
            except FileNotFoundError as exc:
                log(f"profile copy skipped: {exc}; using fresh user-data-dir")
                automation_user_data_dir.mkdir(parents=True, exist_ok=True)
                launch_user_data_dir = automation_user_data_dir

            common_args = [
                "--disable-blink-features=AutomationControlled",
                "--no-first-run",
                "--no-default-browser-check",
            ]

            if chrome_exe:
                # Path A: system Chrome over CDP (closest to a real user).
                port = int(config.get("chromeDebugPort") or 0) or get_available_port()
                log(f"chrome exe={chrome_exe} udd={launch_user_data_dir} profile={profile_dir} port={port} headless={headless} (CDP)")
                chrome_process = launch_debug_chrome(chrome_exe, launch_user_data_dir, profile_dir, port, headless)
                await asyncio.to_thread(wait_for_cdp_endpoint, port, 25)
                browser = await p.chromium.connect_over_cdp(f"http://127.0.0.1:{port}")
                own_browser = False
                context = browser.contexts[0] if browser.contexts else await browser.new_context()
                page = context.pages[0] if context.pages else await context.new_page()
            else:
                # Path B: no system Chrome -> use bundled Chromium with the copied profile.
                persistent_dir = launch_user_data_dir / profile_dir
                if not persistent_dir.exists():
                    persistent_dir = launch_user_data_dir
                log(f"system chrome bulunamadi; bundled chromium + persistent profil kullaniliyor: {persistent_dir}")
                channel = config.get("chromeChannel") or None
                context = await p.chromium.launch_persistent_context(
                    user_data_dir=str(persistent_dir),
                    headless=headless,
                    channel=channel,
                    args=common_args,
                    viewport={"width": viewport_w, "height": viewport_h},
                    locale=locale,
                    slow_mo=slow_mo,
                )
                browser = context.browser
                own_browser = True
                page = context.pages[0] if context.pages else await context.new_page()
        else:
            log(f"launching bundled chromium headless={headless} slowMo={slow_mo}")
            browser = await p.chromium.launch(headless=headless, slow_mo=slow_mo)
            own_browser = True
            storage_state = config.get("storageStatePath")
            if storage_state and not Path(storage_state).exists():
                storage_state = None
            context = await browser.new_context(
                viewport={"width": viewport_w, "height": viewport_h},
                locale=locale,
                storage_state=storage_state,
            )
            page = await context.new_page()

        page.set_default_timeout(timeout_ms)

        results = []
        try:
            for index, step in enumerate(steps):
                entry = await execute_step(page, step, index, artifacts_dir)
                results.append(entry)

            final_screenshot_path = artifacts_dir / "final.png"
            try:
                await page.screenshot(path=str(final_screenshot_path), full_page=True)
                final_screenshot = str(final_screenshot_path)
            except Exception as exc:
                log(f"final screenshot failed: {exc}")
                final_screenshot = ""

            try:
                page_title = await page.title()
            except Exception:
                page_title = ""
            try:
                page_text = await page.locator("body").inner_text()
            except Exception:
                page_text = ""
            page_url = page.url
            challenge_detected, challenge_reason = detect_bot_challenge(page_title, page_text, page_url)

            output = {
                "ok": not challenge_detected,
                "url": page_url,
                "title": page_title,
                "results": results,
                "artifactsDir": str(artifacts_dir),
                "finalScreenshot": final_screenshot,
                "challengeDetected": challenge_detected,
                "challengeReason": challenge_reason,
            }
            if challenge_detected:
                output["error"] = "Bot korumasi sayfasi tespit edildi."

            save_state = config.get("saveStorageStatePath")
            if save_state and own_browser:
                try:
                    await context.storage_state(path=save_state)
                    output["storageStatePath"] = save_state
                except Exception as exc:
                    log(f"storage_state save failed: {exc}")

            print(json.dumps(output, ensure_ascii=False))
            return 1 if challenge_detected else 0
        finally:
            try:
                if browser is not None:
                    await browser.close()
            except Exception:
                pass
            stop_chrome_process(chrome_process)


def main():
    config_path = Path(sys.argv[1])
    config = json.loads(config_path.read_text())
    try:
        return asyncio.run(run_with_config(config))
    except PlaywrightTimeoutError as exc:
        print(json.dumps({"ok": False, "error": f"Timeout: {exc}"}, ensure_ascii=False))
        return 1
    except Exception as exc:
        print(json.dumps({"ok": False, "error": f"{type(exc).__name__}: {exc}"}, ensure_ascii=False))
        return 1


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
          "Playwright ile gercek bir Chromium sayfasi acar; gitme, tiklama, yazma, bekleme, ekran goruntusu alma ve metin okuma gibi cok adimli web islemleri yapar. useChromeProfile=true verildiginde sistem Chrome'u, kullanicinin profili (izole bir kopyaya alinarak) uzerinden CDP ile surulur; bot korumalarini insan gibi gecebilir.",
          {
              {"type", "object"},
              {"properties", {
                  {"steps", {
                      {"type", "array"},
                      {"description", "Her biri 'action' alanli web adimlari. Action: goto, click, click_first, click_close, type, fill, press, hover, wait_for_selector, wait_for_clickable, wait_for_load_state, wait_for_timeout, extract_text, extract_attribute, snapshot, screenshot, select, evaluate, mouse_click, keyboard_press, scroll. Locator icin selector / role+name / textSelector / label / placeholder / altText / titleSelector kullanilabilir; ayrica selectors dizisi ile fallback verilebilir. force, delayMs, timeoutMs, requireSuccess gibi alanlar opsiyoneldir."}
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

ToolResult WebBrowserTool::Execute(const ToolCall& call, const CancellationToken* /*token*/) const {
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
    if (call.arguments.contains("chromeDebugPort") && call.arguments.at("chromeDebugPort").is_number_integer()) {
        config["chromeDebugPort"] = call.arguments.at("chromeDebugPort").get<int>();
    }
    if (call.arguments.contains("refreshProfileCopy") && call.arguments.at("refreshProfileCopy").is_boolean()) {
        config["refreshProfileCopy"] = call.arguments.at("refreshProfileCopy").get<bool>();
    }

    TemporaryFile configFile("voice_agent_browser_config_", ".json", config.dump(2));
    TemporaryFile scriptFile("voice_agent_browser_runner_", ".py", BuildRunnerScript());

    LogBrowserMessage("execute", "configFile=" + configFile.Path());
    LogBrowserMessage("execute", "scriptFile=" + scriptFile.Path());

    const std::string envPrefix =
        "PLAYWRIGHT_BROWSERS_PATH=" + ShellEscape(browsersPath.string()) + " ";

    const std::string runCommand =
        envPrefix +
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
                    payload, runCommand, runResult.exitCode,
                    artifactsPath, venvPath, pythonExecutable, call.arguments
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
    result.imageAttachments = BuildImageAttachments(payload, call.arguments);
    return result;
}

}  // namespace voice_agent
