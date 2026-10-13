#!/usr/bin/env python3
"""Local-only Apple Silicon setup for a contributor-owned SWG installation."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT.parent
DEPS = ROOT / "tools" / "vulkan"
MANIFEST = json.loads((Path(__file__).parent / "dependencies.json").read_text())


def run(*args: object) -> None:
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run([str(x) for x in args], check=True)


def check() -> None:
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise SystemExit("The native launcher currently requires macOS on Apple Silicon; Windows x64 has not been ported.")
    major = int(platform.mac_ver()[0].split(".")[0] or 0)
    if major < 26:
        raise SystemExit("This launcher currently targets macOS 26 or newer.")
    for command in ("git", "xcrun", "codesign", "python3"):
        if not shutil.which(command):
            raise SystemExit(f"Missing {command}. Install Xcode Command Line Tools, then retry.")
    for tool in ("swiftc", "clang++"):
        run("xcrun", "--find", tool)
    if not (GAME / "SWGEmu.exe").is_file() or not list(GAME.glob("*.tre")):
        raise SystemExit("Clone this repository as 'modernization' inside your owned SWGEmu folder containing SWGEmu.exe and .tre archives.")
    if ROOT.name != "modernization":
        raise SystemExit("The current runtime expects this repository to be named 'modernization' inside the game folder.")
    print(f"Prerequisites passed; game assets stay in {GAME}.")


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def download(item: dict, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file() and digest(destination) == item["sha256"]:
        return
    temp = destination.with_suffix(destination.suffix + ".part")
    print("Downloading", item["name"], flush=True)
    with urllib.request.urlopen(item["url"], timeout=90) as source, temp.open("wb") as output:
        shutil.copyfileobj(source, output)
    if digest(temp) != item["sha256"]:
        temp.unlink(missing_ok=True)
        raise SystemExit(f"Checksum mismatch: {item['name']}")
    temp.replace(destination)


def install_archives() -> None:
    for item in MANIFEST["archives"]:
        archive = ROOT / "downloads" / item["name"]
        download(item, archive)
        destination = ROOT / item["destination"]
        destination.mkdir(parents=True, exist_ok=True)
        marker = destination / (".installed-" + item["sha256"][:16])
        if not marker.is_file():
            run("tar", "-xf", archive, "-C", destination)
            marker.touch()
    for item in MANIFEST["files"]:
        download(item, ROOT / item["destination"])
    for item in MANIFEST["git"]:
        destination = ROOT / item["destination"]
        new_clone = not (destination / ".git").is_dir()
        if new_clone:
            destination.parent.mkdir(parents=True, exist_ok=True)
            run("git", "clone", "--filter=blob:none", "--no-checkout", item["url"], destination)
        current = subprocess.check_output(["git", "-C", str(destination), "rev-parse", "HEAD"], text=True).strip()
        if new_clone or current != item["commit"]:
            exists = subprocess.run(["git", "-C", str(destination), "cat-file", "-e", item["commit"] + "^{commit}"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
            if not exists:
                run("git", "-C", destination, "fetch", "--filter=blob:none", "origin", item["commit"])
            run("git", "-C", destination, "checkout", "--detach", item["commit"])


def install_build_tools() -> tuple[Path, Path]:
    venv = ROOT / "tools" / "python"
    if not (venv / "bin/python").is_file():
        run(sys.executable, "-m", "venv", venv)
    python = venv / "bin/python"
    run(python, "-m", "pip", "install", "--disable-pip-version-check", "cmake==3.31.6", "ninja==1.11.1.4")
    return venv / "bin/cmake", venv / "bin/ninja"


def build_dependencies(cmake: Path, ninja: Path) -> None:
    common = ["-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}", "-DCMAKE_OSX_ARCHITECTURES=arm64", "-DCMAKE_BUILD_TYPE=Release"]

    def build(name: str, source: Path, install: Path, extra: list[str]) -> None:
        output = ROOT / "build" / name
        run(cmake, "-S", source, "-B", output, *common, f"-DCMAKE_INSTALL_PREFIX={install}", *extra)
        run(cmake, "--build", output, "--parallel", str(max(1, min(os.cpu_count() or 2, 4))))
        run(cmake, "--install", output)

    build("sdl3", DEPS / "sdl/SDL3-3.4.18", DEPS / "sdl-install", ["-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF", "-DSDL_SHARED=ON", "-DSDL_STATIC=OFF"])
    build("vulkan-headers", DEPS / "Vulkan-Headers", DEPS / "headers-install", ["-DVULKAN_HEADERS_ENABLE_TESTS=OFF"])
    build("vulkan-loader", DEPS / "Vulkan-Loader", DEPS / "loader-install", [f"-DCMAKE_PREFIX_PATH={DEPS / 'headers-install'}", "-DBUILD_TESTS=OFF"])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="inspect prerequisites without installing")
    parser.add_argument("--assets", action="store_true", help="also run owned-asset tests")
    args = parser.parse_args()
    check()
    if args.check:
        return
    cmake, ninja = install_build_tools()
    install_archives()
    run(sys.executable, ROOT / "scripts/generate_local_indexes.py")
    build_dependencies(cmake, ninja)
    run(ROOT / "live-client/build-client.sh")
    # The app packaging step builds only dummyclient; CTest also needs the
    # independently compiled source and asset test executables.
    run(cmake, "--build", ROOT / "build/macos-vulkan", "--parallel",
        str(max(1, min(os.cpu_count() or 2, 4))))
    test = [sys.executable, str(ROOT / "scripts/test.py")]
    if args.assets:
        test.append("--assets")
    run(*test)
    print("Ready:", ROOT / "live-client/SWG Native Client.app")


if __name__ == "__main__":
    main()
