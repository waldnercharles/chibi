# Selected runtime attribution: implementation and validation

Status: implemented and independently validated on integrated source head
`b2c9ae48685a38b0a6b0c0d3de187a9d9b67f555`, 15 October 2026. The six fixed
cases passed in sanitizer-native, Release-native and serial Wasm. The four
complete affected-contract commands then passed on the current-main combination.
See [validation](validation.md) and its hashed logs for exact commands, counts,
build boundaries and exclusions. Final publication identity is recorded in the PR.

The source-freeze descriptions below and `workload.json` preserve the pre-run
admission record; references there to proposed or unexecuted commands describe
that historical freeze, not the later measured status. This does not complete
B04/L14, prove Studio acceptance, detect all leaks or establish a performance win.

## Provenance and scope

- Eligible implementation base: `3c65bb60b6f3443aef6ccf4ab15acbe09b04b191`.
  This includes checked scalar writes. Held operation-descriptor PR407 is absent.
- Approved design: original documentation commit
  `5411c4c9273c9960330d6be443a0faa54f2b69a1`, design SHA-256
  `31515511b688b6ea8e5c76c459f0e45c8990cabaa4ba522ae16ab425a1abf3f9`.
  The same three historical audit documents were rebased without content changes
  onto the implementation base as `b6625b98d13e1cd9ff5d193a5de13e96c5b8fcf9`.
  Their f1bfe source references describe that historical audit, not current main.
- `implementation-sources.tsv` freezes base Git blobs and candidate SHA-256 hashes
  for all changed product, build, dispatcher and supervisor files. `workload.json`
  freezes workload counts, controls, build flags, binary hashes and proposed
  production-supervisor commands. Any code/consumer/bound change requires a
  refreshed manifest and review before execution.
- No existing public struct layout, opcode, intrinsic ABI, prepared format,
  project version, authored syntax, source-profile meaning or lifetime policy
  changes. The additive C APIs are `types/copy.h`, execution scopes in
  `execution/function.h`, and `ecs_world_string_usage` in `ecs/ecs.h`.
- Original installed-SDK native and serial-web Studio compile baselines were
  subsequently built separately; their runtime/GPU and current-engine-SDK
  acceptance remain unverified. Exact Studio source remains
  `78cb4d4c47242db9f7af2b9573653d9756050e99`. This is an engine prerequisite only.

## Exact hooks and deliberately selected coverage

The H1–H6/T1–T2/N1–N3/R1–R2/B1 meanings and per-specimen formulas remain the
approved [design](design.md). Current hook locations are recorded by source
function and adjacent operation so line-number movement cannot change meaning:

| ID | Candidate operation | Fixed proof |
| --- | --- | --- |
| H1 | `types/array.c`, `pin_array_insert`, final `items[index] = copy` | S1 push: two headers, H3+H1 |
| H2 | `pin_value_snapshot_to_arena`, initial `*dst = *src` | S2/S3/S4/S5/S8/F1 |
| H3 | `pin_value_copy`, initial `*dst = *src` | S1/S2/S3/S4/S5 |
| H4 | `pin_value_copy_to_arena`, initial `*dst = *src` | S2/S3/S4 |
| H5 | `types/union.c`, `pin_union_copy`, initial `*out = *source` | S4: three headers directly, four through `pin_value_copy` |
| H6 | `intrinsics.c`, `snapshot_reference_root`, initial `*out = *source` | S6: one header |
| T1/N1 | `array.c`, `arena_string`, completed memcpy classified by existing caller | S2–S6/S8/F1; clone/owning array names 5/5/25 |
| T2 | `pin_value_copy`, successful `xstrdup`; extra strlen only with active scope | S2 null/empty/abc 0/1/4 |
| N2 | `index.c`, `empty_owned_set`, completed nominal-name memcpy | S7 N24 first insertion, zero duplicate |
| N3/R2 | `snapshot_reference_root`, name and root-record memcpy | S6 N12, R=sizeof(ProbeRecord) |
| R1 | `pin_value_snapshot_to_arena`, root-record memcpy | S5/S8/F1 exact sizeof including failure |
| B1 | `program_tasks.c`, `deliver`, borrowed result-descriptor memcpy | task Save sizeof(pin_value), eight times per enabled program |
| VM | `vm.c`, actual interpreter entry | case2 ten calls; cases4–6 actual route |
| SLJIT/fallback | `runner.c`, immediately before generated call / inspected fallback | case5 normal, inspected, COMPILING control; cases4/6 actual route |
| Wasm | `wasm_jit.c`, after installation, immediately before generated entry | case5 normal/inspected/uncompiled; existing execution statistic cross-check |

The unreachable nonzero insertion shift is neither instrumented nor exposed.
A copied root record includes its inline nested fields once; pointed-to strings
are separate real copy spans. Nominal strings count once per actual copy.
Descriptors count selected C assignments, not CPU moves or implicit ownership.
There is no unqualified all-copy or all-allocation metric.

Current-source reconciliation makes these important omissions explicit:

- `execution/function.c:copy_string`, reached by `BF_COPY_STRING`, copies text
  through a different leaf and is **outside** the approved T1/T2 inventory.
- `ecs_component_write_value`'s staging/replacement/publication row memcpy spans
  are **outside** R1/R2. Its recursive managed-field snapshots can still reach
  selected T1 leaves. Consequently a transaction's selected text bytes do not
  claim its entire copied row traffic.
- New checked-write `behavior_component_write` copies the scalar operand and
  entity into descriptors, then calls the existing authoritative publication
  callback. Those scalar memcpy operations are excluded. It creates no seventh
  promised descriptor assignment and does not move any selected copy leaf.
- Existing `BF_COPY_STRING` and ECS row-copy paths also existed at f1bfe; this
  exclusion disclosure does not add hooks or change the approved coverage.
- Other descriptor construction/assignment, raw fields, compiler strings,
  serializers, arbitrary host copies and allocator-internal movement remain
  excluded. Interned field text is separately derived from an exact same-world
  gauge delta; pointer-table rehash traffic is not text traffic.

## Ownership, threading, overhead and reset

Both scope objects are caller-owned and zero-initialized. Each subsystem has one
TLS top pointer. Begin/end preserve counts, nesting is exclusive, and inactive
reset clears counts/overflow. An active parent remains active under its child.
Same-thread ordinary C reentry is included; queued work is counted only when it
runs. Workers do not inherit scopes. Active sharing/concurrent read/reset,
cross-thread end and nonlocal exits bypassing cleanup are unsupported. Inactive
transfer needs caller synchronization. Counters retain no owner/context/module.

The private copy helper is staged only into the types/execution private include
paths; it is not an installed extension or registry. Recording performs no
allocations, clock reads, callbacks, observer changes or atomic additions. Inactive
hooks return after the dormant TLS check. The only newly required string scan,
heap-copy T2's strlen, is guarded by activity. Existing lengths are reused.
Unsigned counters saturate at UINT64_MAX with sticky overflow; the consumer's
payload/entry totals, disjoint merging and deltas use checked arithmetic. Existing
xalloc/arena/Wasm statistics retain their original meanings and are subtracted
only after monotonic endpoint checks in the finite serial fixture.

The intern query traverses existing slots/text on demand, adds no world fields or
hot-path writes and returns scalars. It is quiescent-world-only, never callable
after destruction. Payload is retained text including NUL; backing is the pointer
vector's actual capacity plus its dyna header. It is neither semantic-live text
nor peak allocation. The maximum of finite boundary samples is explicitly named
`sampled_retained_max`. Allocator overhead and all other world storage are omitted.

All scope, owner, artifact restoration, producer allocation and context storage
in the C fixture lives through pico teardown, including REQUIRE failure. Teardown
ends inner scopes first, restores the isolated native inspection state, releases
host contexts before function artifacts, destroys programs before modules/worlds,
frees arenas/producers, and clears the project type catalog last. No scope points
at a returned helper's stack. The private enum names are moved with unchanged
values 0–3 and unchanged artifact layout.

## Concrete six-case workload

One file, `test/language/runtime_attribution.c`, one existing-dispatcher selector,
`--runtime-attribution`. No new executable, variable workload knob, file/network
operation, scene/frame, sleep, browser, worker pool or Studio launch.

1. Copy leaves: S1–S8 plus F1, once OFF and once ON. Exactly 22 observed API
   operations per mode, 44 total, 46 selected descriptor assignments in the ON
   pass. Every operation has its own scope and category assertions. Producer
   mutation/free and arena/world teardown establish the specified bounded
   ownership examples. S6 builds two worlds total, one per mode.
2. Scope semantics: exactly ten copy operations and ten BYTECODE entries in the
   approved nesting/reentry/reopen/reset/saturation schedules. Scalar builder
   <=8 instructions, budget32. Pure reporting-arithmetic checks add no entries.
3. Intern storage: three world constructions, at most two alive. A has `base`
   and 32 unique `edit00`–`edit31` values, 32 repeats and restoration. B is
   untouched after `other`; C starts fresh after A's destruction. A's final
   33 entries/229 bytes, B's 1/6 and table capacities 16/32/64 are exact. No old
   A pointer is dereferenced after free.
4. Tasks: literal source <=1024 bytes, two prepared modules, four programs/worlds,
   one owner per program, eight cycles each. Start, completion update, Save and
   cleanup update give exactly128 boundaries, budget1000. Producer input has
   63 data bytes plus NUL and is destroyed before completion. One retained task
   and result at most per program. Backend scopes are ON in both copy modes.
   Every enabled program asserts B1=8*sizeof(pin_value), surviving output and
   zero final task-result backing; route counters reject accidental fallback.
5. Backend: two scalar builder functions <=8 instructions (below approved32),
   budget32 (below1000). A makes VM, compiled, inspected compiled, scopes-off
   compiled calls. Native B makes isolated COMPILING fallback then real inspected
   native; Wasm B is uncompiled and rejects without a generated entry. Exactly
   six native or five Wasm attempts. An inspect callback requests the inspected
   route but no scope enables inspection implicitly.
6. Paired bounded samples: two builder functions <=32 instructions, no outlined
   calls. Scalar 41+1 and the real snapshot intrinsic on three strings
   `one`,`two`,`three` (14 copied text bytes) are each run on VM and compiled,
   OFF/ON. Eight route-counted warmups plus five paired128-call trials give
   exactly5128 calls. Pair order OFF/ON, ON/OFF, OFF/ON, ON/OFF, OFF/ON. Managed
   ON calls assert H4/T14 each; scalar calls assert no selected copies. Each
   trial has128 separately prepared contexts and one fresh output arena, host
   setup outside clocks, storage <=512KiB used including padding, semantic
   result/checksum parity and exactly128 actual entries when enabled.

The managed trial currently requests64KiB arena backing before timing; the
bounded retained output is read and released after each trial. Timed work is
only invocation/status storage/checksum accumulation. Logging, scope begin/end,
compiler work, result verification, allocator/arena/gauge samples and cleanup
are outside clocks. Reports label module/generation ordinal/owner/phase for
retained module windows, and n/a for builders. Mixed task windows are labeled
program-owner/eight-cycles totals, not per-function costs. Engine base and exact
fixture SHA are embedded at configure time; candidate source/binary manifests
remain the authoritative exact-build provenance.

## Builds, controls and proposed validation

Three fresh candidate build directories use only existing local toolchain and
dependency sources; build concurrency is two. Compilation/configuration are
separate from all runtime measurements.

The recovered candidate was reconfigured and rebuilt in all three directories
without source changes. `workload.json` records the exact configure/build argv,
clean dependency revisions, compiler/Node identities and build-log hashes. The
artifact hashes identify compiled output only; none of these builds has run a
runtime selector. Standalone `test/language` configuration avoids the unrelated
desktop product setup that the root `make build` target would configure.

- `build-attribution-native`: Clang19 Release, runtime `-O3 -DNDEBUG`, no
  sanitizers; test C has `-UNDEBUG`. This is the paired raw-measurement build,
  **not** assertion-enabled runtime or sanitizer correctness coverage.
- `build-attribution-sanitize`: Clang19 Debug, no runtime NDEBUG, ASan+UBSan,
  frame pointers and test assertions. Proposed correctness environment:
  `ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1`. LSan is excluded
  as in the existing workspace validation contract; no leak-detector claim.
- `build-attribution-wasm`: serial Emscripten Release, no pthread option,
  runtime NDEBUG and test `-UNDEBUG`, run through Node.
  Emsdk 6.0.10 supplies Clang24 and Node24.19.0. The proposed command explicitly
  selects that Node through PATH; the CMake emulator setting alone does not
  select the Node used by the production supervisor. This is generated-Wasm
  correctness/parity, not browser/threaded coverage.

The OFF sample is uninstrumented by active scopes but retains dormant compiled
hooks. Per-call OFF backend counters are unobserved: each identical immutable
route is proven at its counted warmup, with no intervening inspection/publication
change. This paired control does not quantify the cost of having hook code at
all. The separately built no-op-hook control remains optional, **not implemented
or admitted**, and no overhead percentage is claimed. Sanitized timings are
correctness-only observations, never performance evidence.

Proposed new invocations through the production supervisor:

- `CHIBI_LANGUAGE_BUILD_DIR=build-attribution-sanitize ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tools/test_policy.py tool language-runtime-attribution`
- `CHIBI_LANGUAGE_BUILD_DIR=build-attribution-native python3 tools/test_policy.py tool language-runtime-attribution`
- `PATH="../chibi-tools/emsdk/node/24.19.0_64bit/bin:$PATH" CHIBI_WASM_BUILD_DIR=build-attribution-wasm python3 tools/test_policy.py tool wasm-runtime-attribution`

The candidate also adds the selector to existing language/Wasm aggregate
registrations so normal dispatcher coverage does not silently omit it. Select
the existing affected-contract checks on the candidate binaries, including applicable
copy/type, program/task, execution/inspection, activation/borrow and checked-write
contracts. Review the source, workload and oracle, then run the selected checks. Unavailable required checks remain blockers.
