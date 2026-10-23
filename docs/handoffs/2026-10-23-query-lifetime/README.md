# WIP private query-lifetime handoff — 2026-10-23

This separate draft preserves existing source for cloud takeover. Charles requested WIP checkpoints without finishing features or starting builds/tests. No new build/test was started and no acceptance/review/publication hold is waived.

## Exact source

- Repository: https://github.com/waldnercharles/chibi
- Branch: `codex/query-lifetime-handoff-20261023`
- Exact source commit: `07a271210bf107f0f736cf2facd31952b7e48444`
- Parent/query source commit: `4d9b2d2f86c671d169a42b7d660dce353a8433fc` (tree `4c3d1d844eec07fa02791d7b1b1f0b65d834a5a3`)
- Exact reviewed lifetime tree: `b2c82d244774c095feb9d4c223740a1a85265099`
- Current candidate with later PLAN-only decision overlay: `52d00ef8c1801e670931b00f60f6dbcd428ae42c`

The original lifetime checkout HEAD remains `cbed73d239f7b50d0a2a6c5644afa4cc8cdf4e91` with its original dirty/index state intact. This checkpoint makes its existing incremental relationship explicit: eight product files and PLAN differ from the frozen query tree. `checkpoint.json` lists paths and hashes; the2,411-file product manifest is retained in `evidence/lifetime-source.sha256`. No new merge parent or common composition is introduced.

The query source/evidence branch is separately pushed at `019cc599233bce30c646f2d7f5ed96ab506f0e79`, `codex/query-handoff-20261023`; consult its handoff for the broader query source and historical focused results. This lifetime branch's parent is the exact query **source** commit, not that later evidence commit.

## Active source and private contract

Active files add `ecs_component_cell_retain/alive` and private `program_task_start_bound`, a deduplicated retained original-cell target set, child inheritance, program-world admission and cancellation checks at execution boundaries. Losing any bound original target reuses existing child cancellation/typed Cancelled/request cleanup. A completed successful result remains stable after later removal. Merely storing/passing a pointer does not infer a dependency.

This is not authored lifetime syntax or automatic compiler inference. Its ten C --program cases inject bindings through a private host wrapper; they are oracles for the mechanism only. Targets selected later, descendant propagation and the actual inference producer remain unresolved. Query preparedABI22 remains distinct from collectionABI22 and pointerABI21.

The final human lifetime decision is preserved in the current PLAN overlay: a dependency remains until task completion, even after final component access. Removing it during a later wait cancels sound/completion effects; earlier completed effects remain. Compiler liveness must not unbind it early, and copied-value work must actually be independent to survive ordinary child cancellation.

`reviewed-source.patch`, `current-plan-overlay.patch` and `evidence/lifetime-product.patch` describe source already active here. They are recovery/apply-to-explicit-base material, not extra unapplied feature drafts. No new collection resolver, rich compiler, corrected task or LLVM implementation is imported. Composition notes remain planning only.

## Evidence

Source reviews are summarized in `review-status.md`. Six changed C units passed syntax-only checks in0.740 seconds using this checkout's source headers and original query generated/dependency includes; changed C/header format22 and diff checks passed. No link or behavior execution is proven. Original command/compiler binary hashes were not recorded.

**Every lifetime execution test remains unrun**, including the ten new --program cases and modified ECS retained-cell case. Historical query baseline passes do not cover this delta. The five required groups, matching bootstrap/recovery, current common source integration and actual authored inference all remain open. No Linux/Windows/installed SDK/Studio execution claim applies here.

Build directories, machine-specific executables/libraries, generated SDK/dependency trees and temporary Git indexes were excluded and remain local. Raw syntax logs contain provenance rather than a cloud-ready build environment. Cloud should generate its own matching headers/dependencies from the final reconciled source.

## Next steps and handoff hold

1. Independently verify both remote checkpoints and source/manifests.
2. Reconcile eligible main, pointer/task and rich compiler source with the query owner boundaries, retaining existing query carrier temporarily until a later carrier migration is eligible. Preserve the schema-independent record cache and separate schema/provenance checks.
3. Execute these private runtime cases on the combined source, then implement and test actual authored dependency inference. The acceptance case must access the target for the last time, commit an independent counter, wait, then suppress completion after target removal/re-add; no marker, host-installed binding or later target access can substitute.
4. Generate matching resources and run the appropriate required/bootstrap/project/platform acceptance before merge/default/release decisions.

This owner will make **no further overlapping implementation changes after handoff**, and holds all original candidates pending cloud confirmation. No branches or worktrees are deleted or discarded. No new producer, LLVM, default, quota or deployment change is requested locally.
