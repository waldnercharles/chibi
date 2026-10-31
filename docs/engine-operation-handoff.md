# PR407 history-preserving integration disposition

Recorded 2026-10-25 (fb4e2c606). Several paths below, including
`tools/test_policy.py` and the CMake files, have since been deleted or archived.

This records the ordinary two-parent integration plan and its explicit source
resolutions. It does not authorize updating the existing PR branch, resolving
its denied review thread, or merging into main.

## Source and ancestry

- Original PR407 head: `c541133b689c620a377982ee339c017b6d12b263`, tree
  `7ca15cc30dc83566f84d05e645caa17534b50b86`.
- Original branch: `codex/verified-core-operation-contracts`.
- Original/recovery common ancestor: `3c65bb60b6f3443aef6ccf4ab15acbe09b04b191`.
- Reviewed recovery predecessor: `595ca6e03439958e178cc33f82ec47d7c5596570`,
  tree `5b349a6feb1befdaa41e9fc92496c95aa7eec0d3`, descended from current-main
  baseline `d1154cdb8677cbfe479d4be40db7ca480d10f103`.
- Recovery source provenance remains the published `f61db252418f0d2d01dc8f43a56f5ff97e9a5351`
  snapshot. Its copied source does not imply ancestry; see
  [validation](engine-operation-validation.md).

The old branch has three commits absent from recovery: `b4d4c1f9`, `b255df02`,
and `c541133b`. The integration uses the original head as first parent and the
strengthened recovery as second parent, retaining both complete histories. The
result must equal the strengthened recovery tree after the per-path resolutions
below. No blanket `ours` strategy, force push, or replacement PR is required.
The actual parent SHAs and tree are recorded by the integration commit.

## Strengthening preserved before integration

The first recovery omitted ten lines from
`program_task_state_awaits_its_function_results`. Those assertions were not
obsolete: generic compile failure alone could hide failure at the wrong phase.
They are restored without changing spelling or semantics. Each rejected write
requires a native catalog, successful parsing, an invalid semantic analysis,
and `Incompatible types: expected double` in the diagnostics. The original
fixtures and their literal expected values are unchanged.

The owning `language-program` selector passed 165 cases / 12,391 assertions,
including this case; the small `language-operations` regression passed 11 cases /
266 assertions. AppleClang 21 Release used the existing cached dispatcher,
two build jobs, and unchanged test-policy deadlines. No production code changed
for this strengthening.

## Explicit merge resolutions

An ordinary merge simulation of the original head and the reviewed predecessor
reported 14 conflict paths and four additional cleanly merged differences.
The restored program assertions now merge without discarding that protection.
Each path has an individual disposition; choosing the current implementation
does not waive the original behavior or tests described here.

| Path | Disposition |
| --- | --- |
| `docs/engine-operation-contracts.md` | Keep current Owner/Delay contract, invocation effects, migration-only Self adapter and private system delta distinction. Public Delta was removed on main in `a0b73aba`; do not restore it. |
| `docs/engine-operation-validation.md` | Keep current provenance, exact-source tests and explicit platform limits. Original receipts remain reachable in the first-parent history. |
| `docs/evidence/engine-operations/log-sha256.txt` | Exclude the 55-line historical receipt list from the result tree. Preserve it in original history; its hashes do not validate recovery. |
| `docs/testing.md` | Keep current main policy. Dedicated validation notes replace the old nine-case, 82-catalog Delta workload description. |
| `sdk/CMakeLists.txt` | Keep current catalog snapshot/value sources together with the recovered operation source/header and capabilities dependency. |
| `sdk/README.md` | Keep current README. Dedicated contract documentation covers the former mapping prose and cross-link; do not reintroduce obsolete Delta wording. This intentionally changes the documentation's location. |
| `src/capabilities/catalog.h` | Keep current type/default APIs and checked Owner/Delay rows, including Owner invocation-read effects. Preserve conservative UNKNOWN identity. |
| `src/capabilities/operation.c` | Retain all dormant-field checks, add collection-identity rejection, and validate Owner after main's public Delta removal. |
| `src/language/lower.c` | Preserve whole-catalog and custom-binding checks, current map support, and main's allowed UNKNOWN `BF_ASSERT` binding. |
| `src/runtime/core_definition.c` | Keep generated Owner/Delay bindings, current assert binding and invocation effects. Do not restore public Delta. |
| `src/runtime/native_capabilities.c` | Preserve raw declaration/binding validation and the existing UNKNOWN assert exception. |
| `test/authoring/prototype_documents.c` | Keep current main's designated initializers, explicit null members and later arena/text API adaptations. The old initializer fixes survive. |
| `test/language/CMakeLists.txt` | Preserve all current test sources/private include exceptions and include operations exactly once. |
| `test/language/main.c` | Preserve current dispatcher breadth and operations exactly once. |
| `test/language/operations.c` | Keep current syntax, OwnerAlias effects, fixed UNKNOWN ABI vectors, collection guards and actual backend counters. Replace obsolete public Delta reads with fixed owner-7 checks while preserving delay/string lifetime assertions. |
| `test/language/program.c` | Preserve every original Delay contract/binding hunk, current lowerer-report fixture bindings, all newer main tests, and the restored ten phase/diagnostic assertion lines. |
| `test/tools/native_catalog.c` | Preserve identity/duplicate validation using Owner's handle, nominal identity and invocation-read effects instead of removed public Delta. |
| `tools/test_policy.py` | Preserve all current groups and operation selectors once; remove only the duplicated auto-merged operation key and stale workload counts. |

The remaining nine paths in the original PR delta already matched recovery
after the ordinary merge: `sdk/contracts.cmake`,
`src/authoring/source_catalog_snapshot.c`, `src/capabilities/catalog.c`,
`src/capabilities/definition.c`, `src/capabilities/definition.h`,
`src/execution/operation.h`, `src/runtime/native_definitions.c`,
`src/runtime/native_definitions.h`, and `test/authoring/ui_pages.c`. Their SDK
dependencies, operation identity/ABI checks, generated bindings and initializer
fixes are retained without additional resolution.

Recovery adds the narrow migration adapter in `src/language/semantic_calls.c`,
format/migration checks in `test/language/format.c`, prepared producer and
runtime-only consumer checks in `test/language/prepared.c` and
`test/tools/prepared_runtime.c`, and the verified Graph inventory changes in
`tools/graph/engine/Targets.chibi`. These additions survive integration.
Strict M2 numeric rules, compiler/bootstrap/runtime pins and unrelated current
main work remain intact.

## Finalization limits

Tree equality is source evidence, not proof that generated revision identities,
packages or binaries are equal. Run the existing CI and compiler self-build
workflows against the final integration head, preserving platform skips and
actual native-catalog execution evidence. Existing PR-branch updates, PR state
changes, and merge into main require separate authorization. The denial for
`PRRT_kwDOT_IpgM6s41Mg` / `discussion_r4286144466` remains in force.
