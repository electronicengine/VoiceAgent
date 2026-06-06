#!/usr/bin/env python3

import runpy
import sys
from pathlib import Path


def main() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    runner = repo_root / "scripts" / "webbrowser_runner.py"
    if not runner.exists():
        raise SystemExit(f"Missing canonical runner: {runner}")
    sys.path.insert(0, str(runner.parent))
    runpy.run_path(str(runner), run_name="__main__")


if __name__ == "__main__":
    main()