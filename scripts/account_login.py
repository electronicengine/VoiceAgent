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


def resolve_relative(base: Path, configured: str) -> Path:
    configured = trim(configured)
    if not configured:
        return Path()
    path = Path(configured).expanduser()
    if path.is_absolute():
        return path.resolve()
    return (base / path).resolve()


def load_account(repo_root: Path, account_id: str):
    accounts_file = repo_root / "account.json"
    if not accounts_file.exists():
        raise RuntimeError(f"account.json bulunamadi: {accounts_file}")

    data = json.loads(accounts_file.read_text(encoding="utf-8"))
    accounts = data.get("accounts") or {}
    if account_id not in accounts:
        available = ", ".join(sorted(accounts.keys())) or "<bos>"
        raise RuntimeError(f"Bilinmeyen accountId: {account_id}. Mevcut hesaplar: {available}")

    config_dir = accounts_file.parent
    root_dir = resolve_relative(config_dir, data.get("accountsRootDir", ""))
    if not str(root_dir):
        root_dir = (Path.home() / ".voice_agent_browser" / "profiles").resolve()

    record = accounts[account_id]
    configured_profile = trim(record.get("profileDir", ""))
    if configured_profile:
        profile_dir = resolve_relative(config_dir, configured_profile)
    else:
        profile_dir = (root_dir / account_id).resolve()

    return {
        "id": account_id,
        "displayName": trim(record.get("displayName", "")) or account_id,
        "provider": trim(record.get("provider", "")).lower(),
        "loginUrl": trim(record.get("loginUrl", "")),
        "loggedInUrl": trim(record.get("loggedInUrl", "")),
        "loginCheckSelector": trim(record.get("loginCheckSelector", "")),
        "profileDir": str(profile_dir),
    }


def make_config(repo_root: Path, account: dict) -> Path:
    artifacts_dir = (repo_root / ".voice_agent_browser" / "artifacts" / f"manual-login-{account['id']}").resolve()
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    Path(account["profileDir"]).mkdir(parents=True, exist_ok=True)

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
        "accountProfileDir": account["profileDir"],
        "account": {
            "id": account["id"],
            "displayName": account["displayName"],
            "provider": account["provider"],
            "loginUrl": account["loginUrl"],
            "loggedInUrl": account["loggedInUrl"],
            "loginCheckSelector": account["loginCheckSelector"],
            "manualLoginTimeoutSeconds": 900,
        },
    }

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
