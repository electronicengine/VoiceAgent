#!/usr/bin/env python3
import argparse
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


def make_config(repo_root: Path, args) -> Path:
    session_id = trim(args.session_id)
    display_name = trim(args.display_name) or session_id
    login_url = trim(args.login_url)
    logged_in_url = trim(args.logged_in_url) or login_url
    login_check_selector = trim(args.login_check_selector)

    artifacts_dir = (repo_root / ".voice_agent_browser" / "artifacts" / f"manual-login-{session_id}").resolve()
    profile_dir = (repo_root / ".voice_agent_browser" / "sessions" / session_id).resolve()
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    profile_dir.mkdir(parents=True, exist_ok=True)

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
        "accountProfileDir": str(profile_dir),
        "account": {
            "id": session_id,
            "displayName": display_name,
            "provider": "generic",
            "loginUrl": login_url,
            "loggedInUrl": logged_in_url,
            "loginCheckSelector": login_check_selector,
            "manualLoginTimeoutSeconds": 900,
        },
    }

    handle = tempfile.NamedTemporaryFile(prefix="voice_agent_site_login_", suffix=".json", delete=False)
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
                error_message = payload.get("error") or "Site girisi dogrulanamadi."
                eprint(f"\nLogin basarisiz: {error_message}")
    return exit_code


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Kalici site oturumu icin manuel login yardimcisi")
    parser.add_argument("--session-id", required=True)
    parser.add_argument("--display-name", default="")
    parser.add_argument("--login-url", required=True)
    parser.add_argument("--logged-in-url", required=True)
    parser.add_argument("--login-check-selector", default="")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    config_path = None
    try:
        config_path = make_config(repo_root, args)
        print(f"Site: {trim(args.display_name) or trim(args.session_id)} ({trim(args.session_id)})", flush=True)
        print(f"Login URL: {trim(args.login_url)}", flush=True)
        print(f"Logged-in URL: {trim(args.logged_in_url)}", flush=True)
        return run_runner(repo_root, config_path)
    except Exception as exc:
        eprint(f"Hata: {exc}")
        return 1
    finally:
        if config_path and Path(config_path).exists():
            Path(config_path).unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
