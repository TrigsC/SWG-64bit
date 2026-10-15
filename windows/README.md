# Windows x64 build milestone (Issue #7)

This is a separate Windows development build, not the Mac app or a completed
game client. Issue #25 covers validation of this foundation; the development
launcher below addresses Issue #26. An actual Windows x64 build/test result is required before
advertising Windows support. The [initial native x64 CI run](https://github.com/TrigsC/SWG-64bit/actions/runs/38650998829)
compiled the core and full client/renderer, passed five core/startup tests and
six renderer-build/window tests, and passed the Mac regression suite. Vulkan
frame presentation remains unverified; use the explicit render probe below.

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
tests and controlled-server checks are outside this Windows test set. The
Python source checks still run separately in CI.

## Launcher

After `python scripts/setup_windows.py`, open
`build/windows-x64/Debug/SWGLauncher.exe`. A downloadable personal test build
is also retained by the Windows CI job as `swg-windows-x64-test-client` for
seven days. Extract it as a folder and open its `SWGLauncher.exe`; keep its
executables, `shaders` and `licenses` together. It requires no compiler, MSVC
runtime installer, additional GUI framework, or managed runtime installation.
It still requires a working Windows Vulkan graphics driver.

To create that bundle locally on a build machine:

```powershell
python scripts/setup_windows.py --configuration Release
python scripts/package_windows.py
```

The package contains only three x64 executables, eight compiled shaders,
license notices, a graphics probe shortcut, instructions, and a SHA-256
manifest. CI smoke-checks the copied executables from outside the bundle.
The launcher finds helpers beside itself and shaders load relative to the
helper executable. Its local asset index remains in the chosen installation's
ignored `modernization/tools/local-assets` folder. Generate that index with
`python scripts/generate_local_indexes.py` in the contributor checkout.

1. Browse to your owned SWG folder containing `SWGEmu.exe` and `.tre` archives.
   The launcher reads `swgemu_login.cfg` there for editable server defaults.
2. Check the server if desired. This performs only the existing SOE handshake.
3. Enter the account and password, then choose **Load characters**. This
   contacts the configured login server and populates a bounded selection list.
4. Select an existing character, re-enter the password, and choose **Connect**.
   Selection uses the character's full 64-bit ID and galaxy, including when
   names are identical on different galaxies. The server still validates it.
5. Close the world window to exit normally, or use **Disconnect** to stop the
   child process. Closing the launcher also stops its child process group.

Passwords never enter process arguments or settings files. The launcher sends
the existing bounded binary credential frame through a private stdin pipe,
clears the password control after transfer, and erases the transfer buffer.
Windows binary stdin mode preserves CR/LF and Ctrl-Z bytes. There is no saved
password or launcher log. Raw helper output is discarded; bounded character
records and known status markers update the UI. Account/server/installation
changes require loading the list again before connecting.

Connection failures, timeouts and process exit codes are visible. Startup has
a 90-second timeout; a ready world has no fixed session timeout. **Disconnect**
forcibly stops the managed process group; a graceful in-protocol disconnect is
not established by this launcher. Non-ASCII game installation paths remain
limited by the existing helper's narrow Windows argument/path handling.

CI runs real child-process tests for credential bytes, Windows argument quoting,
failure exits, cancellation and missing helpers. It also runs
`SWGLauncher.exe --smoke-test`: the real launcher opens, starts the existing
synthetic native world-window probe, observes its completion and exits. This
mode does not contact any server or need game assets, and does not initialize
Vulkan. Live character listing, zone-in and Vulkan frame presentation still
need a permitted controlled server and graphics-capable Windows host.

SDK installer reference:
[LunarG Windows SDK guide](https://vulkan.lunarg.com/doc/view/1.4.313.0/windows/getting_started.html).
