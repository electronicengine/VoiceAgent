#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

EVENT_PREFIX = "__VA_EVENT__"


def eprint(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


def trim(value):
    return str(value or "").strip()


def humanize_account_id(account_id: str) -> str:
    value = trim(account_id)
    if not value:
        return "hesap"
    for old in ("_", "-", "."):
        value = value.replace(old, " ")
    words = [part for part in value.split() if part]
    if not words:
        return "hesap"
    return " ".join(word[:1].upper() + word[1:].lower() for word in words)


def resolve_relative(base: Path, configured: str) -> Path:
    configured = trim(configured)
    if not configured:
        return Path()
    path = Path(configured).expanduser()
    if path.is_absolute():
        return path.resolve()
    return (base / path).resolve()


def get_default_chrome_user_data_dir() -> Path:
    if os.environ.get("CHROME_USER_DATA_DIR"):
        return Path(os.environ["CHROME_USER_DATA_DIR"]).expanduser().resolve()

    config_root = Path.home() / ".config"
    google_chrome_dir = config_root / "google-chrome"
    chromium_dir = config_root / "chromium"
    if google_chrome_dir.exists():
        return google_chrome_dir.resolve()
    if chromium_dir.exists():
        return chromium_dir.resolve()
    return google_chrome_dir.resolve()


def load_browser_profile(data: dict, config_dir: Path, profile_id: str):
    browser_profiles = data.get("browserProfiles") or {}
    if not isinstance(browser_profiles, dict):
        raise RuntimeError("browserProfiles must be a JSON object.")

    profile = browser_profiles.get(profile_id)
    if not isinstance(profile, dict):
        raise RuntimeError(f"Bilinmeyen browserProfileId: {profile_id}")

    mode = trim(profile.get("mode", "")).lower() or "persistent_dir"
    if mode not in {"persistent_dir", "system_chrome"}:
        raise RuntimeError(f"Desteklenmeyen browser profile mode: {mode}")

    resolved = {
        "id": profile_id,
        "mode": mode,
        "profileDir": "",
        "chromeUserDataDir": "",
        "chromeProfileDir": trim(profile.get("chromeProfileDir", "")),
        "chromeProfileName": trim(profile.get("chromeProfileName", "")),
        "requireChromeClosed": bool(profile.get("requireChromeClosed", mode == "system_chrome")),
    }
    if mode == "system_chrome":
        chrome_user_data_dir = trim(profile.get("chromeUserDataDir", ""))
        if chrome_user_data_dir:
            resolved["chromeUserDataDir"] = str(resolve_relative(config_dir, chrome_user_data_dir))
    else:
        configured_dir = trim(profile.get("profileDir", ""))
        if configured_dir:
            resolved["profileDir"] = str(resolve_relative(config_dir, configured_dir))
    return resolved


def split_frontmatter(content: str):
    lines = content.splitlines()
    if not lines or trim(lines[0]) != "---":
        return "", content

    frontmatter_lines = []
    for index in range(1, len(lines)):
        if trim(lines[index]) == "---":
            body = "\n".join(lines[index + 1:])
            return "\n".join(frontmatter_lines), body
        frontmatter_lines.append(lines[index])
    return "", content


def load_skill_account(repo_root: Path, account_id: str):
    skills_dir = repo_root / "skills"
    if not skills_dir.exists():
        return {}

    for skill_path in sorted(skills_dir.glob("*.md")):
        try:
            content = skill_path.read_text(encoding="utf-8")
            frontmatter, _ = split_frontmatter(content)
            if not frontmatter:
                continue
            meta = json.loads(frontmatter)
        except Exception:
            continue

        account = meta.get("account") or {}
        if trim(account.get("id", "")) != account_id:
            continue

        return {
            "loginUrl": trim(account.get("loginUrl", "")),
            "loggedInUrl": trim(account.get("loggedInUrl", "")),
            "loginCheckSelector": trim(account.get("loginCheckSelector", "")),
        }

    return {}


def load_account(repo_root: Path, account_id: str):
    accounts_file = repo_root / "account.json"
    if not accounts_file.exists():
        raise RuntimeError(f"account.json bulunamadi: {accounts_file}")

    data = json.loads(accounts_file.read_text(encoding="utf-8"))
    accounts = data.get("accounts") or {}
    skill_account = load_skill_account(repo_root, account_id)
    if account_id not in accounts and not skill_account:
        available = ", ".join(sorted(accounts.keys())) or "<bos>"
        raise RuntimeError(f"Bilinmeyen accountId: {account_id}. Mevcut hesaplar: {available}")

    config_dir = accounts_file.parent
    root_dir = resolve_relative(config_dir, data.get("accountsRootDir", ""))
    if not str(root_dir):
        root_dir = (Path.home() / ".voice_agent_browser" / "profiles").resolve()

    record = accounts.get(account_id) or {}
    browser_profile_id = trim(data.get("defaultSessionBrowserProfileId", ""))
    browser_profile = None
    profile_dir = Path()
    if browser_profile_id:
        browser_profile = load_browser_profile(data, config_dir, browser_profile_id)
        if browser_profile["mode"] == "persistent_dir":
            configured_profile = browser_profile.get("profileDir", "")
            if configured_profile:
                profile_dir = Path(configured_profile)
            else:
                profile_dir = (root_dir / browser_profile_id).resolve()
    else:
        profile_dir = (root_dir / account_id).resolve()

    login_url = trim(skill_account.get("loginUrl", "")) or trim(record.get("loginUrl", ""))
    logged_in_url = trim(skill_account.get("loggedInUrl", "")) or trim(record.get("loggedInUrl", "")) or login_url
    login_check_selector = trim(skill_account.get("loginCheckSelector", "")) or trim(record.get("loginCheckSelector", ""))
    if not login_url or not logged_in_url:
        raise RuntimeError(
            f"{account_id} icin login metadata eksik. Ilgili skill frontmatter'inda account.loginUrl ve account.loggedInUrl tanimlanmali."
        )

    return {
        "id": account_id,
        "displayName": humanize_account_id(account_id),
        "loginUrl": login_url,
        "loggedInUrl": logged_in_url,
        "loginCheckSelector": login_check_selector,
        "browserProfileMode": (browser_profile or {}).get("mode", "persistent_dir"),
        "profileDir": str(profile_dir),
        "chromeUserDataDir": (browser_profile or {}).get("chromeUserDataDir", ""),
        "chromeProfileDir": (browser_profile or {}).get("chromeProfileDir", ""),
        "chromeProfileName": (browser_profile or {}).get("chromeProfileName", ""),
        "requireChromeClosed": bool((browser_profile or {}).get("requireChromeClosed", False)),
    }


def make_config(repo_root: Path, account: dict) -> Path:
    artifacts_dir = (repo_root / ".voice_agent_browser" / "artifacts" / f"manual-login-{account['id']}").resolve()
    artifacts_dir.mkdir(parents=True, exist_ok=True)

    config = {
        "steps": [],
        "artifactsDir": str(artifacts_dir),
        "headless": False,
        "timeoutMs": 15000,
        "slowMoMs": 0,
        "viewportWidth": 1365,
        "viewportHeight": 900,
        "locale": "tr-TR",
        "useChromeProfile": False,
        "account": {
            "id": account["id"],
            "displayName": account["displayName"],
            "loginUrl": account["loginUrl"],
            "loggedInUrl": account["loggedInUrl"],
            "loginCheckSelector": account["loginCheckSelector"],
            "manualLoginTimeoutSeconds": 900,
        },
    }

    if account.get("browserProfileMode") == "system_chrome":
        config["useChromeProfile"] = True
        config["chromeProfileDirectMode"] = True
        config["chromeRequireClosed"] = bool(account.get("requireChromeClosed", True))
        if account.get("chromeUserDataDir"):
            config["chromeUserDataDir"] = account["chromeUserDataDir"]
        if account.get("chromeProfileDir"):
            config["chromeProfileDir"] = account["chromeProfileDir"]
        if account.get("chromeProfileName"):
            config["chromeProfileName"] = account["chromeProfileName"]
    else:
        Path(account["profileDir"]).mkdir(parents=True, exist_ok=True)
        config["accountProfileDir"] = account["profileDir"]

    handle = tempfile.NamedTemporaryFile(prefix="voice_agent_account_login_", suffix=".json", delete=False)
    try:
        handle.write(json.dumps(config, ensure_ascii=False, indent=2).encode("utf-8"))
        handle.flush()
    finally:
        handle.close()
    return Path(handle.name)


def run_runner(repo_root: Path, config_path: Path) -> int:
    runner = repo_root / "src" / "tools" / "webbrowser_runner.py"
    browser_root_candidates = [
        repo_root / "build" / ".voice_agent_browser",
        repo_root / ".voice_agent_browser",
    ]
    browser_root = next((path for path in browser_root_candidates if path.exists()), browser_root_candidates[0])

    python_exe = browser_root / "venv" / "bin" / "python"
    if not python_exe.exists():
        python_exe = repo_root / ".venv" / "bin" / "python3"
    if not python_exe.exists():
        python_exe = Path(sys.executable)

    env = os.environ.copy()
    env.setdefault("PLAYWRIGHT_BROWSERS_PATH", str((browser_root / "browsers").resolve()))
    env["VOICE_AGENT_AUTO_LOGIN"] = "1"

    proc = subprocess.Popen(
        [str(python_exe), "-u", str(runner), str(config_path)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        env=env,
        cwd=str(repo_root),
    )

    final_json_line = None
    assert proc.stdout is not None
    assert proc.stdin is not None

    for raw_line in proc.stdout:
        line = raw_line.rstrip("\n")
        if line.startswith(EVENT_PREFIX):
            payload_text = line[len(EVENT_PREFIX):].strip()
            try:
                payload = json.loads(payload_text)
            except json.JSONDecodeError:
                print(line, flush=True)
                continue

            if payload.get("type") == "prompt":
                question = trim(payload.get("question", "")) or "Tarayicida girisi tamamlayin ve Enter'a basin."
                print("\n=== Manuel Giris Gerekli ===", flush=True)
                print(question, flush=True)
                print("Tarayici / noVNC penceresinde oturum acmayi bitirdikten sonra Enter'a basin.", flush=True)
                try:
                    input()
                    response = {"ok": True, "cancelled": False, "timedOut": False, "answer": "tamam", "error": ""}
                except EOFError:
                    response = {"ok": False, "cancelled": True, "timedOut": False, "answer": "", "error": "stdin closed"}
                proc.stdin.write(json.dumps(response, ensure_ascii=False) + "\n")
                proc.stdin.flush()
            continue

        print(line, flush=True)
        stripped = line.strip()
        if stripped.startswith("{") and stripped.endswith("}"):
            final_json_line = stripped

    exit_code = proc.wait()
    if final_json_line:
        try:
            payload = json.loads(final_json_line)
        except json.JSONDecodeError:
            payload = None
        if isinstance(payload, dict):
            login_status = payload.get("loginStatus") or {}
            if payload.get("ok"):
                print("\nLogin tamamlandi.", flush=True)
                if login_status.get("manualLogin"):
                    print("Oturum kalici profile kaydedildi; sonraki kullanimlarda yeniden giris gerekmeyecek.", flush=True)
            else:
                error_message = payload.get("error") or "Hesap girisi dogrulanamadi."
                eprint(f"\nLogin basarisiz: {error_message}")
    return exit_code


def main() -> int:
    if len(sys.argv) != 2 or trim(sys.argv[1]) in {"-h", "--help"}:
        print("Kullanim: scripts/account_login.py <accountId>")
        return 2

    repo_root = Path(__file__).resolve().parents[1]
    account_id = trim(sys.argv[1])
    try:
        account = load_account(repo_root, account_id)
        config_path = make_config(repo_root, account)
        print(f"Hesap: {account['displayName']} ({account['id']})", flush=True)
        if account.get("browserProfileMode") == "system_chrome":
            profile_hint = account.get("chromeProfileDir") or account.get("chromeProfileName") or "Default"
            user_data_dir = account.get("chromeUserDataDir") or str(get_default_chrome_user_data_dir())
            print(f"Chrome Profili: {user_data_dir} [{profile_hint}]", flush=True)
        else:
            print(f"Profil: {account['profileDir']}", flush=True)
        return run_runner(repo_root, config_path)
    except Exception as exc:
        eprint(f"Hata: {exc}")
        return 1
    finally:
        config_path = locals().get("config_path")
        if config_path and Path(config_path).exists():
            Path(config_path).unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
