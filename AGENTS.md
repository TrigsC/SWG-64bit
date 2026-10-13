# Codex contribution instructions

This repository is checked out as `modernization` inside a contributor-owned
SWGEmu/Pre-CU installation. Never commit, upload, modify or redistribute the
parent installation's EXE, DLLs, `.tre` files, extracted assets, credentials,
configuration, session logs, screenshots, packet captures, recorded animation
matrices, or derived asset indexes. Do not put local
absolute paths, server addresses, character names or secrets in a PR.

Before work: read README.md, CONTRIBUTING.md, and WORK_QUEUE.md; inspect open
GitHub issues and PRs. Prefer a small `available` issue without a current claim.
Show 2–3 relevant choices and a recommendation. If the user says “work on it”
without selecting one, take the recommended issue. Mark it as claimed with a
brief issue comment if GitHub access allows, and link the issue in the draft PR.

Use the original SWGEmu/Pre-CU client and REA only for narrow, read-only behavior
research when a concrete question requires them. Use original assets from the
local installation for offline tests. Do not execute unsolicited admin commands,
automated gameplay or bulk traffic on public servers. No production-server
login is part of CI. The app remains Mac ARM64 only until Windows x64 has an
actual build and test result.

Run focused tests and `python3 scripts/test.py` before opening a draft PR.
When the local `.tre` files are available and relevant, run
`python3 scripts/test.py --assets`. State the test results and any unverified
live behavior. Keep the upstream license and attribution intact. Keep generated
dependencies, builds and archives out of Git. Never claim complete client parity
from a passing unit test.
