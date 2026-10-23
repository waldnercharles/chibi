# WIP: CI compatibility drafts and validation handoff

This is a recovery and evidence checkpoint for cloud takeover. It adds no implementation to the active engine, starts no build or test, and is not a merge-ready candidate.

The repository is [waldnercharles/chibi](https://github.com/waldnercharles/chibi). This branch starts at the already-pushed CI repair `8f9dbf5e8edb68269c820bc99baf84bf18bc9c17`. That landed repair is reused, not duplicated. Every new tracked file is below this handoff directory. `checkpoint.json`, `source-manifest.json`, `reused-artifacts.json` and `manifest.json` are the detailed inventory and integrity records. Historical patches and logs use lossless `.b64` text transport; `transport.json` records every decoded SHA-256. Decode with `python3 decode-evidence.py /new/output/directory` to read or apply the original bytes. This performs no builds or tests.

## Source and draft boundaries

| Draft | Original base | Preserved state |
| --- | --- | --- |
| Typed compiler-host CI compatibility | HEAD `30cd287d1b86250d13b42c3716a94d747fa87a9b`, staged tree `0b5c20a7222a9d171bfcbe35570c184a78f4c8d1` | Three-file patch `aeaa4e46…`, full candidate files, source inventory, plans and reviews in `drafts/typed-host-compatibility/`. Applied to the typed owner’s candidate, but unapplied over this branch’s active `8f9` source. |
| Protocol9 build-host transition | Tree `9fa28696afbd513529541f5f87b14b0b2f9ae2b4` | Five-file patch `30112d92…`, exact base/candidate copies and reviews are reused from the typed evidence commit via `reused-artifacts.json` (logical prefix `evidence/protocol9/`). Its independently reviewed result is tree `6ef6b76e5c7e83b328e2973cc15c42fc79804043`. |
| Typed SDK oracle context | `0b5c20…` to `966dbacc…` | The fixture-only context patch is in `evidence/typed-readiness/`; readiness receipts are reused from the typed evidence checkpoint. This is typed-owner context, not a new CI implementation. |

The full active typed source and resources are already pushed on `codex/typed-assets-source-handoff-20261023` at `2227496827abb8ef59578216b65573f8cc6fae39`, whose exact tree is `6ef6b76e5c7e83b328e2973cc15c42fc79804043`. The owner’s evidence archive is `codex/typed-assets-handoff-20261023` at `6160eb6fab0f3fcabcae8de82424c9b350d33981`. Their remote heads and artifact bytes were verified. `reused-artifacts.json` maps the protocol9 logs, snapshots, reviews and gate receipts to that archive; they are reused here instead of duplicated. Studio remains a separate owner checkpoint. The source-only WIP commit has actual parent `30cd287d1b86250d13b42c3716a94d747fa87a9b`; the pending merge parent below is preserved in metadata, not adopted as its second Git parent. The original typed HEAD and pending merge parent remain `30cd287d…` and `fa1b7b2bb297eacbbed6aec6f03c4260355db97c`. Do not apply these drafts to the landed `8f9` source or compose them with the separate compiler/LLVM candidates merely because they are checkpointed.

Protocol9 establishes compiled-bootstrap compatibility only. It does not advertise prepared build artifacts. The normal Make upgrade rejects earlier protocol responses and uses the fresh stage-zero host. A transitional SDK 68e host reports 8 while already supporting compiled bootstrap; older released 8 hosts still require the retired bootstrap contract. The generic pinned `.2` compiler-host consumer is a separate path: it retains `compiler-runtime-2026-10-22.2`, without `QueryTarget` or `NativeRecovery`. Producer query/recovery and any new runtime publication remain separate gates.

## Actual validation

All current-source runtime evidence below concerns typed tree `6ef6b76e…`, not this documentation checkpoint’s full tree.

| Check | Actual result |
| --- | --- |
| New regression under original Make8 gate | Expected red: 10 cases passed, 1 failed, 164 assertions; 33.178s wall. The protocol8 route chose authored execution when recovery was required. |
| Ordinary `make build-release`, no host override | Passed in 19.637s. The existing Release 8 host took normal stage-zero recovery and produced reference/Graph hosts reporting 9. Binary and command identities are retained. |
| `build-toolchain` on final source | 11 cases / 168 assertions passed; 34.028s wall. |
| `build-process` on final source | 45 cases / 8,975 assertions passed; 23.655s wall. |
| Required native group | 632 cases / 236,737 assertions passed. Test wall 1,451.145s; total with build 1,733.444s. 75 actual summary rows, 73 configured selectors. |
| Required materials group | 110 cases / 270,494 assertions passed. Test wall 65.630s; total 165.991s. |
| Required project group |**Failed**, exit2, total 9,910.833s. Mesh 10 / 9,971 and model-import 22 / 345 passed. Validate-project 27 passed / 1 failed / 329 assertions. Later 12 project selectors did not run. |
| Required language and tooling groups |**Unrun.** They were not started after the project failure and were not restarted for this handoff. |
| Workflow/source checks |Both candidate workflows passed actionlint; patch whitespace and independent source reviews passed. These are historical source checks, not new runtime validation. |
| PowerShell / current hosted proof |AST parse and actual `Prepare`/`Prove` remain unrun. Local `pwsh` was absent. A reviewed clean typed checkpoint is required before the script’s source-identity checks can pass. |

The concrete project failure is `test/tools/validate_project.c:279`: `run_scenario` for `Constructor` returned `validation_failed`, **“scene has no entities”**. The complete failed log is remotely preserved in the typed evidence checkpoint; resolve logical path `evidence/protocol9/required-test-projects.log` through `reused-artifacts.json`. Its SHA256 is `88e66e3227f4a5c0b0f86005abe8d209c794493fd3bbbcd885ca51df0cc35695`.

The model-import capacity case itself **passed**, taking 9,299.756898 pico CPU seconds. The reported chat/model-capacity interruption is not that test’s outcome. The fixture intentionally reads 1,022 native documents, then 1,021 after a deletion, plus prototypes: 2,045 document reads across two operations. Persistent cache configuration was enabled/default; the records do not prove full recompilation on the second pass. No global duration ceiling, cache change, fixture reduction or profile change was introduced. Pico timings are CPU time; the group’s test wall was 9,802.406s.

## Execution ownership and dependencies

`execution-handoff.json` is the authoritative final ownership record. All recorded CI job PIDs and the model-import driver are absent in the read-only audit. No CI execution ownership remains. No process was stopped or restarted for handoff. Older reused `execution.json` and `plan.json` still say running because the chat stopped before sealing; they are retained unchanged as historical records and superseded by the handoff record.

Original dirty indexes, ignored artifacts, development branches and worktrees remain intact. Raw Git indexes, SDK packages, native executables, object/static libraries, archives and other machine-specific build outputs are excluded. Their hashes and actual compiler commands are retained where relevant. The full typed candidate/resources are now verified remote dependencies. Matching machine-specific SDK/host products still need reconstruction for the chosen validation environment. Archived absolute paths are provenance, not portable dependencies; remap them before using historical local runners. Do not run those runners unchanged or weaken their clean-source checks.

Existing merge/release/default holds and publication denials remain in force. No new native producer implementation, runtime release, pin replacement, workflow dispatch or compiler/LLVM implementation is performed here. Source review does not clear the failed Constructor gate or establish prepared-artifact, cross-platform or default readiness.

Cloud should verify this remote commit, every local `manifest.json` payload hash and the reused remote blob hashes in `reused-artifacts.json`. Use the existing typed source/evidence checkpoints, rather than creating another equivalent snapshot. Repair the Constructor failure on the chosen eligible cloud candidate; retain this original failure. Run the remaining appropriate gates and platform checks. Actual generic pinned `.2` PowerShell proof follows a reviewed clean source revision. Native query/recovery/new producer publication remains separate.

I will make no further overlapping implementation changes after this handoff and will hold these candidates pending cloud confirmation.
