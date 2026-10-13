# Contributing without prior Git experience

1. Clone and run the one-command setup from [README.md](README.md). Codex can
   perform these commands for you.
2. Ask Codex for a few `available` issues, or run
   `python3 scripts/work_queue.py --platform mac --limit 3` from the repository.
   Use `--platform windows` on a Windows machine or `--platform research` for
   source-only research. The default is `mac`; `--platform all` shows every
   unblocked platform. The queue prints platform, needs, and dependencies,
   and skips assigned issues, active comment claims, and issues referenced by
   open PRs. A comment claim expires after 14 days unless renewed; labels and
   assignments still need to be cleared by their owners. Check the issue and
   open PRs before starting. Say “work on it” to begin the recommended one.
3. Codex makes one focused change and runs tests. If you do not have write
   access to this repository, Codex creates or reuses a fork in your GitHub
   account, pushes an issue branch there, and opens a **draft PR targeting
   `TrigsC/SWG-64bit:main`**. The PR appears in this repository; you do not
   need to run Git commands or request write access. Contributors who already
   have write access use an issue branch here, never `main`.
4. Review the PR's behavior and test evidence. Only the repository owner
   decides when it is complete and merges it. A PR does not change `main`.

If Codex cannot access your GitHub account or create a fork, it should keep
the tested work local, tell you the one-time connection or fork step needed,
then finish the push and draft PR after access is available.

If GitHub is unavailable, the queue points to [WORK_QUEUE.md](WORK_QUEUE.md).
That offline list cannot show live claims, assignments, or PRs, so check them
when GitHub returns before publishing work.

Issue labels: `available`, `claimed`, `needs-mac`, `needs-windows-reference`,
`needs-assets`, `research`, `good-first-issue`. A brief “I am working on this”
comment is a claim; it is not a permanent lock. Before starting, check whether
someone already has a related draft PR. If GitHub is unavailable, choose a local
item from WORK_QUEUE.md and defer the PR until access returns.

For every PR, describe a concrete before/after behavior, link the issue, list
commands and results, and separate verified facts from inference. Include no
copyrighted game assets, recorded packet fixtures, derived animation matrices,
or private runtime records. Do not submit copied
decompiled source from SWG.exe or Core3. Preserve the upstream non-commercial
license. Mac reviewers should use their own asset files and account if a manual
connection is relevant and permitted by their server.

Before pushing, run `python3 scripts/publication_audit.py --summary` and
`python3 -m unittest discover -s scripts/tests -p 'test_*.py'`. The same
source checks run on PRs. They block known asset/capture file types and
obvious embedded records; research-note matches are advisory and need human
review. GitHub checks run after a push, so inspect your staged files first:
`git diff --cached --name-only`. GitHub Actions also builds and runs the
asset-free Apple Silicon source suite and compiles the Windows x64 client core.
The Windows check does not validate a renderer or launcher. Keep locally
generated material in ignored `tools/local-assets/`.

`login.swgemu.com` is not a substitute for a controlled integration server:
the public service can change and may restrict unofficial clients. Use local
fixture/replay tests for PR checks. A public login requires current operator
permission and a user-initiated manual check; never use CI credentials.
