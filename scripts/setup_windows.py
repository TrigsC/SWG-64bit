#!/usr/bin/env python3
"""Inventory and build the separate asset-free Windows x64 milestone.

Install prerequisites explicitly; this helper never installs global packages,
downloads SDK installers, reads game assets, or contacts a game server.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def host_machine():
    # platform.machine() can describe the emulated Python process on Windows ARM.
    if platform.system() == "Windows":
        from ctypes import wintypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetCurrentProcess.restype = wintypes.HANDLE
        probe = kernel.IsWow64Process2
        probe.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.WORD), ctypes.POINTER(wintypes.WORD)]
        probe.restype = wintypes.BOOL
        process, native = wintypes.WORD(), wintypes.WORD()
        if not probe(kernel.GetCurrentProcess(), ctypes.byref(process), ctypes.byref(native)):
            raise ctypes.WinError(ctypes.get_last_error())
        return {0x8664: "AMD64", 0xAA64: "ARM64", 0x014C: "x86"}.get(native.value, "unknown")
    return platform.machine()


def build_tool(name):
    installed = shutil.which(name)
    if installed:
        return installed
    portable = ROOT / "tools/cmake/cmake/data/bin" / (name + ".exe")
    return str(portable) if portable.is_file() else None


def inventory():
    sdk = Path(os.environ.get("VULKAN_SDK", ""))
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    vs = ""
    if vswhere.is_file():
        vs = subprocess.check_output([str(vswhere), "-latest", "-version", "[17.0,18.0)",
            "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property", "installationPath"], text=True).strip()
    return {"system": platform.system(), "machine": host_machine(),
            "free_gib": round(shutil.disk_usage(ROOT).free / 1024**3, 2),
            "git": shutil.which("git"), "cmake": build_tool("cmake"),
            "ctest": build_tool("ctest"), "vs2022": vs,
            "vulkan_sdk": os.environ.get("VULKAN_SDK", ""),
            "vulkan_headers": (sdk / "Include/vulkan/vulkan.h").is_file(),
            "vulkan_library": (sdk / "Lib/vulkan-1.lib").is_file(),
            "dxc": (sdk / "Bin/dxc.exe").is_file()}


def missing(info, core=False):
    errors = []
    if info["system"] != "Windows":
        errors.append("Windows is required")
    if info["machine"].lower() not in ("amd64", "x86_64", "arm64", "aarch64"):
        errors.append("A 64-bit Windows development host is required")
    for tool in ("git", "cmake", "ctest", "vs2022"):
        if not info[tool]:
            errors.append("Missing " + tool)
    if not core:
        for tool in ("vulkan_headers", "vulkan_library", "dxc"):
            if not info[tool]:
                errors.append("Missing " + tool + "; install the x64 Vulkan SDK and set VULKAN_SDK")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report prerequisites without installing or building")
    parser.add_argument("--core", action="store_true", help="compile and test without Vulkan/renderer")
    parser.add_argument("--render-smoke", action="store_true", help="also present three frames; requires a Vulkan graphics driver")
    args = parser.parse_args()
    if args.core and args.render_smoke:
        parser.error("--core and --render-smoke cannot be combined")
    info = inventory()
    print(json.dumps(info, indent=2), flush=True)
    if info["machine"].lower() in ("arm64", "aarch64"):
        print("ARM64 host: x64 execution uses emulation. A native Windows x64 CI/host result is still required.", flush=True)
    errors = missing(info, args.core)
    if errors:
        raise SystemExit("\n".join(errors))
    if args.check:
        return
    def run(*command):
        subprocess.run(command, cwd=ROOT / "windows", check=True)
    run(info["cmake"], "--preset", "windows-x64", "-DSWG_BUILD_RENDERER=" + ("OFF" if args.core else "ON"))
    run(info["cmake"], "--build", "--preset", "windows-x64", "--parallel", "2")
    run(info["ctest"], "--preset", "windows-x64")
    if args.render_smoke:
        subprocess.run([str(ROOT / "build/windows-x64/Debug/windows-client-smoke.exe"), "--render"],
                       cwd=ROOT / "windows", check=True, timeout=30)


if __name__ == "__main__":
    main()
