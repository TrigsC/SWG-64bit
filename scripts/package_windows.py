#!/usr/bin/env python3
"""Collect only generated x64 executables, shaders and required license notices."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/windows-x64"
SHADERS = tuple(f"{name}_{entry}.spv" for name in ("Wireframe", "Label", "Mesh", "Terrain") for entry in ("VSMain", "PSMain"))
NOTICES = {
    "PROJECT-LICENSE.md": ROOT / "LICENSE.md",
    "SWGVulkanClient-LICENSE.md": ROOT / "vendor/SWGVulkanClient/LICENSE.md",
    "Asio-LICENSE.txt": BUILD / "_deps/asio-src/asio/LICENSE_1_0.txt",
    "zlib-LICENSE.txt": BUILD / "_deps/zlib-src/LICENSE",
    "VMA-LICENSE.txt": BUILD / "_deps/vma-src/LICENSE.txt",
}


def x64_executable(path):
    with path.open("rb") as stream:
        header = stream.read(64)
        if len(header) != 64 or header[:2] != b"MZ":
            raise ValueError("Invalid executable: " + path.name)
        stream.seek(struct.unpack_from("<I", header, 60)[0])
        if stream.read(6) != b"PE\0\0\x64\x86":
            raise ValueError("Expected native x64 executable: " + path.name)
    image = path.read_bytes().lower()
    if any(name in image for name in (b"vcruntime140", b"msvcp140", b"ucrtbased.dll")):
        raise ValueError("Executable still requires a separately installed MSVC runtime: " + path.name)


def package(output, commit):
    sources = {
        "SWGLauncher.exe": BUILD / "Release/SWGLauncher.exe",
        "dummyclient.exe": BUILD / "swg/tools/dummyclient/Release/dummyclient.exe",
        "windows-client-smoke.exe": BUILD / "Release/windows-client-smoke.exe",
    }
    for name, path in sources.items():
        x64_executable(path)
    for name in SHADERS:
        path = BUILD / "swg/libs/renderer/shaders" / name
        if path.read_bytes()[:4] != b"\x03\x02\x23\x07":
            raise ValueError("Invalid compiled shader: " + name)
    for path in NOTICES.values():
        if not path.is_file():
            raise FileNotFoundError("Missing license notice: " + path.name)
    # A fresh destination prevents stale binaries/data from leaking into CI.
    output.mkdir(parents=True, exist_ok=False)
    (output / "shaders").mkdir()
    (output / "licenses").mkdir()
    for name, path in sources.items():
        shutil.copyfile(path, output / name)
    for name in SHADERS:
        shutil.copyfile(BUILD / "swg/libs/renderer/shaders" / name, output / "shaders" / name)
    for name, path in NOTICES.items():
        shutil.copyfile(path, output / "licenses" / name)
    (output / "README.txt").write_text(
        "SWG Windows x64 personal test build\n\n"
        "Open SWGLauncher.exe and select your owned SWGEmu folder.\n"
        "Load characters, choose one, re-enter the password, and Connect.\n"
        "Use only a server that permits this experimental client.\n"
        "Credentials are not saved. Original game files are not included.\n\n"
        "Keep all package files together. A working Windows Vulkan driver is required.\n"
        "Windows ARM64 runs these x64 executables under emulation; a VM display\n"
        "adapter may not provide Vulkan. TestGraphics.cmd checks three synthetic\n"
        "frames without game assets, credentials or any server connection.\n"
        "The launcher can open even when that graphics test fails.\n\n"
        "The local object-template index is read from the chosen installation's\n"
        "modernization/tools/local-assets folder. It is never part of this package.\n"
        "This is an incomplete client, not a gameplay-parity claim.\n",
        encoding="utf-8")
    (output / "TestGraphics.cmd").write_text('@echo off\n"%~dp0windows-client-smoke.exe" --render\npause\n', encoding="ascii")
    hashes = {path.relative_to(output).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in sorted(output.rglob("*")) if path.is_file()}
    (output / "build.json").write_text(json.dumps({"source_commit": commit, "configuration": "Release", "architecture": "x64", "sha256": hashes}, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/windows-test-client")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    package(args.output, commit)
    # Run from outside the package so accidental CWD dependencies are caught.
    for executable, argument in (("dummyclient.exe", "--help"), ("windows-client-smoke.exe", "--window-only"), ("SWGLauncher.exe", "--smoke-test")):
        subprocess.run([str(args.output.resolve() / executable), argument], cwd=ROOT, check=True, timeout=20)
    print("Personal test package ready; no game assets included.")


if __name__ == "__main__":
    main()
