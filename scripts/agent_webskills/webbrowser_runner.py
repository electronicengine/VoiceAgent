import random
import asyncio
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path

from playwright.async_api import TimeoutError as PlaywrightTimeoutError
from playwright.async_api import async_playwright
try:
    import playwright_stealth
    STEALTH_AVAILABLE = True
except ImportError:
    STEALTH_AVAILABLE = False

EVENT_PREFIX = "__VA_EVENT__"
_PROMPT_LOCK = threading.Lock()
_PROMPT_COUNTER = 0


def emit_event(payload):
    """Emit a control event line to stdout. The C++ host recognises the prefix
    and never confuses these lines with the final result JSON line."""
    print(EVENT_PREFIX + " " + json.dumps(payload, ensure_ascii=False), flush=True)


def _next_prompt_id():
    global _PROMPT_COUNTER
    with _PROMPT_LOCK:
        _PROMPT_COUNTER += 1
        return f"prompt-{_PROMPT_COUNTER}"


async def request_user_prompt(question, *, timeout_seconds=180, mode="text"):
    """Ask the host (C++ side) to collect an answer from the human user.
    Emits an event line, then reads a single JSON response line from stdin.
    Returns the dict {ok, cancelled, timedOut, answer, error} (subset)."""
    prompt_id = _next_prompt_id()
    emit_event({
        "type": "prompt",
        "id": prompt_id,
        "question": question,
        "timeoutSeconds": int(timeout_seconds),
        "mode": mode,
    })
    loop = asyncio.get_running_loop()
    line = await loop.run_in_executor(None, sys.stdin.readline)
    if not line:
        return {"ok": False, "cancelled": True, "error": "stdin closed"}
    try:
        return json.loads(line.strip())
    except json.JSONDecodeError as exc:
        return {"ok": False, "error": f"bad prompt response: {exc}"}

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

    config_root = Path.home() / ".config"
    google_chrome_dir = config_root / "google-chrome"
    chromium_dir = config_root / "chromium"

    executable = get_default_chrome_executable_path() or ""
    if "chromium" in executable and chromium_dir.exists():
        return chromium_dir
    if google_chrome_dir.exists():
        return google_chrome_dir
    if chromium_dir.exists():
        return chromium_dir
    return google_chrome_dir

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
        "--disable-infobars",
        "--disable-extensions",
        "--lang=tr-TR",
    ]
    if headless:
        args.append("--headless=new")
    args.append("about:blank")
    return subprocess.Popen(args)

def chrome_profile_lock_paths(user_data_dir):
    base_dir = Path(user_data_dir)
    return [base_dir / name for name in ("SingletonLock", "SingletonSocket", "SingletonCookie", "LOCK", "lockfile")]

def has_running_chrome_with_user_data_dir(user_data_dir):
    try:
        result = subprocess.run(
            ["ps", "-eo", "pid=,args="],
            check=False,
            capture_output=True,
            text=True,
        )
    except Exception:
        return False

    expected = str(Path(user_data_dir).resolve())
    for line in result.stdout.splitlines():
        if expected and f"--user-data-dir={expected}" in line:
            return True
    return False

def remove_stale_chrome_profile_locks(lock_paths):
    for path in lock_paths:
        try:
            if path.is_symlink() or path.is_file():
                path.unlink()
        except FileNotFoundError:
            continue
        except OSError as exc:
            log(f"stale chrome lock cleanup failed for {path}: {exc}")

def ensure_chrome_profile_closed(user_data_dir):
    lock_paths = [path for path in chrome_profile_lock_paths(user_data_dir) if path.exists()]
    active_process = has_running_chrome_with_user_data_dir(user_data_dir)
    if not active_process and lock_paths:
        log(f"stale Chrome profile locks detected; cleaning up: {', '.join(str(path) for path in lock_paths)}")
        remove_stale_chrome_profile_locks(lock_paths)
        lock_paths = [path for path in chrome_profile_lock_paths(user_data_dir) if path.exists()]

    if lock_paths or active_process:
        joined = ", ".join(str(path) for path in lock_paths) if lock_paths else "active Chrome process"
        raise RuntimeError(
            "Chrome profili zaten kullaniliyor. Bu ortak profili agent ile kullanmadan once Google Chrome'u kapatin. "
            f"Tespit: {joined}"
        )

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

def detect_available_display():
    display = (os.environ.get("DISPLAY") or "").strip()
    if display:
        return display

    display_num = (os.environ.get("DISPLAY_NUM") or "").strip()
    if display_num:
        candidate = display_num if display_num.startswith(":") else f":{display_num}"
        if Path(f"/tmp/.X11-unix/X{candidate.lstrip(':')}").exists():
            return candidate

    for candidate in (":99", ":100", ":1", ":0"):
        if Path(f"/tmp/.X11-unix/X{candidate.lstrip(':')}").exists():
            return candidate
    return ""
# ---------- Locator / human-like helpers ----------

def shorten(text, limit=4000):
    text = (text or "").strip()
    if len(text) <= limit:
        return text
    return text[:limit] + "\n[truncated]"


def describe_spec(spec):
    if spec.get("selector"):
        return f"selector={spec['selector']}"
    if spec.get("role"):
        name = spec.get("name")
        return f"role={spec['role']} name={name}" if name is not None else f"role={spec['role']}"
    for key in ("textSelector", "label", "placeholder", "altText", "titleSelector"):
        if spec.get(key):
            return f"{key}={spec[key]}"
    return str(spec)


async def snapshot_page_state(page):
    try:
        title = await page.title()
    except Exception:
        title = ""
    try:
        url = page.url
    except Exception:
        url = ""
    try:
        body_text = await page.locator("body").inner_text()
        body_preview = shorten(body_text, 300).replace("\n", " | ")
    except Exception:
        body_preview = ""
    return {
        "url": url,
        "title": title,
        "bodyPreview": body_preview,
    }


def _build_locator(page, spec):
    if spec.get("selector"):
        return page.locator(spec["selector"])
    if spec.get("role"):
        kwargs = {}
        if spec.get("name") is not None:
            kwargs["name"] = spec["name"]
        if spec.get("exact") is not None:
            kwargs["exact"] = bool(spec.get("exact"))
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
        spec_desc = describe_spec(spec)
        try:
            loc = _build_locator(page, spec).first
        except Exception:
            log(f"click_first: invalid locator spec: {spec_desc}")
            continue
        try:
            if await loc.count() == 0:
                log(f"click_first: no match for {spec_desc}")
                continue
            await loc.click(timeout=timeout, force=force, delay=delay)
            log(f"click_first: clicked {spec_desc} force={force} timeout={timeout}")
            return True
        except Exception as exc:
            log(f"click_first: failed {spec_desc}: {exc}")
            continue
    return False


async def wait_for_clickable(page, specs, timeout=8000):
    deadline = asyncio.get_running_loop().time() + (timeout / 1000)
    while asyncio.get_running_loop().time() < deadline:
        for spec in specs:
            spec_desc = describe_spec(spec)
            try:
                loc = _build_locator(page, spec).first
                if await loc.count() == 0:
                    continue
                if not await loc.is_visible():
                    continue
                disabled = await loc.get_attribute("disabled")
                aria_disabled = await loc.get_attribute("aria-disabled")
                if disabled is None and aria_disabled not in {"true", "True"}:
                    log(f"wait_for_clickable: ready {spec_desc}")
                    return True
            except Exception:
                continue
        await page.wait_for_timeout(250)
    return False


async def wait_for_selector_any(page, specs, *, state="visible", timeout=8000):
    deadline = asyncio.get_running_loop().time() + (timeout / 1000)
    while asyncio.get_running_loop().time() < deadline:
        for spec in specs:
            spec_desc = describe_spec(spec)
            try:
                loc = _build_locator(page, spec).first
                count = await loc.count()
                if state in ("attached", "visible") and count == 0:
                    continue
                if state == "visible" and not await loc.is_visible():
                    continue
                if state == "hidden" and (count == 0 or not await loc.is_visible()):
                    log(f"wait_for_selector_any: matched hidden {spec_desc}")
                    return spec_desc
                if state == "detached" and count == 0:
                    log(f"wait_for_selector_any: matched detached {spec_desc}")
                    return spec_desc
                if state in ("attached", "visible"):
                    log(f"wait_for_selector_any: matched {spec_desc} state={state}")
                    return spec_desc
            except Exception:
                continue
        await page.wait_for_timeout(250)
    return ""


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


# Common cookie / consent / popup acceptance text in TR + EN + a few EU languages.
CONSENT_TEXT_PATTERNS = [
    "kabul et", "tümünü kabul", "tumunu kabul", "hepsini kabul",
    "izin ver", "onayla", "anladım", "anladim", "tamam", "devam et",
    "accept all", "accept cookies", "accept", "agree", "i agree", "ok",
    "got it", "allow all", "allow cookies", "allow", "i accept", "consent",
    "alle akzeptieren", "akzeptieren", "tout accepter", "accepter",
    "aceptar todo", "aceptar", "accetta tutti", "accetto",
    "aceitar tudo", "aceitar",
]

CONSENT_SELECTOR_HINTS = [
    "#onetrust-accept-btn-handler",
    "button#onetrust-accept-btn-handler",
    "button[aria-label*='Accept' i]",
    "button[aria-label*='Kabul' i]",
    "button[aria-label*='Cookie' i]",
    "button[aria-label*='Çerez' i]",
    "button[aria-label*='Cerez' i]",
    "button[id*='accept' i]",
    "button[id*='cookie' i]",
    "button[id*='consent' i]",
    "button[class*='accept' i]",
    "button[class*='consent' i]",
    "button[class*='cookie' i]",
    "button[data-testid*='accept' i]",
    "button[data-testid*='consent' i]",
    "button[data-testid*='cookie' i]",
    "[role='button'][aria-label*='Accept' i]",
    "[role='button'][aria-label*='Kabul' i]",
    "div[role='dialog'] button",
    "[class*='cookie'] button",
    "[class*='consent'] button",
    "[id*='cookie'] button",
    "[id*='consent'] button",
]

# Common location/notification permission popup buttons that block content.
DISMISS_TEXT_PATTERNS = [
    "daha sonra", "şimdi değil", "simdi degil", "vazgeç", "vazgec",
    "kapat", "hayır", "hayir", "reddet",
    "not now", "no thanks", "maybe later", "later", "decline",
    "dismiss", "close",
]

ACCESSIBILITY_CONTROL_PATTERNS = [
    "skip to ",
    "skip navigation",
    "skip content",
    "main content",
    "primary content",
    "sidebar",
    "search",
    "jump menu",
]

COOKIE_MANAGEMENT_PATTERNS = [
    "manage cookies",
    "manage cookie",
    "cookie preferences",
    "manage cookie preferences",
    "cookie settings",
    "privacy statement",
]


def is_accessibility_control_text(text):
    normalized = (text or "").strip().casefold()
    if not normalized:
        return False
    if normalized.startswith("skip to "):
        return True
    return any(pattern in normalized for pattern in ACCESSIBILITY_CONTROL_PATTERNS)


def is_cookie_management_text(text):
    normalized = (text or "").strip().casefold()
    if not normalized:
        return False
    return any(pattern in normalized for pattern in COOKIE_MANAGEMENT_PATTERNS)


async def any_selector_visible(page, selector_list, timeout_ms=4000):
    selectors = [selector.strip() for selector in (selector_list or "").split(",") if selector.strip()]
    if not selectors:
        return False, ""

    deadline = asyncio.get_running_loop().time() + (timeout_ms / 1000)
    while asyncio.get_running_loop().time() < deadline:
        for selector in selectors:
            try:
                locator = page.locator(selector)
                count = await locator.count()
                if count == 0:
                    continue
                for index in range(min(count, 5)):
                    candidate = locator.nth(index)
                    if await candidate.is_visible():
                        return True, selector
            except Exception:
                continue
        await page.wait_for_timeout(200)
    return False, ""


def is_probable_generic_logged_out_page(page_title, page_text, page_url, login_url=""):
    title = (page_title or "").casefold()
    text = (page_text or "").casefold()
    url = (page_url or "").casefold()
    login_url = (login_url or "").casefold()

    url_signals = [
        "/login",
        "/signin",
        "/sign-in",
        "/register",
        "/signup",
        "/sign-up",
        "/auth",
    ]
    if any(signal in url for signal in url_signals):
        return True
    if login_url and url == login_url:
        return True

    combined = "\n".join([title, text])
    strong_auth_signals = [
        "forgot password",
        "create your account",
        "already have an account",
        "continue with google",
        "continue with github",
        "or sign up with",
        "or continue with",
        "keep me signed in",
        "stay signed in",
        "trouble signing in",
    ]
    if any(signal in combined for signal in strong_auth_signals):
        return True

    auth_action_signals = [
        "sign in",
        "log in",
        "login",
        "sign up",
        "signup",
        "create account",
    ]
    credential_signals = [
        "email",
        "e-mail",
        "work email",
        "phone",
        "password",
        "passkey",
        "show password",
    ]
    has_auth_action = any(signal in combined for signal in auth_action_signals)
    has_credential_prompt = any(signal in combined for signal in credential_signals)
    return has_auth_action and has_credential_prompt


async def _click_buttons_matching_text(page, patterns, timeout_ms=1500):
    clicked_any = False
    try:
        # Search across all frames; click buttons / role=button elements whose
        # visible text contains any of the patterns (case-insensitive).
        for frame in page.frames:
            try:
                handles = await frame.query_selector_all(
                    "button, [role='button'], a[role='button'], input[type='button'], input[type='submit']"
                )
            except Exception:
                continue
            for handle in handles:
                try:
                    if not await handle.is_visible():
                        continue
                    text = (await handle.inner_text() or "").strip().casefold()
                    if not text or len(text) > 80:
                        continue
                    if is_accessibility_control_text(text):
                        log(f"overlay ignore accessibility control text='{text}'")
                        continue
                    if is_cookie_management_text(text):
                        log(f"overlay ignore cookie management text='{text}'")
                        continue
                    if not any(p in text for p in patterns):
                        continue
                    log(f"overlay match text='{text}' patterns={patterns[:4]}...")
                    try:
                        await handle.click(timeout=timeout_ms, no_wait_after=True)
                    except Exception:
                        try:
                            await handle.click(timeout=timeout_ms, force=True, no_wait_after=True)
                        except Exception:
                            continue
                    clicked_any = True
                    try:
                        current_url = page.url
                    except Exception:
                        current_url = ""
                    log(f"overlay clicked text='{text}' url={current_url}")
                    await asyncio.sleep(0.15)
                except Exception:
                    continue
    except Exception:
        pass
    return clicked_any


async def _click_consent_selector_hints(page, timeout_ms=800):
    clicked_any = False
    for selector in CONSENT_SELECTOR_HINTS:
        try:
            for frame in page.frames:
                try:
                    locator = frame.locator(selector).first
                    if await locator.count() == 0:
                        continue
                    if not await locator.is_visible():
                        continue
                    try:
                        text = (await locator.inner_text() or "").strip().casefold()
                    except Exception:
                        text = ""
                    if is_accessibility_control_text(text):
                        log(f"overlay ignore accessibility control selector hint='{selector}' text='{text}'")
                        continue
                    if is_cookie_management_text(text):
                        log(f"overlay ignore cookie management selector hint='{selector}' text='{text}'")
                        continue
                    try:
                        await locator.click(timeout=timeout_ms, no_wait_after=True)
                    except Exception:
                        await locator.click(timeout=timeout_ms, force=True, no_wait_after=True)
                    clicked_any = True
                    try:
                        current_url = page.url
                    except Exception:
                        current_url = ""
                    log(f"overlay clicked selector hint='{selector}' url={current_url}")
                    await asyncio.sleep(0.15)
                    break
                except Exception:
                    continue
        except Exception:
            continue
    return clicked_any


async def dismiss_overlays(page, max_passes=3):
    """Auto-dismiss cookie banners, consent dialogs and intrusive popups.

    Tries multiple passes because closing one overlay sometimes reveals another.
    Returns the number of elements dismissed.
    """
    dismissed = 0
    for _ in range(max_passes):
        pass_dismissed = 0
        try:
            if await _click_consent_selector_hints(page):
                pass_dismissed += 1
        except Exception:
            pass
        try:
            if await _click_buttons_matching_text(page, CONSENT_TEXT_PATTERNS):
                pass_dismissed += 1
        except Exception:
            pass
        try:
            if await _click_buttons_matching_text(page, DISMISS_TEXT_PATTERNS):
                pass_dismissed += 1
        except Exception:
            pass
        if pass_dismissed == 0:
            break
        dismissed += pass_dismissed
        await asyncio.sleep(0.2)
    if dismissed:
        log(f"dismissed {dismissed} overlay/popup element(s)")
    return dismissed


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
    if specs:
        entry["locatorCandidates"] = [describe_spec(spec) for spec in specs]
        log(f"step {index + 1} locator candidates: {' || '.join(entry['locatorCandidates'])}")

    before = await snapshot_page_state(page)
    entry["before"] = before
    log(
        f"step {index + 1} before url={before['url']} title={before['title']} "
        f"body={before['bodyPreview']}"
    )

    if action in ("goto", "navigate"):
        log(f"step {index + 1} goto target={step['url']}")
        response = await page.goto(step["url"], wait_until=step.get("waitUntil", "domcontentloaded"))
        if step.get("loadState"):
            await page.wait_for_load_state(step["loadState"])
        entry["status"] = response.status if response else None
        if step.get("dismissPopups", True):
            try:
                entry["dismissed"] = await dismiss_overlays(page)
            except Exception as exc:
                log(f"dismiss_overlays after goto failed: {exc}")
    elif action in ("dismiss_popups", "dismiss_overlays", "accept_cookies"):
        entry["dismissed"] = await dismiss_overlays(
            page, max_passes=int(step.get("maxPasses", 3))
        )
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
        if not specs:
            raise ValueError("wait_for_selector action requires a locator")
        matched_selector = await wait_for_selector_any(
            page,
            specs,
            state=step.get("state", "visible"),
            timeout=int(step.get("timeoutMs", 8000)),
        )
        entry["matchedSelector"] = matched_selector
        if not matched_selector and step.get("requireSuccess", True):
            raise RuntimeError(f"wait_for_selector timed out for step {index + 1}")
    elif action == "wait_for_load_state":
        await page.wait_for_load_state(step.get("state", "networkidle"), timeout=step.get("timeoutMs", 10000))
    elif action == "wait_for_timeout":
        await page.wait_for_timeout(int(step.get("timeoutMs", 1000)))
    elif action == "extract_text":
        loc = maybe_locator(page, step) or page.locator("body")
        # collect text from all matching elements; falls back to first when only one
        try:
            count = await loc.count()
        except Exception:
            count = 1
        if count > 1:
            texts = []
            for i in range(min(count, 20)):
                try:
                    texts.append((await loc.nth(i).inner_text()).strip())
                except Exception:
                    pass
            entry["text"] = shorten("\n---\n".join(t for t in texts if t), int(step.get("maxLength", 4000)))
        else:
            entry["text"] = shorten(await loc.first.inner_text(), int(step.get("maxLength", 4000)))
        log(f"step {index + 1} extract_text preview={shorten(entry['text'], 300).replace(chr(10), ' | ')}")
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
    elif action == "prompt_user":
        question = step.get("question")
        if not question:
            raise ValueError("prompt_user action requires question")
        response = await request_user_prompt(
            question,
            timeout_seconds=int(step.get("timeoutSeconds", 180)),
            mode=str(step.get("mode", "text")),
        )
        entry["prompt"] = {"question": question}
        entry["answer"] = response.get("answer", "")
        entry["promptOk"] = bool(response.get("ok", False))
        if step.get("requireSuccess", False) and not response.get("ok", False):
            raise RuntimeError(
                f"prompt_user failed for step {index + 1}: {response.get('error', 'no answer')}"
            )
    else:
        raise ValueError(f"Unsupported action: {action}")

    try:
        entry["url"] = page.url
        entry["title"] = await page.title()
    except Exception:
        pass
    after = await snapshot_page_state(page)
    entry["after"] = after
    log(
        f"step {index + 1} after url={after['url']} title={after['title']} "
        f"body={after['bodyPreview']}"
    )
    log(f"finished step {index + 1} action={action}")
    return entry


# ---------- Account / login helpers ----------


async def is_logged_in(page, account):
    """Best-effort check: navigate to loggedInUrl and decide whether the account
    is logged in. Strategy:
      1. Navigate to loggedInUrl.
        2. If the resulting URL host is different from the target host, treat as
            not logged in.
        3. If the page looks like a generic login wall, treat as not logged in.
        4. Otherwise treat selector matches as strong evidence, but tolerate
            selector drift while we remain on the expected host.
    """
    from urllib.parse import urlparse

    selector = (account.get("loginCheckSelector") or "").strip()
    login_url = (account.get("loginUrl") or "").strip()
    target_url = (account.get("loggedInUrl") or "").strip()
    if not target_url:
        return False
    log(
          f"is_logged_in: account={account.get('id')} "
        f"target_url={target_url} selector={selector}"
    )
    try:
        await page.goto(target_url, wait_until="domcontentloaded", timeout=20000)
    except Exception as exc:
        log(f"loggedInUrl navigation failed: {exc}")
        return False
    try:
        await dismiss_overlays(page, max_passes=1)
    except Exception:
        pass

    target_host = (urlparse(target_url).netloc or "").lower()
    current_host = (urlparse(page.url).netloc or "").lower()
    try:
        title = await page.title()
    except Exception:
        title = ""
    try:
        body_text = await page.locator("body").inner_text()
    except Exception:
        body_text = ""
    body_preview = shorten(body_text, 250).replace("\n", " | ")
    log(
        f"is_logged_in: current_url={page.url} current_host={current_host} "
        f"target_host={target_host} title={title} body={body_preview}"
    )
    if target_host and current_host and target_host != current_host:
        log(f"is_logged_in: redirected to {current_host} (expected {target_host}); not logged in")
        return False

    if is_probable_generic_logged_out_page(title, body_text, page.url, login_url):
        log("is_logged_in: generic login wall detected; treating as not logged in")
        return False

    if not selector:
        log(f"is_logged_in: no selector, host matches ({current_host}); assuming logged in")
        return True

    matched, matched_selector = await any_selector_visible(page, selector, timeout_ms=4000)
    if matched:
        log(f"is_logged_in: selector matched ({matched_selector}); treating as logged in")
        return True

    log(
        f"is_logged_in: selector miss on {current_host}, but no login wall detected; "
        "assuming logged in"
    )
    return True


async def perform_manual_login(page, account):
    """Open the login URL in a visible window and ask the user to finish login
    (including any 2FA) in person. Then poll for the login marker."""
    login_url = (account.get("loginUrl") or "").strip()
    display_name = account.get("displayName") or account.get("id") or "hesap"
    if login_url:
        try:
            await page.goto(login_url, wait_until="domcontentloaded", timeout=20000)
        except Exception as exc:
            log(f"loginUrl navigation failed: {exc}")
    try:
        await dismiss_overlays(page, max_passes=2)
    except Exception:
        pass

    timeout_seconds = int(account.get("manualLoginTimeoutSeconds", 240))
    auto_wait = bool(os.environ.get("VOICE_AGENT_AUTO_LOGIN"))
    if auto_wait:
        log(
            f"auto login wait enabled; polling up to {timeout_seconds}s for "
            f"account '{account.get('id')}'"
        )
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            if await is_logged_in(page, account):
                return True
            await asyncio.sleep(2)
        return False

    question = (
        f"{display_name} icin tarayici penceresi acildi. Lutfen oturum acin"
        " (gerekirse iki adimli dogrulamayi tamamlayin) ve hazir oldugunuzda"
        " 'tamam' deyin."
    )
    response = await request_user_prompt(
        question, timeout_seconds=timeout_seconds, mode="confirm"
    )
    if not response.get("ok", False):
        return False
    # After the user confirms, re-check the login marker.
    return await is_logged_in(page, account)


async def ensure_account_logged_in(page, account):
    if not account:
        return {"used": False}
    already = await is_logged_in(page, account)
    if already:
        log(f"account '{account.get('id')}' already logged in")
        return {"used": True, "loggedIn": True, "manualLogin": False}
    has_display = bool(os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"))
    if not has_display:
        log(f"account '{account.get('id')}' needs login but no DISPLAY available")
        return {
            "used": True,
            "loggedIn": False,
            "manualLogin": False,
            "reason": "no_display_for_manual_login",
            "message": (
                "Hesap oturumu yok ve manuel giris icin gorunur tarayici penceresi"
                " gerekiyor; ancak ortamda DISPLAY/WAYLAND_DISPLAY tanimli degil."
                " Lutfen ilk girisi masaustu erisimi olan bir oturumda yapin (ornegin"
                " 'ssh -X' ile baglanin veya cihaza dogrudan baglanin), profil"
                " kalici olacagindan sonraki cagrilarda SSH uzerinden de calisir."
            ),
        }
    log(f"account '{account.get('id')}' needs login - asking user")
    ok = await perform_manual_login(page, account)
    return {"used": True, "loggedIn": bool(ok), "manualLogin": True}


# ---------- Main ----------

async def run_with_config(config):
    steps = config["steps"]
    artifacts_dir = Path(config["artifactsDir"])
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    log(f"step count: {len(steps)} artifacts={artifacts_dir}")

    use_chrome_profile = bool(config.get("useChromeProfile", False))
    account = config.get("account") or None
    account_profile_dir = (config.get("accountProfileDir") or "").strip()
    headless = bool(config.get("headless", True))
    detected_display = detect_available_display()
    has_display = bool(detected_display or os.environ.get("WAYLAND_DISPLAY"))
    if account_profile_dir or use_chrome_profile:
        # Manuel / 2FA login akisi icin gorunur pencere gerekiyor; ama yalnizca
        # DISPLAY varsa headless=False'a gec. DISPLAY yoksa headless=True olarak
        # devam et: cached oturum varsa zaten calisir, yoksa ensure_account_logged_in
        # net hata dondurur.
        if has_display:
            if detected_display and not os.environ.get("DISPLAY"):
                os.environ["DISPLAY"] = detected_display
                log(f"auto-detected DISPLAY={detected_display}")
            headless = False
        else:
            headless = True
            log("no DISPLAY/WAYLAND_DISPLAY - running headless; manual login won't be possible")
    timeout_ms = int(config.get("timeoutMs", 15000))
    viewport_w = int(config.get("viewportWidth", 1365))
    viewport_h = int(config.get("viewportHeight", 900))
    locale = config.get("locale", "tr-TR")
    slow_mo = int(config.get("slowMoMs", 0))
    user_agent = config.get("userAgent") or "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/123.0.0.0 Safari/537.36"
    proxy = config.get("proxy")

    browser_args = {}
    if proxy:
        # proxy format: http://user:pass@host:port or http://host:port
        browser_args["proxy"] = {"server": proxy}

    async with async_playwright() as p:
        chrome_process = None
        browser = None
        login_status = {"used": False}
        if account_profile_dir:
            persistent_dir = Path(account_profile_dir)
            persistent_dir.mkdir(parents=True, exist_ok=True)
            log(f"account profile path: {persistent_dir}")
            common_args = [
                "--disable-blink-features=AutomationControlled",
                "--no-first-run",
                "--no-default-browser-check",
            ]
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
        elif use_chrome_profile:
            chrome_profile_direct_mode = bool(config.get("chromeProfileDirectMode", False))
            chrome_require_closed = bool(config.get("chromeRequireClosed", False))
            chrome_exe = config.get("chromeExecutablePath") or get_default_chrome_executable_path()
            source_user_data_dir = Path(config.get("chromeUserDataDir") or get_default_chrome_user_data_dir())
            profile_dir = config.get("chromeProfileDir") or resolve_chrome_profile_dir(
                source_user_data_dir, config.get("chromeProfileName", "")
            )
            if chrome_profile_direct_mode:
                if not chrome_exe:
                    raise RuntimeError("System Chrome executable not found for direct profile mode.")
                if not source_user_data_dir.exists():
                    raise FileNotFoundError(f"Chrome user-data-dir not found: {source_user_data_dir}")
                if chrome_require_closed:
                    ensure_chrome_profile_closed(source_user_data_dir)
                launch_user_data_dir = source_user_data_dir
                log(f"direct Chrome profile mode: udd={launch_user_data_dir} profile={profile_dir}")
            else:
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
                mode_label = "CDP/direct" if chrome_profile_direct_mode else "CDP/copied"
                log(f"chrome exe={chrome_exe} udd={launch_user_data_dir} profile={profile_dir} port={port} headless={headless} ({mode_label})")
                chrome_process = launch_debug_chrome(chrome_exe, launch_user_data_dir, profile_dir, port, headless)
                await asyncio.to_thread(wait_for_cdp_endpoint, port, 25)
                browser = await p.chromium.connect_over_cdp(f"http://127.0.0.1:{port}")
                own_browser = False
                context = browser.contexts[0] if browser.contexts else await browser.new_context()
                page = context.pages[0] if context.pages else await context.new_page()
            else:
                if chrome_profile_direct_mode:
                    raise RuntimeError("System Chrome executable not found for direct profile mode.")
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
                user_agent=user_agent,
                **browser_args
            )
            page = await context.new_page()
            # Stealth apply
            if STEALTH_AVAILABLE:
                await playwright_stealth.stealth_async(page)

        page.set_default_timeout(timeout_ms)

        results = []
        try:
            if account:
                login_status = await ensure_account_logged_in(page, account)
                if not login_status.get("loggedIn", False):
                    log("account login could not be confirmed; aborting")
                    error_message = login_status.get("message") or "Hesap oturumu acilamadi."
                    output = {
                        "ok": False,
                        "url": page.url,
                        "title": await page.title() if page else "",
                        "results": [],
                        "artifactsDir": str(artifacts_dir),
                        "finalScreenshot": "",
                        "loginStatus": login_status,
                        "error": error_message,
                    }
                    print(json.dumps(output, ensure_ascii=False))
                    return 1
            for index, step in enumerate(steps):
                try:
                    entry = await execute_step(page, step, index, artifacts_dir)
                    results.append(entry)
                except Exception as exc:
                    log(f"step {index + 1} failed: {type(exc).__name__}: {exc}")
                    error_screenshot_path = artifacts_dir / f"error_step_{index + 1}.png"
                    try:
                        await page.screenshot(path=str(error_screenshot_path), full_page=True)
                        error_screenshot = str(error_screenshot_path)
                    except Exception as screenshot_exc:
                        log(f"error screenshot failed: {screenshot_exc}")
                        error_screenshot = ""
                    try:
                        page_title = await page.title()
                    except Exception:
                        page_title = ""
                    try:
                        page_url = page.url
                    except Exception:
                        page_url = ""
                    failed_state = await snapshot_page_state(page)
                    output = {
                        "ok": False,
                        "url": page_url,
                        "title": page_title,
                        "results": results,
                        "artifactsDir": str(artifacts_dir),
                        "finalScreenshot": error_screenshot,
                        "failedStep": {
                            "index": index,
                            "action": step.get("action"),
                            "step": step,
                            "error": f"{type(exc).__name__}: {exc}",
                            "state": failed_state,
                        },
                        "loginStatus": login_status,
                        "error": f"{type(exc).__name__}: {exc}",
                    }
                    print(json.dumps(output, ensure_ascii=False))
                    return 1

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
                "loginStatus": login_status,
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
    if len(sys.argv) < 2:
        print(json.dumps({"ok": False, "error": "Missing config argument."}, ensure_ascii=False))
        return 1

    if sys.argv[1] == "--inline-config":
        if len(sys.argv) < 3:
            print(json.dumps({"ok": False, "error": "Missing inline config JSON."}, ensure_ascii=False))
            return 1
        config = json.loads(sys.argv[2])
    else:
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
