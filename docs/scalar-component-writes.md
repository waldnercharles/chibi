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

## Checks

`language-component-write` and `wasm-component-write` cover scalar bits and
ranges, versions and deferred observers, managed siblings, ordinary and compound
fallbacks, managed publication, and callback/storage failures. Run them through
`python3 tools/test_policy.py tool <route>`.
