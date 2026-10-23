# Values, tasks and managed-pointer WIP handoff

Repository: https://github.com/waldnercharles/chibi. Prepared ABI **21** throughout this handoff. No ABI22 draft was composed into it. This is retention for cloud takeover, not a release or merge-ready claim. No implementation, build, test, bootstrap regeneration or default change was performed while making these checkpoints.

## Retained checkpoints

| Role | Branch | Full commit | Source tree | Parent |
| --- | --- | --- | --- | --- |
| tested | `codex/wip-values-tasks-tested-20261023` | `f764c07cab5a6371adcfacc3297ed96ba4ea3300` | `6353f5a382785977b0b9948cfcbcde37facd0d0c` | `81702f624bd025f4e3ae43daa5fd86fc89c48178` |
| lifetime_docs_and_live | `codex/wip-values-lifetime-docs-20261023` | `87d65fd981b01648560592153579231e55168812` | `f0fe82ef0e09532f74c356fc194edf556f78d13d` | `f764c07cab5a6371adcfacc3297ed96ba4ea3300` |
| failed_recovery_only | `codex/wip-values-tasks-recovery-20261023` | `74a0eb2ce846a1fc94f2747ecb22843359eb2b9e` | `93c325e56332df8d38c501269ed47c21d37f4e8c` | `81702f624bd025f4e3ae43daa5fd86fc89c48178` |
| managed_pointer_predecessor | `codex/wip-managed-pointers-20261023` | `f04d9d37822edb44a8fb1d1f6f68e5546eb8e049` | `310bf387bc87975d7e263ae88406b4cfaa157d34` | `36f3e9350145242e0c0fc48f9f2e459dbd7c14ef` |

The active continuation source is the lifetime-docs branch. Its parent retains the exact tested task tree; only `PLAN.md`, `docs/language-friction.md`, `docs/value-runtime-integration.md` and `docs/value-semantics.md` differ. The recovery branch is the original **failed, unpreparable** task draft and must not be integrated. The pointer branch reuses the existing committed predecessor without rewriting it.

The evidence branch adds this `handoff/values-tasks-20261023/` directory to the live/docs source. It does not alter product source. `manifest.json` and each `snapshots/*/source-manifest.json` provide full identities, comparison bases, changed paths, modes, Git blob IDs and SHA256 values. The original staged/unstaged/untracked split is retained in `original-worktree-state.json`: the index equalled HEAD, and `Tasks.chibi` and `TaskResults.chibi` were untracked but are included in both task snapshots. Original worktrees and branch heads remain untouched.

## Managed-pointer ancestry

The pointer source commit is `36f3e9350145242e0c0fc48f9f2e459dbd7c14ef`, based on `ba8c2851fad53de022f322c0af4e994dcff59204`; `f04d9d37822edb44a8fb1d1f6f68e5546eb8e049` refreshes its portable bootstrap. The values base `81702f624bd025f4e3ae43daa5fd86fc89c48178` descends from integrated artifact `22dc0d935b1478b519801597f16d547974d30c67` and merge `60a407503d287316945f9a80457d051ff1781233`. That merge retains parents `a388f5ea0b9247ace869ea3a0ebb78103410ffe9` and `1bc97b7305ee91102d3ee035e4852aa5341df31c`.

The tracked portable compiler artifact is preserved as part of original history. It is ABI21/version6/minimum2, revision60a407503, SHA256 `3fb0d760a6bafa428b46beb42bfc8759b9d65e6e470f670443fa75c868fc3fe2`, 13,422,214 bytes. It predates the task changes and is **not** a task-containing regenerated bootstrap. Machine-specific executable/build outputs are excluded.

## What the source implements

Managed pointers provide typed cells, automatic retention/cycle collection, field projections and native/Chibi compiler support within the limits recorded in `docs/managed-pointers.md`. The task prerequisite adds the existing task subset to the Chibi package compiler: eager start until suspension, yield, direct/handle await, cancellation/deadlines and Delay, with typed success distinct from Cancelled. Multiple/union task results and broader task/native async parity remain unsupported.

Whole-task inferred component dependencies are an approved **design**, not implemented here. They last through completion, including after final access. Removal suppresses remaining continuation effects; earlier effects remain. No explicit lifetime marker or try/catch is approved. Targets selected later and propagation to already-running children still need design. Ref removal, builder/collection CoW, query-cell lifetime integration and their ABI22 representations remain separate work.

## Validation and review status

- Corrected task tree6353: seven historical focused selectors passed, **11 cases / 101,682 assertions**. Details, commands and immutable file/log hashes are in `receipts/values/task-prerequisite-review/corrected/{review.md,manifest.json}`. Compiler/runtime sources were unchanged after build4; collection checks used build4, task/base/behavior/bounded used build8, writer used build9. Earlier driver hashes were not recorded. Final build9 hashes do not retroactively identify earlier runs.
- Original tree93c: preparation failed after source review. Reserved identifiers, required constructor fields and typed append values were corrected in6353. Additional test-fixture failures and the writer failure (exit139) are preserved. The failed source and logs remain recovery-only; they were not relabelled as passing.
- Managed-pointer predecessor: historical syntax/format/source-edit/semantic/borrow/execution/program, compiler component/package and sanitizer checks passed as listed in `docs/managed-pointers.md`. Bootstrap passed 16,775 assertions and isolated recovery reproduced13,104,075 bytes. Raw diagnostics also retain earlier execution and collection failures/crashes. The ledger and later successful logs distinguish those iterations; not every raw log has an immutable source identity.
- Integrated pointer/main predecessor: `docs/value-runtime-integration.md` records six focused selectors (14 cases total) and a17,478-assertion bootstrap, matching artifact refresh and source-embedding verification. Debug/sanitizer, Wasm and broad suites were not run for that integration. These predecessor results do not cover the task delta.
- Independent source reviews covered pointer ownership/compiler integration, task corrections, writer migration and common compiler interactions. `receipts/values/task-prerequisite-review/common-reconciliation.md` and `systems-lifetime-review.md` are **source-only** maps with historical baseline IDs, not combined implementation or execution proof.
- No broad task suites, task bootstrap reproduction, current cloud rich-compiler combination, component dependency inference/lifetime execution, CoW integration, renderer, Studio, browser or cross-platform validation is claimed. No new builds/tests ran during checkpointing.

`receipts/manifest.json` lists every copied text receipt and its original path/hash. Paths in historical logs describe the original Mac environment; source manifests and patches are portable. `receipts/test-log-summaries.json` extracts historical summaries without upgrading them to current passes. The old `explicit-ref.patch` is an unapplied historical transport patch whose exact original base was not recorded; it must not be reapplied to current source. Its implementation is already superseded in the pointer history.

## Handoff boundaries and next steps

Cloud owns compiler/LLVM. Use its delivered rich compiler structure when reconciling pointer/task support: retain early synthetic task-result declaration preparation, rich callee/native resolution, cached body metadata and cancellation WRITE effects. The existing common-reconciliation map identifies the interaction points, but cloud must verify them against its current branch.

Then agree a common query/collection/target schema and produce one matching bootstrap; the independently numbered ABI22 drafts are incompatible until reconciled. Validate actual authored inference using final-component-access -> durable independent effect -> wait -> completion, removal/readd, Cancelled and cleanup controls. Keep the original reviewed/tested checkpoints available. No old artifact or private host-bound runtime test proves that combined result.

All existing review, validation, merge, release and deployment holds remain. This handoff grants no permission to weaken checks or change defaults. There are no missing active source files in this values/pointer candidate. Local binaries, generated compiler input/duplicate seed and temporary indexes are omitted and can be rebuilt; separate owner drafts must come from their own verified handoffs.

**The local owner will make no further overlapping implementation changes while cloud verification is pending or after handoff.** Worktrees and original dirty state remain retained. Only checkpoint refs and this portable evidence directory were created.
