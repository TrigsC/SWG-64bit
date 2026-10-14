# Windows x64 build milestone (Issue #7)

This is a separate Windows development build, not the Mac app or a completed
game launcher. Issue #25 covers validation of this foundation; launcher UI
remains Issue #26. An actual Windows x64 build/test result is required before
advertising Windows support. The initial implementation has not yet been
validated on a Windows x64 runner.

## Prerequisites and inventory

- Windows x64 host or Windows x64 CI runner for milestone validation. Windows
  ARM64 development with x64 cross tools/emulation is allowed, but is reported
  separately and is not native x64 validation evidence.
- Visual Studio 2022 or its Build Tools, v143 x64 C++ tools and a Windows SDK.
- Python 3.10+, Git, CMake 3.25+ and CTest on PATH.
- For the renderer: x64 Vulkan SDK, including headers, loader import library
  and DXC. Set VULKAN_SDK to its installation directory.
- For presentation: a Vulkan driver supporting Win32 surfaces and the
  renderer's device features, including BC texture compression. An SDK alone
  does not supply a graphics driver. VM display adapters need separate checks.

From the repository root:

```powershell
python scripts/setup_windows.py --check
python scripts/setup_windows.py --core
python scripts/setup_windows.py
python scripts/setup_windows.py --render-smoke
```

The check reports architecture, free disk space, VS2022 discovery, CMake/CTest
and SDK components. A missing prerequisite causes a nonzero exit. The script
does not install global tools. Provision them explicitly or use the CI runner
to avoid spending a small VM's remaining space on SDKs and compiler caches.
No fixed minimum disk estimate has been measured yet.

Optional portable CMake/CTest (kept inside this checkout, about a 36 MB download):

```powershell
python -m pip install --no-deps --only-binary=:all: --platform win_amd64 --target tools/cmake cmake==3.31.6
```

The helper checks PATH first, then this portable installation.

The core step configures an x64/v143 build with Vulkan disabled, compiles the
client and four synthetic core tests, and launches `dummyclient --help`.
The full step builds the client/visualizer, shaders and window smoke program.
Build products and fetched dependencies stay under ignored `build/windows-x64`.
Only bounded synthetic tests are registered; no game files, credentials or
game servers are used. `scripts/setup.py` and the Mac app remain Mac-only.

## Backend and smoke evidence

Windows uses `renderer/Window.cpp` (native Win32 HWND), Vulkan Win32 surfaces,
GDI text rasterization, DirectXMath and DXC-compiled HLSL/SPIR-V. macOS uses SDL3,
CoreText and glslang/GLSL. The Windows entry point does not use the Mac CMake
wrapper, Swift launcher, dylib paths, or MoltenVK ICD environment override.
The shared renderer already selects Win32 surface creation on Windows;
Apple portability-enumeration flags remain Apple-only. Portability-subset
device extensions are enabled only if advertised by the selected driver.

CI first builds/tests the core without Vulkan, then installs checksum-verified
Vulkan SDK 1.4.321.1 using LunarG's unattended installer and builds the renderer
and full client. CTest launches `windows-client-smoke --window-only`, which
creates a native window, pumps messages, and exits. This checks native process
and window startup, **not Vulkan device initialization or presentation**.

On a graphics-capable Windows x64 host, `--render-smoke` additionally creates
the real renderer and swapchain, loads the compiled shaders, presents three
synthetic clear frames and exits within a 30-second process timeout. Missing
drivers or rendering failures fail this probe; they are never reported as a
skip/pass. Neither smoke demonstrates gameplay parity. Record the exact host,
commands and results when supplying Issue #7 validation evidence.

Source tests currently covered: login input, movement reporting, remote motion
interpolation, target selection, and client CLI startup. Mac renderer/asset
tests, controlled-server checks and launcher UI are outside this initial
Windows test set. The Python source checks still run separately in CI.

SDK installer reference:
[LunarG Windows SDK guide](https://vulkan.lunarg.com/doc/view/1.4.313.0/windows/getting_started.html).
