# Focused reverse-engineering workflow

The repository includes independent client source and synthetic tests;
it does not vendor the original executable, third-party server source, REA,
Ghidra, or decompiler databases. The contributor's own SWG installation is
the reference artifact. REA is an optional tool for a scoped issue.

## Check existing research first

[Stardust Client Research](https://github.com/Levarrishawk/Stardust-Client-Research)
is a public, research-only record for another SWG client executable. Start at
its [research index](https://github.com/Levarrishawk/Stardust-Client-Research/blob/main/START_HERE.md)
and read the narrow contract and evidence checkpoint for the feature at hand.
Its reported coverage is a weighted estimate of observable behavior, **not** a
complete decompilation or a count of recovered functions. Its implementation
lives in a separate project.

Useful entry points include the
[packet-handler map](https://github.com/Levarrishawk/Stardust-Client-Research/blob/main/docs/PACKET_HANDLER_MAP.md),
[localized prose and grammar investigation](https://github.com/Levarrishawk/Stardust-Client-Research/blob/main/research/checkpoints/ANALYSIS_CHECKPOINT_2026-09-21_STRING_VARIABLES_AND_GRAMMAR.md),
[asset pipeline](https://github.com/Levarrishawk/Stardust-Client-Research/blob/main/docs/ASSET_PIPELINE.md),
[terrain architecture](https://github.com/Levarrishawk/Stardust-Client-Research/blob/main/docs/WORLD_TERRAIN_ARCHITECTURE.md), and
[skeleton pose research](https://github.com/Levarrishawk/Stardust-Client-Research/blob/main/docs/SKELETON_ANIMATION_POSE_CONTRACT.md).
Follow each document's evidence links and stated confidence; a server-source
correlation alone does not establish original client behavior.

The Stardust findings are pinned to the executable hash in that repository's
README. A differently named file can have identical bytes, but a hash mismatch
means its addresses and exact instruction claims cannot be transferred directly.
Related clients may still share protocols and asset formats. For each adopted
finding, record the external document link and revision, its evidence level,
the local client/asset/server revision used to check it, and what remains
unverified. Prefer a local synthetic or owned-asset regression. Use a narrow
read-only REA or Windows comparison only when the existing record leaves a
question open or versions disagree.

Public access to the research does not authorize copying its tools, text,
decompiled excerpts or third-party implementation code into this repository.
Link to it, write independent behavior specifications and tests, and check its
current license and the provenance of any material before considering reuse.

1. Form a narrow question, such as “what opcode and payload width does room
   delivery use?” Check existing source, tests and the relevant linked research
   first. Note which assertions are specific to the researched executable.
2. If a concrete binary question remains, run `python3 scripts/setup_rea.py`. This
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
