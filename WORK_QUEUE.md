# Current work queue

GitHub issues and active PRs are authoritative once the public repo is live.
This file is the offline fallback and a list of bounded initial issues.

| Area | First reviewable slice | Needs |
| --- | --- | --- |
| Chat rooms | Receive/submit room messages using the existing directory and membership state; verify Spatial and Planet routing | Mac, fixture tests, optional Windows reference |
| Chat tab management | Match original add/remove/rename tab controls and persisted per-tab subscriptions | Mac, original UI evidence |
| Creature motion | Smooth remote entity interpolation while preserving authoritative final positions | Mac, local synthetic or privately generated replay |
| Interior movement | Verify live door entry/exit, cell transitions and server permissions | Controlled server, Windows reference |
| Terrain grading | Restore building terrain modification from contributor-owned server configuration without bundling a server-derived map | Mac, owned server data |
| Audio | Device output, sound resource resolution and volume settings | Mac, owned assets |
| Windows x64 | Establish a separate x64 build, renderer backend and CI smoke; do not label the Mac app cross-platform before passing | Windows x64 maintainer |
| Packaging | Remove workspace-relative runtime dependencies and create a signed/notarizable Mac distribution | Mac, licensing review |

Each issue should define one trigger, expected behavior, source of evidence,
asset-free test, optional asset/live test, and what remains outside its scope.
