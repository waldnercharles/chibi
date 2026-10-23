# WIP self-hosted compiler handoff

Cloud takeover checkpoint, 2026-10-23. This preserves existing work and evidence. It is not merge, release, bootstrap, game-support or default eligibility. No implementation, builds or tests ran for this handoff.

## Exact source

Repository: https://github.com/waldnercharles/chibi

Branch: `codex/selfhost-game-support-20261022`

Base: `d0de4a84ecf50badcab661411470bdf6f3da5fa2`

Source commit: `aec822eb5d98cbdf987f81766e33cf02e2b33308`

Source tree: `1a59410022d128108e7744cf2b5641047a44d713`

The source commit contains the exact reviewed 32-file candidate. `checkpoint.json` lists every changed path. This handoff directory is a later documentation/evidence addition; its full commit tree must not be confused with the source tree above.

The changes cover rich native catalog and callable-library compilation, ordered Append lowering, prepared-host invocation, behavior/state/results, host routing and fixtures. Project policy and native pair publication remain separate source-only work on `codex/selfhost-project-policy-20261022`. That draft also requires typed-assets and SDK-owner inputs.

## Checks and failures

- Completed: existing independent source reviews; all 32 working/index source hashes matched the frozen manifest before committing; whitespace check; v1/v2 recovery patches reconstruct their exact trees in temporary Git indexes. No behavior tests ran for the checkpoint.
- Earlier v1 tree `2acb927a94319b4bfff42fa56d5af613e4b664da`: recovered compiler preparation requested 212,975,283 bytes with 210,629,435 remaining. It failed before service binding. Layout work 231,747,812 was within its 268,435,456 limit.
- Earlier v2 tree `bd1414dfc0409a27615af086ee842ce863727f96`: initial reserve and service-table storage reservation passed; decoded-definition ownership failed later with 7,167 bytes remaining. Exit 1; publication/full load were not reached.
- Current v3 tree `1a59410022d128108e7744cf2b5641047a44d713`: independent source review clear; load, storage fit and execution unmeasured. There is no v3 success receipt.
- Unrun on v3: complete ordinary compiler load, debug parity, all five required test groups, compiler/actions/host checks, self-reproduction/bootstrap, fresh resources and native/selfhost project parity, installed SDK/default adoption and broader acceptance.

Original copied manifests retain their historical wording, including earlier review-pending statuses. `debug-watch-refactor/review.json` records the completed independent delta review. No old receipt attests to a newer source tree. The earlier checksummed 12,098,990-byte compiler artifact is not a compiler built from this candidate.

## Storage and lifecycle

The failed measurements concern prepared-closure aggregate admission and finalization/definition storage, before ordinary semantic execution. Owned-result lifetime is a separate contract. Process RSS is another measure; these receipts do not establish an RSS ceiling. Keep existing limits and accounting unchanged.

`compiler_host_invoke` already copies output into malloc-owned JSON before invocation teardown; that output can outlive the producer host. Raw `pin_value` headers can retain borrowed layout/type-name pointers. No authored Facts producer is implemented here, and no public API exports Code/Slot as its replacement.

## Evidence and local-only dependencies

`evidence/` retains the original failed measurement logs/receipts, source reviews, corrected unrun runners, and future library/system acceptance maps. These are historical tools and reports, not automatically portable cloud commands. Runner paths, macOS process tools and diagnostic binaries must be adapted/rebuilt and reverified on the receiving host. `diagnostics/instrumentation.patch` preserves the exact three-file diagnostic delta, verified against the original/instrumented hashes and applied in a temporary Git index. It is not active source. For diagnostic replay, use a separate throwaway checkout at the source commit, apply that patch and rebuild `chibi-build-host` with the command recorded in `native-build.json`; adapt and reverify the copied local-path runner before use. Keep the ordinary checkout uninstrumented. The original diagnostic binary is not portable and is not included. The ordinary runner's exact-index guard expects the source tree, not this documentation-added tree.

`recovery/` contains source deltas from the source commit to both failed candidate trees. Their reconstruction was checked. It contains no executable artifact and does not attest execution.

Local ignored state remains under `.chibi/game-support/combined-d0/`, including diagnostic binaries, CMake products, original captures and other scratch. No machine-specific executable or native archive is published. The invalid one-byte LLDB capture remains invalid and local. Diagnostic archives require a normal rebuild before use as production evidence. Bootstrap/runtime executable provisioning and third-party archives remain separate dependencies.

The source-only Facts schema and SourceMap live with the shader owner, not this checkpoint. Pointer ABI21, collection/query ABI22, typed runtime protocol9, and this ABI20/protocol8 prepared-host candidate are distinct pending compositions. Do not infer compatibility from version numbers.

## Remaining holds and next steps

1. Fetch and verify the remote source/checkpoint commits and source-manifest hashes before takeover. WIP publication clears no independent review or merge/release hold.
2. Reproduce a complete load on the exact candidate with an ordinary host and unchanged aggregate limits; preserve failures and phase-specific measurements. Do not rerun the old cancellation-race runners. The corrected runner hashes are recorded in `launch-cancel-review.json`.
3. Resolve source-load failure before claiming broader compiler execution. Then run the affected and required checks, compiler parity/bootstrap and project/resource/installed-route acceptance on the exact integrated tree.
4. Obtain the other owners' checkpoints before composing typed assets, SDK pair publication, pointers, collections, queries or shaders. The Project pair draft is mixed-baseline source, not a patch to apply blindly here.
5. The cloud team owns further implementation and validation after confirming takeover. This chat will avoid overlapping implementation while handoff confirmation is pending.

The former local queue required explicit CI release before heavy execution. Cloud can schedule its own isolated machine, while preserving all source, review, test and merge holds. Native LLVM remains opt-in. No compiler/default/runtime ABI change is accepted by this checkpoint.

## Other owner handoffs

These names and IDs were verified in the app. Their candidate status is from the existing workspace plan snapshot; those owners must provide and verify their own remote checkpoints.

| Chat | ID | Checkpoint need |
| --- | --- | --- |
| Review Chibi self-hosting work | 01a16428-9213-7ec1-b984-a2af4ceced41 | Preserve current plan, queue, reviewed composition and landing receipts. |
| Typed Chibi assets and KDL removal | 01a160a6-7daa-74e1-b595-8af541a773eb | Preserve typed tree6ef6b76e and full pending ancestry; native/material groups passed, projects/language/tools and integration gates incomplete in the recorded plan. |
| Extend Chibi build graph and SDK packaging | 01a165aa-e67e-75b3-8bf1-5637109599a1 | Preserve reviewed source-only full SDK pair/header draft51c42e4a and its required inputs; installed proof unrun. |
| Repair Chibi CI and dependency publication | 01a165ab-2d6d-7e51-937e-5b7466ea2c72 | Preserve exact test logs/receipts, process state and release status; no complete required-gate pass recorded. |
| Plan Odin-inspired packages | 01a15cd2-7126-7cc0-bc72-da5791aae3d5 | Preserve tested tree6353f5a3, separate docs overlayf0fe82ef and later source maps; common/query/bootstrap composition unrun. |
| Shared string slices and method-style… | 01a160a6-14c8-7393-bae2-5de4ed95921a | Preserve tree97c1656f with fixture/effect repairs, earlier f90c tests and remaining failures/unrun gates; source ABI22 differs from artifact ABI21. |
| Review system and query syntax | 01a16526-25dc-7040-9202-a0a882f7f29d | Preserve reviewed tree4c3d1d84 and separate lifetime snapshotb2c82d24; combined runtime/source eligibility unproved. |
| Chibi materials and CPU-debuggable shaders | 01a165aa-9cba-7700-b8d0-b3e84dab468b | Preserve source-only Facts1f3e1858, SourceMapbc3e72e1 and current native foundation; producer, closure/facade/material/render acceptance pending. |
| Review and merge open PRs | 01a129ce-181e-7493-b1c4-ac7eb70be726 | Preserve PR407/460/Studio77 local integration/recovery trees, ordinary merge identities and all remaining operation/platform/browser holds. |

No additional owner chats were contacted for this handoff.
