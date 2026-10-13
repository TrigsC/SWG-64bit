#!/usr/bin/env python3
"""Run REA's own reviewed Codex setup for a bounded research issue."""

from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    npm = shutil.which("npm")
    if npm is None:
        raise SystemExit("REA requires a supported Node.js/npm installation. See https://github.com/morluto/rea/blob/main/docs/installation.md")
    print("REA setup will show its own proposed Codex configuration changes and ask you to review them.")
    print("A native decompiler provider such as Ghidra is a separate requirement for SWGEmu.exe analysis.")
    subprocess.run([npm, "exec", "--yes", "--package=rea-agents@6.2.0", "--", "rea", "setup"], cwd=ROOT, check=True)
    print("Restart/reconnect Codex, then run REA's provider doctor before relying on binary analysis.")


if __name__ == "__main__":
    main()
