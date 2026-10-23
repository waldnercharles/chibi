# WIP query handoff — 2026-10-23

This is an existing-source checkpoint for dot's cloud team, not a completed feature or an integration approval. Charles asked owners to push current work without finishing features or starting builds/tests. No build or test was started for this handoff. Required checks and publication/review holds are not waived.

## Source and ancestry

- Repository: https://github.com/waldnercharles/chibi
- Branch: `codex/query-handoff-20261023`
- Exact reviewed source commit: `4d9b2d2f86c671d169a42b7d660dce353a8433fc`
- Its parent: `cbed73d239f7b50d0a2a6c5644afa4cc8cdf4e91`
- Exact reviewed source tree: `4c3d1d844eec07fa02791d7b1b1f0b65d834a5a3`
- Current source plus PLAN overlay, before handoff files: `f7755c94e403c497b3ab1448b1a36d17d0ebb6f4`
- The source commit preserves all 178 changed paths, including 12 previously untracked source files. `checkpoint.json` gives paths and SHA256 identities. `evidence/query-validation/candidate-source.sha256` records the original 2,411-file product freeze.

The original query and lifetime working copies, indexes, local branches and primary dirty design notes remain untouched. There is no new merge commit. The inherited merge `afe04307bde9a43b625cba82f6a763a10f20349a` retains both original parents. This branch includes pointer foundation commits `5e254084a` and `cbed73d23`; do not blindly reapply another pointer branch over them.

## Active source versus receipts and drafts

Active source implements the flat `system(...)` and `query(...)` lists, implicit read-only body-local dt, copied component values, managed pointer bindings, ordered pure computed bindings/if guards, independent cursors and project33-to34 migration. Replaced system/query forms are removed; ordinary behavior handlers and callable split retain their contract. Empty `system()` remains manually attached and unqueried. Grouped syntax is deferred.

`PLAN.md` is the current notes overlay. `reviewed-source.patch` and `current-plan-overlay.patch` are recovery patches for source already present on this branch; do not reapply them to this checkout. `drafts/primary-design-notes.*` is an **unapplied**, separately based preservation of the primary checkout's original edit. `reviews/` contains read-only composition and future acceptance maps, not active implementations or executed proofs.

The separate lifetime checkpoint will be on `codex/query-lifetime-handoff-20261023`. Its reviewed tree is `b2c82d244774c095feb9d4c223740a1a85265099`; it adds private target-set runtime plumbing and ten unexecuted C cases. It is not authored automatic-inference support.

## Evidence and limits

`checkpoint.json` and retained logs distinguish passed, failed and unrun work. Final focused Release results are: source queries11/65,210 assertions; query values15/1,458; execution55/4,760; effects4/83,656; migration8/114; source systems11/334; preview4/55; runner17/173. Historical full formatting passed in76.0 seconds; the last incremental graph build took6.3 seconds separately. Tests include VM/JIT where the selectors choose them. No final executable hash was recorded for these older runs, so logs are not a retroactive exact-binary identity proof.

Earlier failures are retained, including the source query run with7 passed/4 failed; later `source-queries-2.log` passes11/11. Broad native/projects/language/tools survey failures were fixed in focused checks but their final required rerun remains unrun. Earlier material-suite success is historical and does not prove the final combined source.

**Still unrun:** final five Make groups, current-main/common integration, matching query bootstrap and isolated recovery, authored dependency inference, execution of the separate lifetime draft, and corresponding installed SDK/platform/Studio checks. Existing checked-in compiler resources are recovery/history assets; they do not establish a matching query-ABI22 compiler artifact.

## Dependencies and holds

Query preparedABI22 is distinct from collectionABI22 and pointerABI21. Main d0 isABI20 and lacks this pointer/query foundation. First common composition was planned to keep query's current pin_schema carrier temporarily; no collection resolver/CoW source has been imported here. Independent collection test authorization is satisfied, but that does not establish common-source eligibility.

Known separate inputs: corrected task source tree `6353f5a382785977b0b9948cfcbcde37facd0d0c` on base `81702f624bd025f4e3ae43daa5fd86fc89c48178` (its owner must supply the remote checkpoint); compiler handoff `0b1492796941d6bd4159dc8b920f7ca8fe35c2c9`; coordinator inventory `93d1a5d9cf46b1dfdbdb024207d9dbc6e3278ae7`. These are **not composed** into this branch. Review maps preserve rich resolver/effects/cached-body requirements and the schema-independent record-validation cache boundary.

Build outputs, local SDK/dependency caches, diagnostic executables and Git indexes remain local and were excluded. Raw text logs retain original Mac paths as provenance; they are not required cloud directory names. Third-party dependencies should be provisioned from the repository's manifests. There is no local-only product source outside the preserved candidate; primary notes and review documents are classified separately above.

## Cloud next steps and ownership

1. Verify this remote head, source commit/tree and manifests, and separately verify the lifetime branch and other owners' checkpoints.
2. Reconcile eligible main, pointer/task and rich compiler contracts deliberately. Preserve current record-check budgeting/hash_work, exact rich resolution and known effect masks in query purity, named-result ownership, ordered guards and module/cursor/component-cell lifetime. Do not compose equal-numbered ABI drafts by name.
3. Generate matching compiler/runtime resources only after the shared contract agrees; perform affected and required checks, then bootstrap/recovery and project/SDK/Studio acceptance on that exact source.
4. Implement and execute actual authored target-dependency inference. A dependency lasts until task completion even after final component access; removal suppresses later completion effects without rolling back prior effects. The acceptance case must infer it without a marker, host-installed binding or later component read. Private C binding tests cannot substitute for that gate.

This owner will make **no further overlapping implementation changes after handoff** and holds the candidates while cloud confirmation is pending. No branch/worktree deletion, merge, release, default flip or publication action is authorized by this checkpoint.
