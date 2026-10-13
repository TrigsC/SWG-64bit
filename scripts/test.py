#!/usr/bin/env python3
"""Run deterministic source tests by default; opt into owned-asset tests."""

import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/macos-vulkan"
CTEST = ROOT / "tools/python/bin/ctest"

SOURCE_ONLY = (
    "arm64_math", "camera_volume_clearance", "offline_world_cpu",
    "native_login_input",
    "native_transport_lifecycle", "native_movement_reporting",
    "remote_motion_interpolation", "reliable_soe_wire", "reliable_soe_windows",
    "exterior_floor_support", "accepted_cell_crossings", "precise_spatial_frames",
    "native_target_selection"
)
LOOPBACK = {"native_transport_lifecycle", "reliable_soe_wire"}


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--assets", action="store_true", help="also run tests using your local game data")
    p.add_argument("--loopback", action="store_true", help="include tests that bind localhost UDP sockets")
    p.add_argument("--list", action="store_true", help="list source-only tests")
    args = p.parse_args()
    if args.list:
        print("\n".join(SOURCE_ONLY))
        return
    if not CTEST.is_file() or not BUILD.is_dir():
        raise SystemExit("Build not found. Run python3 scripts/setup.py first.")
    if args.assets:
        subprocess.run([sys.executable, str(ROOT / "scripts/extract_local_test_assets.py")], check=True)
    subprocess.run([sys.executable, "-m", "unittest", "discover", "-s",
                    str(ROOT / "scripts/tests"), "-p", "test_*.py"], check=True)
    command = [str(CTEST), "--test-dir", str(BUILD), "--output-on-failure", "--no-tests=error", "--parallel", "4"]
    if not args.assets:
        selected = [name for name in SOURCE_ONLY if args.loopback or name not in LOOPBACK]
        command += ["-R", "^(" + "|".join(re.escape(name) for name in selected) + ")$"]
    elif not args.loopback:
        command += ["-E", "^(" + "|".join(re.escape(name) for name in LOOPBACK) + ")$"]
    print("+", " ".join(command), flush=True)
    raise SystemExit(subprocess.call(command))


if __name__ == "__main__":
    main()
