# WIP Project policy and pair publication handoff

Existing source-only work retained for cloud takeover on 2026-10-23. Repository `waldnercharles/chibi`, development branch `codex/selfhost-project-policy-20261022`, base `5c28191b3ea53afa232c34e37b14fd9b01024aef`.

The checkpoint adds this handoff directory only. No active runtime, policy, build graph or SDK file is replaced. `draft/candidate/` contains 16 complete reviewed proposed files; `checkpoint.json` lists their implementation paths and copied-file hashes. The frozen manifest hash is `2b69b9426b491f0da95ddb760276f4b7738260cc6269786ec2814006323d162f`. All 16 copied source hashes were verified against that manifest.

`draft/README.md`, manifests and review receipts retain their original historical status text. Independent kernel, host/policy and graph reviews were source-clear, including formatting review. The original live untracked Project remains unchanged at SHA256 `f7987d0577e433844069b6d498b4cb200ac34bdd3aaefbf6d17eda34af7644fb`; its full bytes are preserved remotely by `draft/candidate/test/fixtures/toolchain/Project-before-pair.chibi`. The corrected proposed Project is SHA256 `522b76cadc40f8e2ccb9ca730f5c336f6cb98286cb2b18681ac101ae5b4129ce`.

## Status and dependencies

No compilation, syntax/type check, C test, policy execution, prepared941 execution, graph parity, SDK packaging/import, native/selfhost Terrain parity or default adoption has run for this draft. Its 16 new C cases are source only. WIP publication does not satisfy independent review of a future combined tree or any test, merge/release or default hold.

The draft combines per-file baselines. It is not a patch to apply directly to this branch or current main:

- Compiler source tree `1a59410022d128108e7744cf2b5641047a44d713`, now preserved by source commit `aec822eb5d98cbdf987f81766e33cf02e2b33308` on `codex/selfhost-game-support-20261022`. It supplies the prepared host protocol8/runtime/CLI contract.
- Typed tree `6ef6b76e5c7e83b328e2973cc15c42fc79804043` supplies the recorded native authoring, CMake, graph and test baselines. Its protocol9 route is a distinct contract, not an automatic substitution for source32.
- SDK warm CMake SHA256 `2a563172deba768fc6dfb36497766f02861b936b889a05849259878e1e8ab0a1`, plus the separate SDK owner's Engine/NativeBuildFiles filters and reviewed HOST/2 producer/generated941 proof.

These other-owner dependencies need their own verified remote checkpoints. This branch does not claim to contain their full source trees. Complete candidate files and exact input hashes are retained so composition can be reviewed after those handoffs.

## Contract and remaining holds

Reserved binding941 is `build.publish_pair(string,string,string,string) -> int`, HOST effect, all arguments c_string. Status0 means both committed;1 means not committed with prior pair/absence restored;2 means restoration incomplete;3 means committed with housekeeping retained. Publication supports ordinary-error rollback, not atomic reader visibility, writer locking, crash journaling or fsync durability. Cancellation can be observed after commit. Full details and authored case inventory are in the original draft README.

PREPARE/1 and INSPECT/1 stay unchanged; the prospective witness is HOST/2. No witness has been generated or executed. Native fault1/2/3 coverage is separate from the planned generated normal0 proof.

Next: verify the remote checkpoint and all copied hashes, collect missing typed/SDK checkpoints, compose exact eligible sources, independently review that combination, build and run the authored C cases plus actual prepared/policy/SDK/game acceptance. Preserve failed predecessors and unrun coverage. The cloud team takes over implementation and validation after confirming the checkpoint; this chat will avoid overlapping implementation while confirmation is pending.

Local ignored draft history, baseline copies and pre-format snapshots remain under `.chibi/project-policy/pair-transport-draft/`. No branches, worktrees or uncommitted files were deleted. No additional owner chats were contacted.
