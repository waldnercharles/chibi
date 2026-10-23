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
| State or other external field without a durable resolver | Checked external write with publication callbacks | Implement approved scoped direct mutation and owner-invalidation checks. |
| Query component | Draft logical component cell | Preserve original incarnation across suspension; invalidate on removal; finish typed recovery API. |
| `for item: ref in values` | Mutates the selected collection item | Agree on pointer iteration or explicit indexed mutation; ordinary value iteration is not equivalent. |
| Mutable retained view | Pointer-backed view approved in the array chat | Follow the array currently stored at the pointer, with fixed indices and current full-range checks. |

Never turn the address of external storage into a detached local copy without
making the changed behavior explicit. Engine writes must retain validation,
effects and change publication. A managed pointer keeping bytes alive does not
by itself make a destroyed entity or replaced catalog valid.

Approved on 2026-10-22: external locations without a durable resolver use scoped
direct mutation with the same `^T` and `&value` syntax. Infer whether a parameter or a derived pointer
can escape or cross suspension, and accept an external address only at a call
proven safe for that origin. Ordinary managed pointers retain their existing
escape guarantees. The runtime must use checked logical locations and preserve
write publication; it must also handle owner invalidation during a synchronous
call. Static nonescape analysis alone cannot prove that an entity remains alive.
Unknown callback/retention contracts must not be assumed safe. The owner
selected this checked direct-mutation model over mandatory read/modify/write
helpers. It is approved but not implemented at this checkpoint.

The same helper may accept either kind of origin:

```chibi
Heal :: (health: ^Health) {
    health.current += 10
}
Heal(&localHealth)  // Ordinary owned local.
Heal(&health)       // Engine location, when the call is proven scoped.
```

The compiler must follow derived origins through forwarding calls and aggregate
values. Unknown retention, returning or publishing an external pointer, and
crossing suspension are not accepted as scoped use. Summaries must reach a
fixed point across recursive calls and imported functions; checking only the
immediate function body is insufficient. Runtime publication boundaries must
also reject scoped pointers that were not admitted by trusted compiled code.

Also approved on 2026-10-22: query-produced component pointers may survive
suspension by automatically resolving the same logical target. Storage movement
does not change source behavior:

```chibi
saved := row.health
await Delay(1)
saved.current -= 1
```

The query prototype already resolves attached cells through world, full entity
identity and component identity on each dereference. Its removal policy retains
a detached final value; that implementation choice is now superseded. A logical
component handle retains its original component incarnation, resolving the
current storage while that target exists. Removal, entity destruction or world
destruction permanently invalidates the target. A later replacement component
is a different target and never revives or redirects the old pointer. The owner
selected recoverable invalid-target failure for subsequent access. The query
owner is implementing this policy; its concrete failure representation and
public recovery API must be documented before claiming full recovery support.

The owner subsequently approved prevention through task lifetime binding.
An operation bound to that original target cancels if the target is
removed; its children cancel through the existing task tree. Surviving independent
awaiters receive the existing typed `Cancelled` result; owned child waiters
cancel with their parent. Completed successful tasks remain successful if their
former target is removed later. Unrelated tasks can hold the same pointer
without acquiring that dependency. Retaining a pointer does not delay deletion,
and replacement does not revive a bound operation.

Children inherit the lifetime dependencies when they start. A child
that continues after its parent completes retains those dependencies itself;
later target removal cancels the child without changing the parent's retained
successful result.

The owner subsequently rejected requiring a call-site marker and asked for
inferred dependencies. This supersedes the earlier explicit call-site choice;
the proposed `on health` spelling is rejected. Merely storing a handle must be
distinguished from work that requires its target. The private runtime interface
accepts resolved targets before the task's first instruction; the compiler
producer remains to be implemented.

After exploring dependencies that change as execution progresses, the owner
rejected waiting until a conditional access to detect removal: unrelated
post-await side effects might already have run. While a task may still access
the component, a future conditional access counts as a dependency. Removal must
cancel before the authored continuation resumes, even if that branch would
have been skipped. Earlier effects are not rolled back. This applies to
removable engine-owned targets used by tasks or behaviors, not only systems;
ordinary managed pointers retain their allocations.

The owner then approved keeping an inferred dependency until the task finishes,
even after its final component access. For example, `Poison(health)` applies
damage, waits, then plays a completion sound without reading health again.
Removing health during that wait cancels the sound and the rest of Poison.
Copying a component value inside an already-bound task does not detach the task.
A surviving caller can start separate work using copied values; ordinary child
task cancellation still applies, so spawning a child does not let it escape its
parent's cancellation. Completed successful tasks retain their results.

The inference representation and handling of targets selected later remain to
be specified. Compiler optimizations must not end a dependency early or change
observable cancellation timing. The runtime's fixed target-set prototype is
not proof of complete authored inference.

Removal takes effect at safe execution boundaries. A bound operation must not
consume a pending request's result or execute another authored continuation
after its target is invalid. The scheduler must also inspect pending tasks whose requests
never complete. Short protected access prevents removal during the protected
operation; suspension ends that protection. External asynchronous work may have
already completed; cancellation does not reverse its effects. Runtime cleanup
releases requests and captured values. It does not undo earlier writes or imply an authored
cleanup callback. General checked access to an already-invalid unbound pointer
remains a separate typed-result API question.

Followup actions belong in task sequencing. The owner rejected `destroy_after()`.
Existing `await` can sequence a followup on successful completion:

```chibi
animation := DeathAnimation()
result := await animation
if result is not Cancelled {
    // Perform the followup here.
}
```

The existing `after` declaration targets declared actions. Extending it to task
handles is a separate language change, not required for this runtime increment.

Recovery audit: authored Chibi currently has tagged unions and `switch`, but no
general `try`/`catch` or result-producing runtime failure boundary. A checked
dereference failure stops the activation and reports structured diagnostics to
the host. Task cancellation can reach an existing waiter as `Cancelled`, without
the underlying failure reason, and is not a general recovery mechanism. Merely
using that runtime failure path does not fulfill source-level recovery.

The owner explicitly ruled out introducing `try`/`catch`, ever. Recovery must use
typed values/results. Discuss checked pointer operations and a possible result
that exposes scoped mutable access, so ordinary mutation need not require a
manual copy/write-back cycle. Neither API has been adopted. A separate validity
precheck is insufficient; validation must accompany the actual access, or a
scoped access contract must prevent invalidation for its entire use.

One API sketch for that discussion, not adopted syntax:

```chibi
switch saved.access() {
    case .Available(health):
        health.current -= 1
    case .InvalidTarget:
        return
}
```

The available case would expose scoped mutable access, not a copied component.
It must not escape or cross suspension. Its validity must hold throughout the
use, including calls that could remove or replace the component; a one-time
check followed by an unprotected pointer does not meet this contract. Those
invalidation rules need a concrete discussion before implementation.
Rust's [`Weak::upgrade`](https://doc.rust-lang.org/std/rc/struct.Weak.html#method.upgrade)
is a precedent for reporting target availability as a typed value. Its successful
result is an owning `Rc`, so its ownership and lifetime rules are not this scoped
Chibi proposal and must not be copied wholesale.

The optimization is separate from the lifetime contract. A cached address can
be reused while a checked storage/layout version is unchanged. Relocation
invalidates that cache; ordinary value changes need not. Suspension discards a
compiler proof of address stability, but does not necessarily require a full
lookup if runtime versions still match. Proofs may also eliminate repeated
checks inside a region where structural mutation cannot occur.

Flecs provides an open-source precedent: its MIT-licensed
[`ecs_ref_update` implementation](https://github.com/SanderMertens/flecs/blob/a141ad44b57e5ca481b3b477bdd2ff8107b1b5fc/src/ref.c)
uses table versions to avoid resolving unchanged references. Its entity/component
pair contract is not evidence for Chibi's proposed incarnation-preserving
contract. Port only the applicable cache algorithm after auditing the pinned
source and retaining its license notice.

Correct lookup-per-access comes before caching. The cache needs a stable world
control block, entity generation, component incarnation and retained or checked
layout metadata. Current query membership revisions and component publication
versions do not establish address stability. Every relocation and schema
replacement must participate in the cache's invalidation protocol. A version
check alone cannot prevent a concurrent free between checking and using the
address; execution must retain its structural-mutation exclusion or use another
verified synchronization mechanism. A logical handle supports escape and
suspension; a raw external location without a durable resolver remains scoped.
The lifetime extension is approved; its implementation is not yet validated.
Caching remains a separate optimization after correct lookup-per-access behavior.

The owner approved pointer-backed mutable views in the array chat: `p := &a;
v := p.view(range)`. A view follows the array currently stored at `p`, even
after reassignment, at fixed numeric indices. Ordinary copies of `a` remain
independent. The array implementation owns this protocol and validates the
current complete range on access. A view must not extend a scoped external
pointer's lifetime; that origin must participate in escape checking.

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
