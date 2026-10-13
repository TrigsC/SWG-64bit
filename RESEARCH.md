# Focused reverse-engineering workflow

The repository includes independent client source and synthetic tests;
it does not vendor the original executable, third-party server source, REA,
Ghidra, or decompiler databases. The contributor's own SWG installation is
the reference artifact. REA is an optional tool for a scoped issue.

1. Form a narrow question, such as “what opcode and payload width does room
   delivery use?” Check existing source and tests first.
2. If binary evidence is needed, run `python3 scripts/setup_rea.py`. This
   invokes the pinned [REA project](https://github.com/morluto/rea) setup,
   which shows proposed Codex configuration changes for review. Install a
   supported Ghidra provider following REA's documented guidance; installing
   the CLI alone does not make native decompilation available. Restart Codex,
   run the provider doctor, then inspect the chosen binary read-only and bound
   the analysis to relevant functions/data.
3. Write down addresses, hashes, observed constants and uncertainty privately;
   commit only a small behavior specification and synthetic tests.
   Never paste decompiled functions, original client code, server source,
   account data, packet captures, raw or derived asset matrices, or game assets
   into the PR.
4. Test the native implementation with a deterministic fixture. Use an actual
   server only when the operator permits it and the behavior cannot be proven
   offline.

To reproduce the local decoded-animation reference on the verified client
revision, run `python3 scripts/generate_local_animation_reference.py` from the
repository. It reads the contributor's `SWGEmu.exe`, configured `.tre` archive
order, and two default animation clips, then writes an ignored TSV under
`tools/local-assets/`. The generated values are for local comparison only;
do not add them to Git. The command requires no server or Windows VM. Another
executable revision needs its decoder table checked before this generator can
be used for it. Recorded live controller traces have session-specific values
and are not a required input to the normal build or source tests.

The existing Mac code started from SWGVulkanClient at commit
`c098e0c4cffccd6eb4c809b785405f03b646ec6b`. Preserve its
[license](vendor/SWGVulkanClient/LICENSE.md) and identify any future third-party
source separately before incorporating it.
