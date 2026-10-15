# Inferred capture lifetime: decision proposal

Status: **P, not implemented or fully approved**. The owner selected inferred
captures; signature spelling, by-value/reference behavior, mutation and lifetime
still need a decision. This proposal adds no grammar or callable implementation.
See [current decisions](../studio-migration-roadmap/decisions.md) and
[current explicit task-reference semantics](v1.md#c05-ownership-references-and-automatic-lifetime-si).

## Concrete consumer

Studio snapshot `78cb4d4c47242db9f7af2b9573653d9756050e99` supplies two related
small consumers in the same asset-list rebuild:

- [The comparator](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_assets.c#L101-L109)
  reads `sort_index` to look up both integer asset IDs, compares kind first, then
  compares names with `strcasecmp`. [Its caller](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_assets.c#L168-L211)
  assigns the file-global `sort_index = index` immediately before `qsort`.
- [The filter loop](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_assets.c#L212-L237)
  rejects a row when the selected kind differs or
  `studio_matches(s->assets_filter, asset->name)` is false. The cache is keyed by
  the index and its revision, selected kind, collapsed groups and filter text.
  A rebuilt index invalidates numeric asset IDs.

These are synchronous consumers of surrounding context, not stored UI callbacks.
The comparator motivates an explicit callable environment instead of a hidden
file-global pointer. The filter motivates captures of a string and scalar. No
stable-sort/tie-break or matching change is proposed: preserve the current result
contract, including equal comparator results and `studio_matches` behavior.
A safe typed index-generation read API does **not** already follow from the C
pointer; it must be provided or an owning value snapshot used first.

## Recommended minimal choice

1. **Snapshot values at callable creation.** Infer free names from resolved
   bindings. Capturing outer `filter = "cat"` keeps `"cat"` even if the outer
   binding later becomes `"dog"`. Capturing a collection keeps its value,
   independently of subsequent outer replacement or mutation. Evaluate each
   capture once, in a specified order that still needs a fixture and review.
2. **Captured bindings are read-only in the callable.** Assignment through a
   capture is rejected, including captured aggregate subfield/element writes.
   Do not silently interpret a write as mutation of a private copy. Ordinary
   invocation-local variables are separate. Initially require a read-only
   comparator/predicate body and transitively checked callees.
3. **Synchronous and nonescaping first.** The consumer may invoke during its
   current call only. It cannot return, store, enqueue, retain or forward the
   callable to an unverified consumer. Reject `await`, `yield`, task creation,
   suspension-capable callees and synchronous host reentry. No callable can
   remain live across owner close/replacement/reload in this initial contract.
   Runtime owners perform cleanup; authored users manage no arenas or frees.
4. **Owned managed values, honest costs.** Reuse the existing
   [deep value snapshot](https://github.com/waldnercharles/chibi/blob/62906a504fee6b2e0bd6349a8eeff1450529cd90/src/types/array.c#L150-L259)
   semantics where their registered shape is supported. One environment per
   callable creation, not per comparator invocation; release it when its
   synchronous consuming scope ends. Strings/arrays/managed records may copy
   and allocate proportional to captured payload. Type admission and snapshot
   failure must be checked before exposing the callable. Borrowing/snapshot
   elimination is optional only when it preserves the value contract and errors.
5. **Handles retain identity, not resource contents.** Copying a numeric handle
   does not snapshot the resource, freeze it or extend its lifetime. Admit only
   a concrete read-only service/entity handle with owner/generation validation
   and a resource that stays valid for the entire synchronous call. For the
   asset comparator this means a pinned read-only index generation, or copied
   supported values; never capture the raw C `reference_index *`. Reject captured
   local/component/collection `ref` values in this first slice. Do not admit
   task handles merely because their bits fit: they have owner-local semantics.

The body and consumer's effect/lifetime metadata must both be verified. A
native promise labelled pure is insufficient. This is a proposal for a narrow
language/runtime callable contract, not a broad C callback facade or a wrapper
around the existing Studio controller.

## Alternatives and consequences

- **Inferred read-only borrowing:** retain references only for a statically
  proven synchronous call. Potentially fewer copies, but aliases must not change
  captured state while the callable runs. The contract must say whether creation
  or invocation observes the value; changing that silently is unacceptable.
  Borrowing can instead be an invisible optimization of the recommended snapshot
  contract when equality of observations is proved.
- **Inferred mutable references:** writes affect the outer binding and later
  outer writes become visible. Existing
  [task boxes](https://github.com/waldnercharles/chibi/blob/62906a504fee6b2e0bd6349a8eeff1450529cd90/src/execution/program_tasks.c#L157-L231)
  show how locals survive frame retirement, but do not supply a safe general
  closure/host ABI. This option adds aliasing, retained managed roots, owner and
  reload obligations. It is not selected by “inferred captures.”

Stored callbacks need a separate real consumer and a choice between old-owner
invalidation/re-registration and pinned old-generation closures. Task-producing
closures, escaping refs, mutating captures and signature/type spelling stay open.
No option has measured allocation/performance or native/Wasm conformance evidence.

## Acceptance before implementation can be called complete

Use existing/approved source forms for the consumer data; do not require an
unselected record or packed-storage syntax. Add C/pico cases for creation-time
snapshot versus outer mutation, rejected captured writes, string/array lifetime,
unsupported refs, transitive suspend/retention/reentry rejection, owner/generation
invalidation and cleanup on failure. Compare bytecode with actual SLJIT and
actual Wasm. Exercise both real comparator and filter decisions, repeated rebuild
and independent instances, count environment copies/retained bytes, and preserve
last-good output if construction fails. Admit the exact bounded workload under
the unchanged production supervisor before running it.
