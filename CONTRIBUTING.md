# Contributing without prior Git experience

1. Clone and run the one-command setup from [README.md](README.md). Codex can
   perform these commands for you.
2. Ask Codex for a few `available` issues. Look at the issue comments and PRs;
   a claimed issue is busy. Say “work on it” to begin the recommended one.
3. Codex creates a branch, makes one focused change, runs tests, and opens a
   **draft PR**. A PR is a review proposal; it does not change the main branch.
4. Review the PR's behavior and test evidence. A maintainer decides when it is
   complete and ready to merge.

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
`git diff --cached --name-only`. Keep locally generated material in ignored
`tools/local-assets/`.

`login.swgemu.com` is not a substitute for a controlled integration server:
the public service can change and may restrict unofficial clients. Use local
fixture/replay tests for PR checks. A public login requires current operator
permission and a user-initiated manual check; never use CI credentials.
