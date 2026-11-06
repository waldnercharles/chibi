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
independent of this revision.

Mutable views require an explicit pointer to an array:

```chibi
p := &a
v := p.view(0..<2)
if v {
    v[0] = 7
    // a is [7, 2, 3]; b is [9, 2].
    a = [4, 5]
    // v now shows [4, 5].
}
```

The view retains that logical pointer and fixed indices. Each access resolves
the array currently stored at `p`, including after reassignment of `a`. Writes
through a view update `a`; ordinary copies of `a` stay independent. The whole
window must fit the current array: shrinking can make it invalid, and regrowing
can restore it. A mutable `a.view(range)` on an ordinary array is rejected.
Immutable string views retain byte backing directly. A retained view must not
extend a scoped external pointer's lifetime.

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
layout metadata. A reference-counted external backing uses a collectable anchor
for tracing its initialized elements once. Managed copies must belong to the
same validated heap family; ordinary native persistence uses capture. Maps use
an equivalent entry/index backing.

Cleanup during collector sweep releases external memory without dereferencing
arena allocations: the collector may already have retired a child or tracing
anchor. Deterministic release outside collection can drop the nested value
handles owned by the last backing reference. Later cleanup is idempotent.

Ordinary values are finite array/map/record/union structures. The C value-copy
boundary rejects a raw cycle made directly from those descriptors with a
recoverable error. Explicit managed pointers can form cycles and retain their
target identity; historical capture remains cycle-aware. Recursive native value
admission has a 256-level resource bound; logical release uses an explicit work
queue rather than one C stack frame per nested container.

Ordinary copies of boxed outer storage must create independent nested value
handles. Copying raw child descriptor pointers retains forbidden mutable aliases.
Every detach or growth requires recomputing interior addresses. Explicit pointer
traversal stops ordinary value isolation and writes the pointer's target place.

## Compiler barriers

- Declarations, assignments, ordinary arguments/results, element/field stores,
  state/event storage and optional/union publication establish value copies.
- Temporary symbol/index/length reads and mutator receiver evaluation borrow.
- Mutation targets locals, parameters, results, state or pointer cells, followed
  by field/index/map paths. The earlier plain-receiver intrinsic54 was removed;
  nested mutations require checked writable locations.
- BF_COPY_ARRAY can express a logical copy; existing reference paths can express
  writable places. Native and authored compilers must use the same protocol.
- Intrinsics57/58 express value copy/drop, and59 mutates an array through an
  existing logical reference path. Intrinsic60 projects an active union payload
  into a checked writable path;61 deletes a map entry through a writable path.
  Mutable view52 receives a pointer to ARRAY; string view52 continues to receive
  immutable STR.
- Retain proven readonly call borrowing and valid fresh-result/last-use transfer
  optimizations. Do not make every lifetime snapshot mark backing shared.
- Ordinary foreach can take one CoW value snapshot; pointer/reference iteration
  depends on the separate ref-removal design.
- Compiler `EnsureOptional`, `EnsureSequenceCollection` and type-reader helpers
  update their owning type tables through explicit pointers or return updated
  tables. These updates no longer depend on ordinary array arguments aliasing.

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

Execution tracks owning shells created by constructors, native outputs and
explicit copies. Borrowed headers do not become additional owners. Scope and
overwrite drops retire discarded values; frame cleanup covers errors and
remaining temporaries. Returning a value transfers ownership to the caller
before retiring the callee. Argument copies occur immediately after evaluating
each argument, before evaluating the next one.

## Implementation order and holds

Draft instruction allocation is coordinated with the pointer and query owners:

| IDs | Owner |
| --- | --- |
|46-50|Existing managed pointers.|
|51-61|Sequence operations, value copy/drop, collection place mutation, union projection.|
|62-79|Unallocated.|
|80-95|Query family, with80-88 currently assigned.|

ID54 is reserved: the earlier plain-receiver array mutator was removed. Array
mutation uses the checked writable-place operation59.

Both candidates currently say prepared ABI22. This does not make their schemas
compatible. Combine the actual layouts and instructions before freezing one
bootstrap and runtime seed; the current compiler/assets ABI20 milestone remains
independent.

1. Establish a separate CoW candidate while preserving this draft.
2. Define copy/borrow/transfer and writable-place contracts across VM, JIT and
   generated C, including scalar nested values and ordinary call boundaries.
3. Implement array/map payload ownership and complete nested write barriers.
4. Migrate both compilers and their own metadata-table algorithms together.
5. Integrate pointer-backed mutable views and current-pointee bounds checks.
6. Verify semantics, lifetime, backend parity and call-loop allocation behavior;
   regenerate the matching bootstrap only once the source/ABI settles.

The pointers chat owns Builder and ref-removal work. Array CoW is a prerequisite
to using ordinary byte[] as a value Builder's backing. Coordinate clean source
checkpoints; do not import that chat's dirty worktree.

## Evidence

The active candidate is `codex/value-array-cow-20261022` in
`.worktrees/value-array-cow/chibi`, at HEAD `329d8fb046612caa17f4aa99ee61f96512a2fa60`
with the resolved, uncommitted pointer prerequisite merge
`f04d9d37822edb44a8fb1d1f6f68e5546eb8e049`. It is separate from the preserved
shared-array draft. No commit, push or publication has occurred.

On 2026-10-22 a fresh stage-zero Release build completed for `test_language`,
`test_tools` and `chibi-compiler-host`, with sanitizers disabled. Configuration
took 2.7 seconds, the CMake native-host build 3.0 seconds, third-party preparation
0.6 seconds, and the Chibi graph build 14.4 seconds. The graph log is
`.chibi/logs/1792715022572335000-40519-graph.log`. The drivers were built and
were not executed.

Compile-only preparation then accepted all 42 compiler/core source files in
30 authored packages. Linking the scratch helper and preparing the sources took
7.793 seconds. Its source and logs are in workspace
`.scratch/shared-sequences-20261022/source-build`; the helper binary was removed
after success. This prepares the compiler definitions without invoking the
resulting compiler or reproducing a bootstrap.

The full preparation exposed excessive verifier storage in `EmitBody`. The
verifier now retains possibly-undefined operand states at basic-block entries
and checks each instruction in order within its block. Its existing 64 MiB cap
includes all working storage. Independent source review found no blocker;
provenance, control-cycle and suspension checks are unchanged.

PR #633 is merged at head `86bb84597a76ccf93905e527a1bc15b378459bea`. Its
byte-preserving string representation is a prerequisite; its scope explicitly
deferred shared slices and copy-on-write. This candidate builds on that work.

The pointer owner has supplied clean checkpoint
`22dc0d935b1478b519801597f16d547974d30c67`, including newer main integration and
its own matching ABI21 bootstrap. That checkpoint and the query draft have not
been imported here. A common integration must reconcile their actual schemas
and produce one matching compiler artifact.

The owner subsequently requested continuing with focused tests and bootstrap
verification. Runtime, native compiler and authored compiler regressions use the
existing C dispatchers. Execution is coordinated with the other compiler/runtime
work using the shared validation lane.

The fresh native build for frozen tree `cb5cbcfe` completed on 2026-10-22.
The first `types` run passed all 11 new cases but failed an incorrectly changed
document-byte ownership assertion: that reader still owns its bytes inline in
the arena. After restoring that assertion and its existing GC checks, tree
`f90c191e8be1305f4ea05344e7f01fbf155889c8` passed all 27 `types` cases and 4861
assertions in 0.550 seconds. Its `language-record-runtime` run passed six of
eight cases, including all three new resolver cases, and failed two older cases
because their harness supplied no caller-owned managed heap. It reached 834
assertions in 0.659 seconds. Source, binary hashes and logs are sealed in
workspace `.scratch/shared-sequences-20261022/native-phase-result-2.json`.

The subsequent harness migration supplies a fixture-owned heap through result
inspection. Values intended to survive producer heap and module destruction
use historical capture before that teardown. Array metadata ownership checks
follow the actual managed owner, then verify the escaped capture owns its
layouts. The execution harness also uses the stabilized argument bank while
suspended, independent union payloads and real managed pointer cells. These
harness changes await a fresh run. Source review also found obsolete union
cost/provenance summaries: both constructors now copy payloads as values, so
their summaries report allocation and possible payload copying without using
the old keep mask. Empty and cancelled-singleton behavior is preserved. Native
semantic/execution groups, authored collection/sharing groups and bootstrap
reproduction remain unrun.

The pointer owner assigned the resolver-aware retained-layout increment to this
workstream. `pin_record_from_type_resolved` freezes registry-local aliases and
children using one borrowed resolver during sizing and initialization.
`pin_record_resolver` resolves only owned frozen metadata and rejects conflicting
layouts under the same name. Independent source review found no blocker. The
pre-increment files are preserved in workspace
`.scratch/shared-sequences-20261022/resolver-record-before`.

The included bootstrap artifact remains prepared ABI21 while these draft sources
use ABI22. The standard artifact regeneration target includes the self-hosting
check. Common query/task integration is still separate from this candidate.
