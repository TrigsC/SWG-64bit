# Codex contribution instructions

This repository is checked out as `modernization` inside a contributor-owned
SWGEmu/Pre-CU installation. Never commit, upload, modify or redistribute the
parent installation's EXE, DLLs, `.tre` files, extracted assets, credentials,
configuration, session logs, screenshots, packet captures, recorded animation
matrices, or derived asset indexes. Do not put local
absolute paths, server addresses, character names or secrets in a PR.

## Select the host platform before setup

Read [windows/README.md](windows/README.md) on Windows. The Mac launcher and
`scripts/setup.py` require Apple Silicon/macOS and are not the Windows setup
path. Use the following commands from the `modernization` repository root:

```powershell
python scripts/setup_windows.py --check
python scripts/setup_windows.py --core
python scripts/setup_windows.py --configuration Release
python scripts/package_windows.py
```

The Windows build requires VS2022/v143 x64 tools, a Windows SDK, CMake/CTest,
Git, Python and the x64 Vulkan SDK. The check reports missing prerequisites;
it does not install them. The exact pinned Vulkan SDK install/checksum steps
are in `.github/workflows/publication-check.yml`. Build outputs stay in ignored
`build/windows-x64`; the personal test bundle is `build/windows-test-client`.

For a small VM or a user who only wants to test a binary, prefer the CI
artifact over installing a compiler and SDKs. Find the successful **Source
and build checks** run for the intended branch/commit and download
`swg-windows-x64-test-client` from its artifacts. Artifacts expire after seven
days; run the workflow again if needed. Verify GitHub's ZIP digest and the
extracted `build.json` file hashes. Keep executables, shaders and license
notices together, then open `SWGLauncher.exe`. The workflow definition is the
reproducible build recipe; the expiring artifact is not the source of truth.

For owned-asset testing, generate only the ignored local object-name index:
`python scripts/generate_local_indexes.py`. The launcher reads it from the
chosen installation's `modernization/tools/local-assets`. Never include that
index or original game files in a package, commit or CI upload.

Windows validation has separate levels: compile/tests, launcher/world-window
smoke, three-frame Vulkan presentation, and permitted live server checks.
The copied package is smoke-checked in CI. Run `SWGLauncher.exe --smoke-test`
and `windows-client-smoke.exe --render` locally without an account/server to
check startup and graphics. Windows ARM64 uses x64 emulation; report it
separately from native x64 CI. A software-renderer smoke pass is not evidence
of hardware performance, live login, or gameplay parity.

Before work: read README.md, CONTRIBUTING.md, and WORK_QUEUE.md; inspect open
GitHub issues and PRs. Run `python3 scripts/work_queue.py --platform mac --limit 3`
on a Mac (or choose `windows` / `research` as appropriate) to shortlist issues;
the live issue and PR pages remain the final check. Prefer a small `available`
issue without a current claim.
Show 2–3 relevant choices and a recommendation. If the user says “work on it”
without selecting one, take the recommended issue. Mark it as claimed with a
brief issue comment if GitHub access allows, and link the issue in the draft PR.

When publishing a contribution, check the contributor's GitHub access to
`TrigsC/SWG-64bit`. Public contributors normally have no write access: create
or reuse a fork in their own account, create an issue branch, push that branch
to the fork, and open a draft PR targeting `TrigsC/SWG-64bit:main`. Keep the
upstream remote for fetching and use a separate fork remote for pushing. If the
contributor has write access, use an issue branch in the upstream repository;
never push directly to `main`. Handle the fork, Git remotes, push, and PR for
the contributor. Do not ask for upstream write access. If GitHub access or fork
creation is unavailable, retain the tested local branch, explain the one-time
account connection or fork step needed, and resume publication afterward.

Before new binary research, check the linked Stardust research record using the
version and evidence rules in [RESEARCH.md](RESEARCH.md). Reuse its documented
findings as leads; corroborate build-specific claims against the contributor's
own client before treating them as SWGEmu behavior. Use the original
SWGEmu/Pre-CU client and REA only for narrow, read-only behavior research when
a concrete question remains. Use original assets from the
local installation for offline tests. Do not execute unsolicited admin commands,
automated gameplay or bulk traffic on public servers. No production-server
login is part of CI. The Swift/AppKit app remains Mac ARM64 only; Windows uses
its separate native launcher and verified x64 build path.

Run focused tests before opening a draft PR. On Mac, use `python3 scripts/test.py`;
on Windows, use the Windows core and full Release build/test commands above.
Run Python unittest discovery and the publication audit on either platform.
If local build tools are unavailable, report that limit and obtain the native
compile/test result through CI. The Mac test runner's missing build is not a
Windows test failure. When local `.tre` files are available and relevant, run
`python3 scripts/test.py --assets`. State the test results and any unverified
live behavior. Keep the upstream license and attribution intact. Keep generated
dependencies, builds and archives out of Git. Never claim complete client parity
from a passing unit test.

For Mac runtime or packaging changes, validate the exact `.app` the user will
open, not only the build-tree executable. If updating an existing app, set
`SWG_CLIENT_APP_PATH` to its absolute path when running `live-client/build-client.sh`;
do not create a second client bundle by default. After source tests, verify its
code signature and bundled shaders/libraries with the temporary build directory
unavailable. Quit and reopen the app and complete a permitted world load when
credentials and a test server are available. Report source, packaged-app, and
live results separately; mark unavailable checks **Not run**.
