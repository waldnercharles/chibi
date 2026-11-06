# Value sequences and copy-on-write

## Current decision

On 2026-10-22 the owner explicitly revised the previous shared-array decision
in the pointers chat (`01a15cd2-7126-7cc0-bc72-da5791aae3d5`). Ordinary arrays,
maps and builders are independent values. Explicit pointers share mutable
access. Storage may initially be shared, but writing one ordinary value must
not change another. Pointer elements retain pointee identity.

```chibi
a: int[] = [1, 2]
b := a
b[0] = 9
// a is [1, 2]; b is [9, 2].
p := &a
p.append(3)
// a is [1, 2, 3]; b is [9, 2].
```

Nested ordinary values follow the same rule. `b := a; b[0].append(x)` must
isolate both the outer value and its selected child. A pointer element remains
a shared pointer; changing its target remains visible through other pointers.

The earlier copied half-open bracket/method slices, strict optional bounds and
explicit outer clone surface remain usable. Immutable string storage is
independent of this revision. Mutable views need a decision: a pointer-backed
place with fixed indices, an explicit retained-value alias exception, or defer.
No mutable-view rule is inferred from the CoW decision.

## Preserve and reuse

The draft is preserved on `codex/shared-array-objects-20261022` at
`.worktrees/shared-array-objects/chibi`, with its pointer prerequisite merge
uncommitted and source conflicts resolved. HEAD is `329d8fb04`; MERGE_HEAD is
clean pointer checkpoint `f04d9d378`. All tracked source changes and the three
untracked source/document files were additionally archived under workspace
`.scratch/shared-sequences-20261022/shared-array-before-cow*` before this revision.
No commit, push or publication has occurred.

Reuse immutable string owners and arena cleanup, frozen native/authored record
layout closures, heap-family validation, managed roots/tracing, callback result
imports, fail-atomic defaults/reload, native persistence captures, cycle-aware
historical capture/equality, range parsing, boxed optional bounds, and typed
sequence get/length. These do not require shared ordinary assignment.

Replace managed-root identity copies, mutation through a plain evaluated
receiver, and compiler metadata updates that depend on implicit shared arrays.
Do not treat the preserved draft's public reference text as the final contract.

## Runtime operations

Separate intent rather than overloading generic snapshots:

| Operation | Contract |
| --- | --- |
| Import | Retain borrowed C storage safely in the managed owner family. |
| Value copy | Create an independent logical descriptor, initially sharing payload backing. |
| Borrow | Read or resolve the existing value without creating another logical owner. |
| Transfer | Move a provably dead value into its destination. |
| Mutation | Resolve a writable place, ensure unique backing along its path, then write. |
| Capture | Keep an independent historical/native-persistence value. |

A value descriptor owns logical length and a backing reference. Backing owns
capacity, representation, data/items, stride, optional indexes and immutable
layout metadata. Trace descriptor to backing, then initialized elements; shared
payload should be traced once. Maps use an equivalent entry/index backing.

Ordinary copies of boxed outer storage must create independent nested value
handles. Copying raw child descriptor pointers retains forbidden mutable aliases.
Every detach or growth requires recomputing interior addresses. Explicit pointer
traversal stops ordinary value isolation and writes the pointer's target place.

## Compiler barriers

- Declarations, assignments, ordinary arguments/results, element/field stores,
  state/event storage and optional/union publication establish value copies.
- Temporary symbol/index/length reads and mutator receiver evaluation borrow.
- Mutation targets locals, parameters, results, state or pointer cells, followed
  by field/index/map paths. The current intrinsic54 plain receiver is insufficient
  for arbitrary nested writable locations.
- BF_COPY_ARRAY can express a logical copy; existing reference paths can express
  writable places. Native and authored compilers must use the same protocol.
- Retain proven readonly call borrowing and valid fresh-result/last-use transfer
  optimizations. Do not make every lifetime snapshot mark backing shared.
- Ordinary foreach can take one CoW value snapshot; pointer/reference iteration
  depends on the separate ref-removal design.
- Compiler `EnsureOptional`, `EnsureSequenceCollection` and type-reader helpers
  currently append to ordinary `types` parameters and return only indices. Give
  these updates explicit pointer access or return updated tables. Do not add new
  ref-based interfaces while ref removal is being designed.

## Performance requirement

A monotonic shared bit is sufficient for isolation but is not a complete
performance argument. Dead argument/result descriptors that remain until GC can
make repeated observe/identity-then-append loops detach unnecessarily.

The implementation must distinguish borrowed reads from real copies and either
release logical ownership deterministically on overwrite/scope/call exit, or
prove readonly borrowing/result elimination at the relevant boundary. Genuine
retained copies may require a detach; ordinary receiver reads must not. Keep
geometric capacity growth and require evidence before claiming append complexity
through call boundaries. Do not implement CoW by changing every snapshot into a
new owning shell.

## Implementation order and holds

1. Establish a separate CoW candidate while preserving this draft.
2. Define copy/borrow/transfer and writable-place contracts across VM, JIT and
   generated C, including scalar nested values and ordinary call boundaries.
3. Implement array/map payload ownership and complete nested write barriers.
4. Migrate both compilers and their own metadata-table algorithms together.
5. Integrate the decided mutable-view contract; until then hold its lowering.
6. Verify semantics, lifetime, backend parity and call-loop allocation behavior;
   regenerate the matching bootstrap only once the source/ABI settles.

The pointers chat owns Builder and ref-removal work. Array CoW is a prerequisite
to using ordinary byte[] as a value Builder's backing. Coordinate clean source
checkpoints; do not import that chat's dirty worktree.

## Evidence

One combined native source build passed before later verifier/runtime follow-ups
(graph131.0seconds). Fresh stage-zero preparation subsequently exposed missing
sequence verifier cases and a raw integer branch; both are fixed in source, but
that exact final draft has not completed a fresh build. This is draft evidence,
not a CoW implementation claim.

No new sequence tests have been added or run. A session developer instruction
requires an explicit user request before tests. The user asked why bootstrap
regeneration is needed; that explanation does not authorize tests. The included
bootstrap artifact remains prepared ABI21 while these draft sources use ABI22.
The standard artifact regeneration target includes the self-hosting check.
