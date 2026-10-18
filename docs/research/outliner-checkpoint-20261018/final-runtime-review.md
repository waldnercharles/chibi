# Final shared runtime source review — 2026-10-18

Review scope: the current engine worktree `/workspace/chibi-outliner-m3`, based on HEAD `ff6d06c888fe660894776255ec7f9fc507db6393`, with the staged M3 integration and shared authority, scalar append, request-fuel and source-publication changes. This is an exact-source integration review, not a landing review or a declaration that the final validation matrix is complete. No build, test, authored execution, preparation, source mutation or network write was performed in this review. Only this outside evidence note was written.

Reviewer `/root/outliner_runtime_authority` authored the earlier authority/scalar implementation; this final check must not be described as independent review of that authorship. Independent implementation reviews were previously supplied by `/root/outliner_contract` and `/root/outliner_m3_integration/audit_auto_merges`. The latter also independently reread the current combined lifetime/escape/checkpoint boundaries for this review and reported no concrete blocker, verifying the same final hashes below. Fuel and source-guard implementation were authored by other agents.

## Result and formatting verification

**No concrete blocking correctness issue found in the reviewed combined runtime source.** All fourteen shared fuel files match `request-fuel-draft/ENGINE.sha256`; all scalar manifest entries match after intentionally taking the fuel manifest for its overlapping files. The format-only changes do not modify those files. The lowerer, final-ABI census fixture and both authored census inputs retain their pinned identities.

For each of the eight files below, the saved backup and current file match the before/after manifest. I read every unified diff, not just the whitespace-stripped equality result. Changes are indentation or line wrapping; identifiers, operators, literal contents, comments, preprocessing directives and macro argument token order are unchanged. No token semantics changed. The independent whitespace-stripped check also passed for all eight. `format-engine-1/manifest.json` and the original backups remain untouched.

| Formatted file | Before SHA-256 | Current SHA-256 |
| --- | --- | --- |
| `src/authoring/application_catalog.c` | `96ad5faf86b889d92a25478f6ba55d8a877fb6d4cd8d018146544d6386a36c7d` | `d792c2f4353a70e54e15a8772d8aca6574160a337729791ec533fb5cd65f9b95` |
| `src/authoring/studio_catalog.c` | `0c0191239a6c32e55ab3ad85f8595c66b7e2f1a3681d9e3e6903a4e0d9b200cb` | `12ca98e594fd11da272ba4dfb45eefd5fc041bf999f3875af2b92548abab3fb7` |
| `src/authoring/studio_catalog.h` | `e0bcc7863efe5c340aac75f17fcabb70a1033b33b9579245888a541c679d3588` | `7c00004dbe702ae16699e850e05d79d9b223c74e4933fde3fd3db180d9366383` |
| `src/render/ui.c` | `a1ce7b78237ae5de9a7be6d6a94468a9a3cfe73ab06efc60ec1eec847200d5c5` | `076441853f75555db70daed82ebe47bd9447c91de6b2d725476d2a355baed4f0` |
| `src/runtime/behaviors/source_guard.h` | `829919c03989d4dddd948128f1b44c198311ad9d6aaa3d80eb118c3193ffa1f9` | `7b9e68fc08095184bbd1ddebd31f4fc15721747c129c29d814b1a212a8a778c8` |
| `src/runtime/behaviors/source_publication.c` | `8d4f07882b8477114c8a16361f290acd5aca211a83979c0c971e7d869f1addf4` | `1a3008213fbb229fb1f386937712d51b2f45422376c76d03b57382fb00daa675` |
| `src/runtime/text_format.c` | `503490e162b8250f72d3a2c048159055c93fdf5e7d51da8e0231960a4276011f` | `bd2f98d0fe409d15d195a211694d4c90ba84c1792ec4d764b0135bef30ea86a9` |
| `test/language/text_format.c` | `8ede53ef019e2a636e9f50c36beb0a78c1aed1cb12fd954e79b074d1a66cc77e` | `7366b36efb34e7451e87799951553638be6e4a6c6d907ce1caa093eecbf50cbb` |

## Reviewed integration invariants

- **Origin and account ownership.** Queued events retain their exact dispatch object, source fan-out retains across the complete callback loop, and each activation/task/continuation independently retains the object. A detached host request retains token lifetime without falsely counting as pending execution. Synchronous frames borrow the account under a retained activation, refresh it from the root on reentry, and clear it when retired. Root cleanup resets/releases retained call frames before dropping the dispatch, and clears cached root `work`. Cancellation during native begin, ready-request delivery or first-run child execution marks stopping and defers destruction until the active boundary returns. Account and cancellation identity do not depend on recycled activation addresses.
- **Authority boundaries.** Only the first uninterrupted explicitly tagged handler slice grants proof. Runtime-owned context traversal admits synchronous helpers and rejects fabricated contexts. Nested dispatch/emit, including unknown-event/empty-registry source calls, revokes the outer slice permanently. Pause, failure, cancellation, source/service rebind, replacement and detach revoke it. Tasks, action continuations and resumed slices inherit cancellation/account ownership without gaining authority. `execution_context_state` reads the original active instance's live state, including uncommitted scalar assignments, under the same proof; it does not use an imported helper's value owner.
- **Queue and failure ordering.** Owned payload snapshots and retained tokens survive the detached dispatch batch. On failure, remaining old events precede callback-enqueued later events. Cancellation removes only entries carrying that exact cancelled token; unrelated and untagged payloads retain order. Whole-source fan-out remains valid if a synchronous callback releases the original caller reference.
- **Finite request work.** An opt-in token has one nonzero finite balance, with no refill on enqueue, pump, helper entry, resumption, task or continuation. One absolute grant requires the current original proof and exact token, cannot lower the limit, and preserves spent work. Each successful canonical BF_CHECK charges once, after the legacy frame-budget check succeeds. `spent < limit` precedes increment, so the counter cannot wrap at UINTPTR_MAX. Ordinary contexts remain on the old path. Shared-account execution disables prepaid fast/loop shortcuts while preserving the scalar runtime body and its checkpoints. A direct prepaid root lacking canonical checks fails with the explicit unsupported-policy state error. VM, generated C, SLJIT and Wasm paths share these semantics; SLJIT's float-only layout uses nonclobbering scratch and distinct failure materialization. The prepared ABI fingerprints the account layout and context pointer, requiring actual consumer/image rebuilds.
- **Scalar alias and escape preservation.** Derivation remains restricted to canonical unnamed I32 arrays and exact add/snapshot/reader callbacks. Only canonical Get's scalar index accepts the exact built-in `int` decoration. Pairing rejects alternate CFG entry into add/retain/commit; explicitly elided observers can be skipped only on the uninspected runtime path. Whole-body private-array/backing provenance rejects raw writes, native escapes and unsupported effects. Outlined arguments receive either an owned snapshot or an independently proven readonly view; the optimization does not claim every pure host call is readonly. Runtime callback and relevant array-bank checks retain canonical fallback. Latest-header/backing/count/capacity/arena matching permits only suffix writes; older prefixes fork. Geometric growth and checked arithmetic preserve immutable logical headers, escaped snapshots and the existing nominal-record optimization.
- **Cache/collection boundaries.** Private tail caches reset on fresh invocation, rebind, inspection/deoptimization and arena-owner changes, while normal BF_INVOKE parent reentries preserve them. Unrelated stale native result metadata is not dereferenced by the I32 eligibility guard after an arena reset. Extra roots participate in both tracing and visiting, and storage accounting/cleanup includes their private buffers. Scalar lowering changes only derived host-call flags; runtime compaction retains BF_CHECK and prepared images cannot supply those private runtime flags as trusted metadata.
- **Publication guard.** Register, replace and remove all pass through source_stage/source_publish. The guard validates the complete final registration set before any replacement commits, using borrowed immutable views. Rejection frees staging and leaves the current programs unchanged. Commit applies the prepared generation before reporting actual applied module pointers, and discard does not notify. Guard installation validates the existing set first; exact reinstall is a no-op and replacement by a different guard is rejected. Callback reentry/mutation is forbidden by the documented host contract; staging/executing assertions reinforce the boundary. This mechanism does not treat arbitrary native callbacks as hostile sandboxed code.

The independently reviewed prior fixes remain present: unconditional nested source-boundary revocation, whole-fanout retention, active cancellation deferral, zero-argument command-bank pointer safety, observer-aware append pairing, correct canonical reader identity/name handling, fresh-arena eligibility, and exact legacy/shared checkpoint debit order. Earlier failing test logs remain preserved; assertions and limits were not weakened to erase them.

## Recorded execution evidence, not new runs

I read these existing logs. Each standalone command retained its shared hard 60-second ceiling. Build/configuration time is outside those recorded test times. These are checkpoint results from their recorded candidate/configuration, not proof that every required final clean-candidate build or group has run. Older native authority/source/text results must not be relabeled as a fresh run of the final formatted tree solely because the tested behavior is unchanged.

| Existing log | Passed cases / assertions | Recorded wall time | Meaning |
| --- | ---: | ---: | --- |
| `test-authority-2.log` | 9 / 904 | 0.194s | Original authority lifecycle/fan-out regressions; native bytecode and actual compiled coverage. |
| `test-source-guard-2.log` | 3 / 62 | 0.144s | Six tiny bytecode module preparations, at most two registrations/entries, no authored execution. |
| `test-text-format-1.log` | 4 / 80 | 0.108s | Includes complete entity-handle decimal preservation. |
| `test-scalar-append-5.log` | 6 / 1717 | 0.189s | Native exact-plan, alias, ownership, cache, fallback and copy assertions. |
| `test-wasm-scalar-append-1.log` | 6 / 1716 | 0.256s | Same specimen, actual serial emitted Wasm; the native-only assertion explains the one-count difference. |
| `test-request-work-3.log` | 5 / 815 | 0.216s | Final host-reference/update-epoch fixture corrections; actual native backends. |
| `test-wasm-request-work-1.log` | 5 / 815 | 0.216s | Same request-work specimen through actual serial emitted Wasm. |
| `test-request-work-c-1.log` | 1 / 107 | 0.313s | Real compile/load of generated C plus VM parity. |
| `test-outliner-work-3.log` | 1 / 393 | 0.475s | Final fuel-ABI preparation-only census: K=249, 14 reachable bodies, Project two append plans and IntRange one. |
| `test-studio-live-fuel-2.log` | 1 / 10184 | 7.138s | Installed consumer evidence: 4096 actors/32 rows, actual bytecode and SLJIT, 8587 entries each; exact before-Project and after-Publish budget failures. |

The last consumer run follows the owner's independently reviewed fixture-only transport-oracle correction. Its earlier failing `test-studio-live-fuel-1.log` remains valid historical evidence, superseded by this rerun, not removed. The successful consumer log prints `shared_spent=not_observed`; do not claim that it directly measured the balance. Runtime fixtures establish balance accounting separately. The owner reports SDK10 identity prefix `778b78b7` and a successful native25 format rebuild; those are owner-reported provenance, not a new package audit performed by this reviewer.

Exact finite scalar workload remains six cases, eight module preparations, 24 top-level calls, 402 synchronous helper invocations, 818 executed appends, 12 FormatText/TextByteLength pairs, 10 length calls, eight Get calls, 14 static reproofs (none executed as malformed IR), 10 direct reader calls, seven direct appends plus one overflow rejection and two managed collection passes; max loop input128 and 20000 per authored frame. Exact plan assertions and linear packed-copy bounds remain in place.

Exact request-work workload remains five cases, 12 modules (10 behavior/two imports), six tiny IR definitions, 40 tokens with at most four live, one held host request, seven observations, 10 queued emits, 20 dispatch APIs, **12 pumps plus two updates**, 14 direct tiny calls plus two outlined children, 14 synchronous helper invocations including those children, two tasks, two continuations and two host completions; loop32, depth3, grants at most1000. The two updates replace two pumps to advance the already-started yielded Child; that source has no system/update handler. Generated-C coverage uses two tiny IR definitions, one compiler child capped at10 seconds and 14 C/VM calls under its unchanged overall60 seconds.

## Explicit limits and remaining validation

1. This review supplies no landing, merge, deployment or complete-suite approval. Final candidate identity mapping, required groups, installed-package/consumer checks, worker profile and UI/startup/browser evidence remain owned by the parent and the current final validation plan. Serial Wasm success does not establish pthread or browser coverage. No source-independent proof of sanitizer instrumentation is inferred from a Debug label.
2. Token/account use is serialized with the programs, as authority.h states. Public account layout exists for generated C; trusted hosts must use the authority API rather than write it. Neither this API nor the source guard promises safety against malicious native mutation, misuse of borrowed views, freeing a running program, or concurrent mutation of one token.
3. Request work counts canonical checkpoints, not native operation cost, total allocation or wall-clock time. The scalar optimization improves ordinary I32 append without providing a general growable-array language interface. No scene-size cap, arbitrary production budget increase, asynchronous continuation of authority, or broader language syntax was introduced.
4. Positive scalar prepared-image encode/decode has not been directly exercised by this specimen. The canonical-image validation/rederivation path is statically reviewed and the common prepared/rebinding trust boundary has existing tests; that is not a claimed I32 round-trip pass. Actual paused scalar resume and resume after switching to a different live arena are likewise statically reviewed, with separate common lifecycle coverage, rather than directly exercised by these six scalar cases. Existing arena binding does not migrate live values: their old arena must remain alive. Fresh same-address arena reset, helper reentry, inspection fallback and extra-root GC are directly covered.
5. Request-work tests do not separately exercise a grant at the last paid checkpoint or every nested invalidation/grant combination. Exact proof validation and debit ordering were reviewed; authority's nested revocation and work's grant tests provide separate evidence. No broad new test expansion is admitted by this note.
6. Publication acceptance commits a model/source generation; a later handler failure does not roll it back. The consumer's before-Project/after-Publish cases deliberately distinguish retention of the previous valid model from retention of the newly accepted valid model. The preparation-only K249 census is qualified only for the pinned authored/lowering/ABI inputs, not arbitrary edited policy completion or native runtime cost.

## Frozen reviewed source identities

SHA-256 below identifies the reviewed content, independent of the dirty/staged worktree's HEAD. For overlapping scalar/fuel files, the integrated fuel hash intentionally supersedes the earlier scalar-only manifest entry.

```text
d792c2f4353a70e54e15a8772d8aca6574160a337729791ec533fb5cd65f9b95  src/authoring/application_catalog.c
12ca98e594fd11da272ba4dfb45eefd5fc041bf999f3875af2b92548abab3fb7  src/authoring/studio_catalog.c
7c00004dbe702ae16699e850e05d79d9b223c74e4933fde3fd3db180d9366383  src/authoring/studio_catalog.h
48f92bc1267d27134bf3de6a9983e85dff33f556c15954fe6d0bc4de447331bf  src/execution/authority.c
852a77301c443793d03ab873fcc9b44704e88347f994b9894319b4d40de0d07b  src/execution/authority.h
554f74c94f2b13da6701735898f40aa96cfcd0f1ce2593073411c729d3e80075  src/execution/authority_internal.h
9f7e256a8a405656261ef3954c0d344518eedb22bef97070b290a3d8d8958e53  src/execution/c.c
bf17a0a66f520487dd05cb37f60c75fad23ca30b4a849a878281908b72e60d2d  src/execution/call.c
6b5989ea1776ed5f2e0b2743c9950198fc192aca37e28ed2b0ecb10263132f45  src/execution/definition.h
332a81e200171775ebd4f3fc5e6b63f054327ec920cf69dc8bdbd957ff8592ca  src/execution/function.c
fe1e9b69f923f7169f7731e78adecb7325d03b1a98dc12a60ce58b84bc50e09c  src/execution/function.h
de23afc818023ea5eca39e63063729e743593fde9047f90542cd6959b8cf466d  src/execution/host.c
3506eec419a023b14f1b93023f657f579442bd420ee84fb9545679edda2f724c  src/execution/instruction.h
7de3ab6e36a17c5efb8eabc5ac0e7056bf6a6013d2c22b6b89eb8792e8a73c7d  src/execution/jit.c
05b64f78fd0075ac3cbf77fc8bddb34be129ce81abf6fbfd3308b603c15e1d41  src/execution/jit_host_call.c
24dacc38edc7d12ff36a25c0dff84acd10d8699e1b4ecfdb608eacc030536e97  src/execution/machine.c
60ba8e943d7bceecef0ba83e0a91ab97e1d156c12e049f7e1a4208e2946bab92  src/execution/opt.c
cfec73fa439511c5d7fa368ad646e1b35cd0d01ed5ed74c56c57fc85924512a8  src/execution/prepared.c
c3e03c715e2ef8cc56d784a4c5e6c2e296b051f5776cd095a6f309e93281e830  src/execution/program.c
f2c41073b241c9b5f86c03623abf3478c041307e9533302798b97a5ffdf13e1e  src/execution/program_activation.c
64d5436cfca581dd47a0c27239010ad613ae4256697d0954cbd719ae0dc00d2a  src/execution/program_commands.c
04c0f6788efed7978a9eee21c281336d473913eba3090f26458c8ae110f1525b  src/execution/program_inspection.c
8b569f1522394b43623344333c258e3681dd14139c9b067499d4eff4caf69368  src/execution/program_internal.h
ae19d7099fdd3c30f43813eaf3a42a47fbaecdf04173dc80f6347233fe960bf6  src/execution/program_observers.c
23c89c2a8abf64f225199494c3ee172301850000c585d31f5cba737520d5dc4f  src/execution/program_tasks.c
ad87bab67d54e7d3c9e3561d171549025aecbb74b56c3a42a31fbe41673d8022  src/execution/request.c
e5f379006f46b3df4d15f97e4aa5396b73dcc65926878b803a4fcdfb605eb520  src/execution/row_builder.c
8aa7924349da7ae358b558478dc5040f529c2b004140e8ae4142794fd12468e6  src/execution/row_builder.h
e89a3f3266e500ebfaff95221f4dc4e90703454d95119b98a0ded3af5f9d24d8  src/execution/runner.c
57b25599ae4642d058d330c3d2514d3e0b666abf7d108c3eb2c73c5ccd2e90ad  src/execution/storage.c
c828f904b636c111e66f81c463de5ed4f03720b982fe54d4b53b3053277ac836  src/execution/suspend.c
00da830f1e057a1c597896c4071462d015d9e4681e55a5d6dc3a17c0fc9827b7  src/execution/wasm_emit.c
f373d2063f857fb9034acdeffa59c0360a550ec8215f6c191df654df073c8c48  src/execution/wasm_jit.c
a3e1508d62da99923c83cc0682a8779b654d2ffb63fa4035ede5349762de8371  src/execution/work_internal.h
2658fc232b983e2d91a983998e3ad5e306963a428f9687e11c23ca83d778cc97  src/language/lower.c
076441853f75555db70daed82ebe47bd9447c91de6b2d725476d2a355baed4f0  src/render/ui.c
74f6994f02044df509c196ce1455db5e8fb866863c5eb9cd9429d40671ff50b6  src/render/ui.h
3ded0918dc86c5aacdd85b3fad23fdc02bc77827ee33d81e57ff0d81ed066b74  src/runtime/arrays.c
8258abf432650976f212242c737b348c5e5924d5516a17d3eb4c47764f0be2af  src/runtime/arrays_definition.h
7f8a9be2f493f9f758a29b98d2d0ba8ee8d62bae32098598115b665aa263afe0  src/runtime/behaviors/source.c
345a6fa840449c0c8e4406821537fe7ab93f777007722ac247a23bb6c4c63c54  src/runtime/behaviors/source_authority.h
7b9e68fc08095184bbd1ddebd31f4fc15721747c129c29d814b1a212a8a778c8  src/runtime/behaviors/source_guard.h
ea9f40b1822a8109074d75c6e8b9e20596d3da1c1cd6f6bbb1739962646e976a  src/runtime/behaviors/source_internal.h
1a3008213fbb229fb1f386937712d51b2f45422376c76d03b57382fb00daa675  src/runtime/behaviors/source_publication.c
bd2f98d0fe409d15d195a211694d4c90ba84c1792ec4d764b0135bef30ea86a9  src/runtime/text_format.c
434e56b12ab3fd24f76dbd3067cb8b4ef3648f9ce35a8153227c7c362c5894ef  src/types/array.c
b3e0303358417e280bef2f0a8777e463f69853aab014905098649c3f03b96972  src/types/array_internal.h
e7e08b4fbc21bb252f5cdaf176fc8987afcd4c4ab6e0f6008a28c7b4a01ce6eb  test/language/authority.c
9c2af9b1ab45cf5aae5a8d9cda33fbf6856fdee3acff9164267c5d7409aa03b2  test/language/outliner_work.c
13e3a724b3cf310255d9a685368bd542e8c96a363e8b2536ac6d2d441f341e0a  test/language/scalar_append.c
08f729686d95f002a13389be15225871aba5888439073cf0cf17ef2e643943b7  test/language/source_guard.c
7366b36efb34e7451e87799951553638be6e4a6c6d907ce1caa093eecbf50cbb  test/language/text_format.c
b961d70ef07ffa8973b2ef48739e85af2c7ab025a2158ccabf1c5874f3010748  test/tools/request_work_c.c
```

Census input hashes (Studio):

```text
510f2155a49ff77724efd38be5d1098f5ac5957c62acb0c06eb755de22e8a450  projects/studio/content/behaviors/OutlinerRows.chibi
e2c727396b2a5004327fcf17cd3e1a8194629c928db75c4927ef6f4cae9c28a5  projects/studio/content/libraries/OutlinerRowPolicies.chibi
```

Evidence file hashes (relative to `/workspace/outliner-build`):

```text
b4e7840f06a6db65743a310b995a2ae3992e921a3b394aa7c8f06655cbcbf35e  format-engine-1/manifest.json
95af69dc75b23505bae0ebec0d31c3860f47aa8e1c6a8f269c32bf5be7b3a8b1  request-fuel-draft/ENGINE.sha256
8a53861c9199fcff49f2cac47549df0ce2342d899e89fbfd24aaca42a186f0ed  scalar-append/final.sha256
84ed5efb881fa4726c6e2019a93796391fa1ca4388ab60fc5c625475bf9974d0  test-authority-2.log
33393e30d8423c7d0395975d41412a7c55abcdc8b13492e1381326d8d9dcb976  test-source-guard-2.log
84d92424520b573d6558a3dc4cfcc6ef3c9d7a3f6c5a436168f7308c9439a1ed  test-text-format-1.log
d82d1248a16805506f777e4eb800d1fcf31afc228c8d7a5d2fbc5f837c13d3fb  test-scalar-append-5.log
429ed76d720b531b6d1bedb7e0541f91d891c1ea93d0c2ccd8d18f73d517b089  test-wasm-scalar-append-1.log
ce516c3cc65621ea24958edd1cafd32a4796844f3cedb1941deb009f0c42bb29  test-request-work-3.log
c733aa6fa4cfa663098a5e908ad8a814ba51959fa22bc1f3ae90f21c275a062f  test-wasm-request-work-1.log
57abaa8ab2af1fe3f119bd741c71443cc738ccf35adca0b7433ab9eb318c6b96  test-request-work-c-1.log
530ceb49ea65144694a986b57b26829d430725c24dab3f7663617388f80ecc70  test-outliner-work-3.log
ebe25c31d45445c115b3ff578b26fb23cb8ec69b3ac974e1454b5deef57ce2e5  test-studio-live-fuel-2.log
```
