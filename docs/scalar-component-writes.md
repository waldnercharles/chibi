# Checked scalar component writes

Explicit, immediately consumed component setters can lower to the outputless
`BF_COMPONENT_WRITE` instruction. No authoring syntax or allocation API changes.
This is a reconstruction on the reviewed generic implementation, with fresh
baseline evidence; it does not recover unpublished historical test results.

## Eligibility and evaluation

The existing component-access capability declaration is the semantic contract.
Lowering does not recognize native function names or infer arbitrary callback
bodies. A selected field must be a non-nominal, single bool or numeric scalar,
with no reference/container flags. Its name, storage type, offset and width must
match the reflected root, whose size and layout hash also match the catalog.
The result must be that scalar. Root-returning setters and action-instance
completion stay on the generic path, as do managed, compound, nested, escaping,
unproven and ordinary member writes. Ordinary debug snapshots are unchanged.

Evaluation stays owner first, then RHS and its existing conversion, each once.
Only afterward does the checked instruction reacquire the current world, validate
the full entity handle and component membership, and compare layout and stride.
Verified field offsets and widths fit the row. No row pointer is cached or passed
to the publication callback, so RHS storage changes cannot leave a stale write
destination.

## Publication and results

The operation calls `context.store_component` exactly once with the declared
scalar type and original converted RHS. It never prewrites bytes. The normal
program callback delegates to `ecs_component_write_value`, preserving reflected
type/range validation, managed siblings and their ownership, same-value version
increments, dirty marks and deferred/coalesced `ECS_ON_SET` notifications. It has
no `context.value_storage` prerequisite. A raw `BF_WRITE` is not a substitute.

An unbounded root returns the original converted RHS, even when a custom store
normalizes the stored value. The generic `bounded_type` condition examines the
whole root recursively, not just the selected field; when true, lowering emits a
separate checked scalar read after successful publication. That read reacquires
the current world again. A failing callback or reread stops subsequent result
publication, but does not undo mutations already made by a callback. The existing
store ABI provides no rollback guarantee.

Missing/stale storage reports `BEHAVIOR_FUNCTION_HOST_DATA` with
`Referenced storage is unavailable`. A missing or failing publication callback
reports the same failure kind with `Cannot retain referenced component write`.
Source, frame, stack, pin and step attribution use the failing instruction.

## Execution and persistence

The VM, SLJIT and generated C use the same narrow typed-pointer publication helper.
Pointers to scalar operands preserve 64-bit handles and value bits on 32-bit
targets. SLJIT reserves materialization cells even for register-backed operands,
before local and parent-frame storage, and asserts those bounds during emission.
Compiled Wasm uses its existing canonical machine-step helper import for
this instruction. It does not emit an unchecked Wasm store.

The verifier checks scalar types, value provenance, current-world binding identity,
layout/stride and field extent. The instruction is a non-removable write and call
boundary for liveness, read reuse, range proofs, scalarization, borrowing and
fusion. Effects record the component write; query predicates reject it and
parallel scheduling remains conservative. Program preflight checks its installed
component layout, including bool fields. Runtime cost analysis keeps publication
callback costs conservative.

Prepared definitions serialize the canonical instruction using the existing
payload. The opcode declaration macro supplies its name/ordinal schema identity,
so incompatible instruction tables are rejected automatically. This additive
opcode changes neither existing payload layout nor existing opcode semantics and
does not require a manual prepared version bump. No used project storage contract
changes, so no project migration/version bump is needed.

## Baseline and validation

Before implementation, four fixed generic C/pico cases independently passed on
native ASan/UBSan and serial Wasm: 492 assertions each, with 16 small source
compilations, 22 event dispatches and 12 direct calls, budget 100 per invocation.
They cover scalar bits and ranges, versions/deferred observers, managed siblings,
ordinary and compound fallbacks, managed publication, and six callback/storage
failure cases. An initial malformed fixture and missing activation were repaired
before those baseline passes; the failed revision's evidence was retained.

The dedicated `language-component-write` and `wasm-component-write` routes keep
the unchanged production 60-second supervisor. Expanded implementation fixtures
and generated-C checks require fresh independent workload admission and exact
source/binary identities before execution. The fixed selector is included once
in each regular language and Wasm group, preserving the other members and their
order. Those complete groups require independent admission and measurement on
the exact candidate. Full quarantined suites remain disabled, not passed.

The native `language-component-write-layout` route selects only the same eight-map
C sizing case, using the actual internal layout helper without building or executing
authored code. It is a duplicate subset, deliberately omitted from `GROUPS` because
`language-component-write` already runs it once. This bounded diagnostic route is
for safe sizing negative controls, not a replacement for complete validation.

Independent validation at checkpoint `519d9d79` passed, with source and binary
identities checked before each serial route and after the matrix:

- Native ASan/UBSan dedicated cases: 14 cases, 2,944 assertions, 0.143 seconds.
- Serial Wasm dedicated cases: 14 cases, 2,906 assertions, 0.342 seconds.
- Complete 29-member language group: 396 cases, 38,908 assertions, 21.172 seconds.
- Complete 17-member Wasm group: 240 cases, 34,987 assertions, 4.635 seconds.
- Generated C parity: 1 case, 6,039 assertions, 0.857 seconds.
- Prepared integration: 19 cases, 833 assertions, 0.260 seconds.
- Complete native tooling: 116 cases, 33,227 assertions, 23.787 seconds.

Each route used the unchanged production 60-second supervisor; configuration and
build time were separate. Before these passes, source review caught and repaired
the native operand-cell reservation defect without executing unsafe generated
code. The first candidate runtime then caught a fixture catalog-lifecycle error;
balanced installation and static borrowed metadata fixed it. Both failures remain
recorded.

At checkpoint `42448f3c`, independent validation also passed the isolated layout
route (1 case, 14 assertions), both complete language/Wasm groups, native tooling
and prepared integration. In a separate worktree, removing only the checked-write
stack reservation scan made the unchanged third sizing assertion fail as expected
(3 cells instead of 16). That diagnostic never emitted or executed generated code.
Restoring the exact source and rebuilding passed all 14 native write cases again,
including ordinary/inspection compiled execution with live local sentinels.

Integration with eligible main `f1bfe133` retains the reviewed module-composition
fix and checked-write product bytes. Both selectors appear exactly once in the
combined regular groups (30 language, 18 Wasm). Independent review and execution
of exact integrated checkpoint `b38abe0e` passed:

- Language group: 403 cases, 39,346 assertions, 22.575 seconds.
- Wasm group: 247 cases, 35,425 assertions, 3.367 seconds.
- Native tooling, including generated C: 116 cases, 33,227 assertions, 23.789 seconds.
- Prepared integration: 19 cases, 833 assertions, 0.250 seconds.

All four commands ran serially under the unchanged 60-second supervisor, with
source/artifact identities checked before each run and afterward. Native used
Clang 19 Debug ASan/UBSan with leak detection disabled; Wasm used serial Release
with test assertions. The earlier safe control was reverified, not replayed:
its helper, guards and fixture bodies are identical in the integration.

[Machine-readable validation evidence](scalar-component-writes-validation.json)
records exact commits, hashes, configurations, workloads, failures and results.
The subsequent documentation-only change does not alter product, fixture or
policy bytes. Full quarantined suites remain disabled, not passed; no whole-app,
performance, leak-check or unpublished historical-results claim is made.
