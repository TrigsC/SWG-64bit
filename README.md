# SWG-64bit — Apple Silicon / SWGEmu Pre-CU

An experimental, native ARM64 and 64-bit SWGEmu client built from the
[SWGVulkanClient](https://github.com/maddogg221/SWGVulkanClient) codebase. The
current Mac app renders a world, handles movement and early HUD/chat features,
and remains **incomplete**. It is not affiliated with SWGEmu, Lucasfilm,
Daybreak, or the upstream maintainer. See [LICENSE.md](LICENSE.md): the upstream
code permits personal, non-commercial forks and redistribution under its terms.

**Supported now:** macOS 26+, Apple Silicon. Windows x64 is a future build and
test target, not an available launcher option. No original game binaries,
`.tre` archives, extracted assets, credentials, or private server data are in
this repository.

## One prompt for a contributor

Open a Codex project in a **locally owned SWGEmu Pre-CU installation folder**
containing `SWGEmu.exe` and `.tre` files. Prompt Codex:

> Set up https://github.com/TrigsC/SWG-64bit.git in this SWG folder.
> Follow its README and AGENTS.md. Show me three available GitHub issues suitable
> for this Mac, recommend one, and wait for me to say “work on it.”

Codex should clone the repository as `modernization`, run
`python3 modernization/scripts/setup.py`, and report the test result. The setup
keeps tools and generated files inside `modernization`; it reads the contributor's
existing game files in place and does not upload them. The script checks for
Apple Silicon and Xcode Command Line Tools before downloading pinned dependencies.
It builds an ignored object-template index from the contributor's own `.tre`
filename tables. The recorded animation matrices, packet fixtures, and
server-derived terrain grading map used during private development are not
distributed; fidelity tests needing them remain local-only. Terrain grading
that depends on the private server map is currently unavailable in a fresh
community checkout.
For the verified `SWGEmu.exe` revision, a contributor can regenerate a local
animation rotation reference from their own executable and `.tre` archives:

```sh
python3 modernization/scripts/generate_local_animation_reference.py
```

The script reads the local archive search order, writes only to ignored
`modernization/tools/local-assets/animation_rotations.tsv`, and refuses an
unverified executable revision. It does not connect to a server or require a
Windows VM. Live controller/session recordings remain optional private
diagnostics; rerunning a session will not reproduce their IDs and timing
byte-for-byte.

Run `python3 modernization/scripts/setup.py --check` to inspect prerequisites
without installing anything.

GitHub Actions runs an asset-free Apple Silicon build and source tests with
`python3 modernization/scripts/setup.py --ci`. This mode is for CI: it skips
the local game-file check and asset index, and does not make the resulting app
playable without the contributor's own installation. Actions also compiles the
Windows x64 protocol/client core without a renderer; this is not a Windows
launcher or a verified Windows graphics build.

Once set up, say **“work on it”**. Codex should read the selected GitHub issue,
check for an active claim or PR, implement one scoped change, run the focused
tests plus the source-only suite, and open a draft PR with evidence and limits.
Codex can propose issues without logging into a server. See [CONTRIBUTING.md](CONTRIBUTING.md).
`python3 modernization/scripts/work_queue.py` lists unclaimed public issues;
Codex should also inspect comments and open PRs before taking one.

## Manual setup

From the owned game folder:

```sh
git clone https://github.com/TrigsC/SWG-64bit.git modernization
python3 modernization/scripts/setup.py
```

The resulting app is `modernization/live-client/SWG Native Client.app`.
Credentials are entered in its UI and are not saved by the launcher. The
server address is editable. By default it reads the local `swgemu_login.cfg`;
the setup script does not contact any server. Use only servers that permit this
experimental client. We have not established permission for the public
SWGEmu service to accept it, so `login.swgemu.com` is **not** an automated test
target or default baked into this repository.

## How the tests work

`python3 modernization/scripts/test.py` runs the source-only suite, with no
login or game assets required. `--assets` additionally runs archive/asset parity
tests against your own local installation. For HUD/chat tests it extracts a
bounded set of UI, font, localization and object-template inputs into ignored
`modernization/tools/local-assets/tre_export/`; no extracted files are committed
or uploaded. `--loopback` includes two localhost
UDP tests that may need Codex sandbox permission. `--list` shows the source test set.
The current client still has incomplete gameplay, UI, audio, networking and
platform parity; see [WORK_QUEUE.md](WORK_QUEUE.md) for areas to contribute.

For binary research, use [RESEARCH.md](RESEARCH.md) and its pinned REA setup
helper. REA and Ghidra are optional for routine implementation. Install them
only for a scoped research issue. Publish a behavior specification and synthetic
tests, never raw or derived asset matrices, recorded packet fixtures, client
binaries, decompiled code, private captures, or account data.
