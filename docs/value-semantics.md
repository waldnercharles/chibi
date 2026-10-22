# Value collections, pointers and builders

Approved direction, 2026-10-22. This document supersedes the earlier decision
that ordinary dynamic-array assignment shares mutable contents and length.
It also records the owner's decision to remove authored `ref` syntax in favor
of pointer parameters and address-taking. These changes are not implemented by
this planning checkpoint.

## Observable behavior

Assignment and ordinary function arguments copy the value of their type.
Arrays, maps and builders are values. Copying a pointer copies access to the
same target. Copying a record copies its fields according to their types:
value collection fields remain independent, while pointer fields preserve
their target identities. A copied array of pointers has independent slots but
still points to the same objects. This is not a recursive clone of an object
graph.

The following are acceptance examples for the approved semantics:

```chibi
a: int[] = [10, 20]
b := a
b[0] = 99       // a remains [10, 20]; b is [99, 20].
p := &a
p^[0] = 7      // a is [7, 20]; b remains [99, 20].
```

Ordinary arguments follow the same rule. Changing a local value parameter
does not change the caller's collection or builder. Passing `&value` to a
pointer parameter permits changing that caller's value. A pointer already
stored in a variable can be passed directly; it continues to denote its target.

Builder API examples are proposed method syntax, not implemented declarations:

```chibi
a := strings.builder()
a.append("Hello")
b := a
b.append("!")  // a builds "Hello"; b builds "Hello!".
p := &a
p.append("?")  // a builds "Hello?"; b still builds "Hello!".
text := a.build()
a.clear()      // text remains "Hello?".

AppendSuffix :: (builder: ^Builder) {
    builder.append("!")
}
AppendSuffix(&a)
```

The builder is reusable. `build()` returns an immutable, byte-preserving string
snapshot. Assignment and ordinary arguments do not require `clone()` or `move`.
An explicit copy operation may still be useful to release a retained large
backing allocation; it is not necessary to make ordinary values independent.

## Copy-on-write

Copy-on-write changes allocation cost, not the results above. Logical copies
may initially share backing storage. Before a write, the runtime gives the
destination independent backing when another value or published string can
still observe the old bytes. Unshared backing can be mutated in place.

The first mutation of a shared array may copy all of its elements. Repeated
mutation of one unshared builder must not repeatedly copy its accumulated
prefix. Ordinary reads must not detach storage. Nested value collections obey
the same rule at each modified level; explicit pointer elements retain their
identity throughout.

Swift provides the closest source-backed precedent for value collections and
copy-on-write: [value and reference types](https://www.swift.org/documentation/articles/value-and-reference-types.html)
and [Array implementation](https://github.com/swiftlang/swift/blob/main/stdlib/public/core/Array.swift#L175-L228).
Chibi's implementation must fit its tracing collector. Swift's reference-count
uniqueness mechanism cannot simply be copied into that collector. Audit and pin
any source actually ported, including its license, before copying code.

A conservative shared flag on backing is a possible implementation: logical
copies mark it shared; mutation detaches; fresh detached backing is exclusive.
This can cause an unnecessary later copy after other aliases die, but does not
require user-facing ownership rules or exact reference counts for correctness.
It is not sufficient evidence of amortized append cost: repeated ordinary
argument or identity-result copies can die before each append while leaving
the shared flag set, forcing repeated prefix copies. The design must release
logical owners deterministically where needed, prove safe borrowed reads or
elide copies at last use, and measure the resulting workloads. GC reachability
alone does not identify those dead temporary owners soon enough. The storage
mechanism remains an implementation proposal, not part of the language contract.

Logical copies and temporary reads must be distinguished. Copies into ordinary
arguments, variables, fields, returned values and retained task/state storage
must preserve independence. Address-taking and a temporary read solely used to
mutate an existing location must not manufacture another logical copy. In
particular, marking every managed READ as shared would make repeated builder
append operations quadratic. A mutation resolves the writable location,
detaches and updates that location when needed, then performs the write.

## Prerequisites and sequence

1. Reconcile the array work with this contract. Preserve its owned record
   metadata, GC tracing and native-boundary checks. Replace shared assignment
   behavior with logical value copies and detach-before-write behavior. Apply
   the same contract to maps; do not quietly retain a reference-semantic map.
2. Support collection-valued record fields in both compilers, layouts,
   snapshots, persistence and tracing. The current authored-record restrictions
   reject them. A Builder should contain ordinary `byte[]`, rather than a
   special native object whose copying has different rules.
3. Replace source `ref` with pointers after settling the external-location and
   iteration cases below. Preserve internal checked-place operations and
   generic managed-cell helpers; these are also used without the keyword.
4. Add authored mutating receiver calls. Resolve a writable receiver exactly
   once and mutate its location. Declaration syntax and rvalue receiver rules
   need a concrete API discussion before implementation.
5. Implement Builder in Chibi, with append, reserve, clear and immutable build
   snapshots. Keep allocation, GC and storage barriers in narrow runtime
   primitives. Character append uses the approved explicit UTF-8 operations.
6. Validate the combined source and bootstrap artifact, then continue to the
   next package decision. A past passing checkpoint is not validation of the
   combined result.

The array chat owns the existing collection work. This planning worktree is
isolated from its uncommitted implementation and from the clean managed-pointer
checkpoint `f04d9d37822edb44a8fb1d1f6f68e5546eb8e049`. Integrate clean reviewed
source checkpoints and current main before producing a new compiler artifact.

## Remaining semantic decisions

Current `ref` uses cover more locations than managed pointers currently do:

| Location | Existing behavior | Required decision or work |
| --- | --- | --- |
| Local, value parameter, flat authored field | Managed address-taking works | Migrate parameter types, calls and body accesses. |
| Existing alias parameter | Scoped forwarding works | Pass its replacement pointer directly; add a pointer level when rebinding a pointer. |
| Array element or map entry | Logical index/key reference | Define address identity across CoW, resize, removal and replacement. |
| State or engine component field | Checked external write with publication callbacks | Define scoped versus escaping pointers and owner invalidation. |
| `for item: ref in values` | Mutates the selected collection item | Agree on pointer iteration or explicit indexed mutation; ordinary value iteration is not equivalent. |
| Mutable retained view | Earlier shared-array design retains an owner and range | Reconcile with value collections and explicit pointer sharing before implementing. |

Never turn the address of external storage into a detached local copy without
making the changed behavior explicit. Engine writes must retain validation,
effects and change publication. A managed pointer keeping bytes alive does not
by itself make a destroyed entity or replaced catalog valid.

Approved on 2026-10-22: engine-owned locations use scoped direct mutation with
the same `^T` and `&value` syntax. Infer whether a parameter or a derived pointer
can escape or cross suspension, and accept an external address only at a call
proven safe for that origin. Ordinary managed pointers retain their existing
escape guarantees. The runtime must use checked logical locations and preserve
write publication; it must also handle owner invalidation during a synchronous
call. Static nonescape analysis alone cannot prove that an entity remains alive.
Unknown callback/retention contracts must not be assumed safe. The owner
selected this checked direct-mutation model over mandatory read/modify/write
helpers. It is approved but not implemented at this checkpoint.

Rust's [reference lifetime checks](https://doc.rust-lang.org/book/ch04-02-references-and-borrowing.html#dangling-references)
provide a precedent for diagnosing invalid borrowed access during compilation.
This design does not adopt Rust's full ownership model, exclusive mutable
borrowing or user-written lifetime parameters. Current general Chibi `ref`
supports task capture and suspension, so retiring it requires deliberate
migration of those callers; its existing flag is not a noescape proof.

Removing `ref` requires a semantic source migration, not search-and-replace:
`ref int[]` becomes `^(int[])`, scalar reads/writes dereference the pointer,
forwarding an existing alias passes the pointer, and a local argument uses
`&local`. Preserve a parser for sources written under project version 33 if a
project migration needs it. Only bump project format when a named existing
project requires rewriting, following AGENTS.md.

## Verification to accompany implementation

Cover independent assignment and ordinary arguments; nested collection/record
copies; explicit pointer aliases; pointer elements; immutable build snapshots;
growth, removal and clear; escaped owned locals; suspended calls; and GC after
roots disappear. Include a measured append workload that detects repeated
prefix copies, in addition to semantic assertions. Both compiler stages and
the supported execution backends need equivalent results. Record exactly which
checks ran on each source/artifact checkpoint.

The append workload must include read-only helper calls and identity-return
calls between appends, with discarded temporaries and infrequent GC. Simple
append-only loops do not expose all unnecessary-copy paths. Actual retained
snapshots are different: preserving independently observable old values can
require real copies.

No runtime changes or validation results are claimed by this document.
