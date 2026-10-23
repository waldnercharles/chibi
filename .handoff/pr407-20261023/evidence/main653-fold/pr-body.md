## Current unpublished recovery (2026-10-22)

The local recovery candidate is staged tree `fa24367aa5284d67160118716c538207d3672cd8`, incorporating eligible main `d0de4a84ecf50badcab661411470bdf6f3da5fa2` and a new prepared producer/runtime-only consumer test. It is uncommitted and unpushed; this PR still publishes historical head `c541133b689c620a377982ee339c017b6d12b263`. Current recovery covers Owner/Self and Delay; the private system delta instruction remains, without reintroducing the removed public Delta Time capability.

Independent source review found no integration blocker. The pending merge and its resolutions are preserved. A tree-only simulation of the later ordinary main merge preserves the exact candidate tree; this is source equality, not final artifact or generated revision identity. No builds or tests have run on this combined tree. Earlier focused and broad results below apply only to their stated historical sources. Required validation and Wasm/consumer checks remain pending the shared validation slot.

There is no universal test wall-clock ceiling or timing-admission quarantine. Actual operation/resource limits, required server checks, the unresolved review thread and the denied review-state mutation remain in force. No approval or merge readiness is implied.

## Historical published core-operation contract
Add checked, closed descriptors for the existing Owner/Self, Delta Time and Delay operations. Shared typed declarations generate execution bindings; validation rejects malformed known declarations, raw/native opcode mismatches and custom callback substitution. The whole catalog is checked before semantic state rewrites and the checked scalar-write path. Known operation identity/effects/guarantees participate in capability ABI and incremental snapshot identity; unknown host operations remain conservative.

No new author syntax, registry, generic FFI guarantee or project migration. Delay intentionally does not promise no reentry or no allocation/copy/retention. Prepared instructions retain actual-opcode schema identity and manual semantic-version requirements.

## Historical adaptation checkpoint (2026-10-19)

The owner explicitly authorized adapting this PR to current main and taking it through independent review and validation to merge, clearing the earlier merge-call cancellation hold. The PR is draft while that work is incomplete. Head `c541133b689c620a377982ee339c017b6d12b263` is the historical candidate; it conflicts with current main `07eb2abaa5a097352c472746b2aadf2f97cd5e4e`. Earlier reviews and test results below do not validate the upcoming integration. The unresolved review thread and all applicable exact-head validation remain to be addressed before landing. No absent CI is treated as passing.

## Historical local native validation checkpoint (2026-10-19)

The adapted local source tree `f8ea41fafb9a428e6a134a3d41acd3e269fd5189` is based on main `07eb2abaa5a097352c472746b2aadf2f97cd5e4e`; it has not yet been published to this PR branch. Independent source/workload/configuration review approved scalar collection-identity rejection and Owner-alias invocation effects through the current compiler. Five pre-existing native-test fixture files also receive value-preserving designated initializers required by the Clang 19 warning policy.

All eight selected native production commands passed serially: language group (35 selectors, 505 cases, 30.809s), native-tools (136 cases, 25.917s), native-catalog (8 cases, 0.318s), language-prepared (22 cases, 0.298s), reference-index (6 cases), terrain-checkpoint (6 cases), terrain-seams (3 cases), and material-surface-mesh (18 cases, including terrain-volume). Debug ASan/UBSan and assertions stayed enabled; LSan is unavailable under tracing. Actual bytecode/SLJIT, generated-C and native/prepared consumers ran. The affected operation public-header consumer also compiled against a task-local installed SDK, with no whole-repository header fallback for that consumer.

Each of the two new single-guard negative controls failed at exactly its intended assertion, without a crash, sanitizer finding or timeout. Restoring the clean source reproduced the original artifact hashes and passed all 11 operation cases (237 assertions).

Wasm validation remains pending the separate Emscripten installation approval. Publication, final exact-head review, current integration/server checks and resolution of the existing review thread still precede merge. These native results are local-candidate evidence, not evidence for the historical remote head.

## Historical published candidate and holds
Head `c541133b689c620a377982ee339c017b6d12b263`, tree `7ca15cc30dc83566f84d05e645caa17534b50b86`, integrates prior published `b255df023158d6b7af3bce644d37c3ca640c29fa` with eligible main `3c65bb60b6f3443aef6ccf4ab15acbe09b04b191` (checked scalar writes). At the October 15 checkpoint, remote readback confirmed this head and clean mergeability; that mergeability is no longer current. The exact tree equals independently reviewed local `2085cccc04082cde9acc6078d329fd35c85045cc`.

**Historical hold, cleared by the owner on October 19:** the separate merge-call cancellation hold previously awaited owner clarification. Independent technical review explicitly APPROVES the exact published head above, and all five selected gates passed. The GitHub review thread is still visibly unresolved: the source finding is fixed and independently verified, but the review-state mutation was denied. The review thread remains unresolved; the separate cancellation hold was cleared by the owner on October 19.

## Review finding and reproduced repair
[GitHub finding](https://github.com/waldnercharles/chibi/pull/407#discussion_r4286144466) identified accepted noncanonical dormant component/aggregate/flow metadata. Regression-only `d581d39e3a8d393f89170e500bf5840f5640e009` independently failed exactly at the absent rejection of Owner.component=1: 8/9 cases passed, 161 assertions, exit1, 0.092s. No malformed catalog was lowered/executed and no sanitizer/crash/timeout occurred.

Fix `f17e7b1406622e8f67f6105c9854aece366cc10c` adds the twelve canonical-zero checks after the unchanged UNKNOWN early return; active component/flow ordinal rules remain unchanged. Byte-identical regression fixtures passed natively and in Wasm: 9/9 cases, 208 assertions, 0.097s and 0.200s. This includes all 36 malformed rejects and three canonical raw accepts. Full fixed bounds: 82 catalog attempts, seven source preparation/lowering attempts, four finalizations, eight dispatches, two synthetic updates with 1000-step caps; no frames/network/test children.

## Historical October 15 current-main combination
Independent source/workload review approved combined `2ae4981bdf7ad6370c904d27a76c114cf05c39d7`. Four applicable compile-only builds passed; all five complete production commands ran serially:

| Command | Selectors | Cases | Assertions | Seconds |
| --- | ---: | ---: | ---: | ---: |
| language group |31|412|39,562|21.345|
| Wasm group |19|256|35,641|3.531|
| native-tools |1|116|33,227|23.185|
| native-catalog |1|6|346|0.103|
| prepared |1|19|833|0.246|

Operation, module and scalar-write cases occur exactly once in each applicable group. All previous group members/order are preserved. Native-tools uses the compiler-enabled minimal closure; native-catalog uses the separate native/compiler closure with preparation enabled. Prepared totals include producer6/199, runtime-only consumer12/629 and parent1/5.

The final two-file documentation delta at `2085cccc` was independently approved: all 2,895 other tracked files and six artifact digests unchanged, all 48 evidence hashes verified. The 47-entry publication payload reproduces the exact tree: 37 existing eligible-main blobs and 10 new blobs. Remote commit/tree/branch identity is verified. The exact published-head checks were then independently rerun as recorded below.

Independent combined report SHA256: `4365215045ba674ec28e3d9e7bedfb04be96200c6da9e9492939048e7eb995c6`.
Final local documentation/payload review SHA256: `9526418b189879994463473d8cdaa71c69ffe94daf997b246cd062bc1255e74a`.

## Historical final published-head verification
Independent review explicitly APPROVES `c541133b689c620a377982ee339c017b6d12b263` with no blocking findings. All 2,897 published tree entries, 89 corpus files, six artifact hashes and 48 historical evidence hashes match. Four compile-only currency builds passed separately. The five unchanged complete production gates passed:

| Command | Cases | Assertions | Seconds |
| --- | ---: | ---: | ---: |
| language group | 412 | 39,562 | 23.944 |
| Wasm group | 256 | 35,641 | 3.374 |
| native-tools | 116 | 33,227 | 22.479 |
| native-catalog | 6 | 346 | 0.092 |
| prepared | 19 | 833 | 0.276 |

All nine operation cases appear exactly once per target, including all 36 malformed rejects and three canonical accepts. The module and checked-write cases remain present once each. No sanitizer finding, unexpected failure, crash or outer timeout occurred. Source and artifacts remained unchanged after testing. This is an independent technical review, not a submitted GitHub approval.

Final independent review report SHA-256: `2fb4a1aaa8e987d42a90c5c566c2609e8578f9810606e0b8e3ee9aa6cc2b4108`.

## Limits and historical evidence
Native correctness uses Clang19 Debug with ASan/UBSan and assertions; LSan is unavailable under tracing. Wasm uses Emscripten6.0.10 and Node24.19 with assertions. VM/SLJIT, VM/generated Wasm and the bounded generated-C tooling case are covered. No full engine, browser, GPU, Studio, threaded-Wasm or leak-check pass is claimed. Absent GitHub statuses are not passing CI; actual server requirements remain gates.

Earlier substantive review findings (scalar semantic flags, direct-rewrite bypass, Delay reentry metadata and raw binding fixtures) were fixed. Five isolated guard-removal controls produced predicted failures, restored source rebuilt a byte-identical passing binary, and control D detected the required boundary diagnostic while downstream rejection remained intact. Those historical controls are not reruns of this new guard. Previous exact-head matrices, failure provenance and limitations remain in [validation](https://github.com/waldnercharles/chibi/blob/c541133b689c620a377982ee339c017b6d12b263/docs/engine-operation-validation.md) and [contract](https://github.com/waldnercharles/chibi/blob/c541133b689c620a377982ee339c017b6d12b263/docs/engine-operation-contracts.md).

This is the first narrow L16 slice; generic host ownership/effects, retained arguments, per-argument aliases and typed consumer-service lowering remain separate work. No GitHub approval is invented and no explicit hold is waived.
