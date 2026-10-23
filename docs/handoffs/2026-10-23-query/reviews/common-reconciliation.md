# Common compiler reconciliation map

Read-only audit, 2026-10-22. No common source has been imported or validated.
Inputs: task6353f5a382785977b0b9948cfcbcde37facd0d0c on81702;
rich+Append treeb3582f1774816d0e010edecffe71d9dfe3e371de on maind0de4a84.
Task's later docs-only lifetime overlay is f0fe82ef0e09532f74c356fc194edf556f78d13d.

Main d0 is preparedABI20 and does not include pointer source60a407503 or
artifact22dc0d935. Its common ancestor with81702 is1bc97b730. Reconcile pointer
source before applying task6353; do not treat the old pointer artifact as proof
of the combined compiler. The rich source is awaiting its own validation and
is not yet eligible for import. Collection test authorization is now satisfied,
but its new resolver source still needs review/validation and is not part of
this first task/query integration. Preserve query's existing carrier temporarily.

## Compiler ownership and order

Use the rich compiler as the structure to extend. Preserve public pure
Native5/Port4 and rich CatalogNative9/private ResolvedNative10,
CatalogPort10/ResolvedPort10: direct, qualified name, kind, actual intrinsic,
nominal/default metadata and private source ownership all matter.

1. Add Callable.waits initialization and mark waiting callables. Expand synthetic
   task result declarations before both rich cached-body preparation paths:
   Behavior.CompileSources and Compile.Link/PreparePackages. Their Body results
   and every assembler must share exactly the same extended type/member indices.
   Task's current Assemble-only expansion is too late after rich body caching.
2. Preserve BodyContext and enriched Body.Emitted, semantic effects, direct_write,
   and sparse facts. Keep Work phases -1 effects, -2 native result facts,
   -3 owner rewrite, -4 semantic facts before AST indexing. Insert task dispatch
   and result narrowing into this loop, plus SUSPEND source point numbering.
3. Extend CallStep/ValueStep with task-handle selection before ordinary result
   selection. Preserve native overload/default binding, owner rewrites, result
   provenance, RecordNativeCall and exact source IDs. TaskStep needs the rich
   ResolvedNative array and CalledFunction resolution, including ambiguity and
   shadowing checks before its Delay shortcut.
4. Assemble consumes prepared[index] and retains Game.Library, source mode,
   body_effects and INVOCATION_READ. Add command/host index separation, command
   banks and invocation.task. Mask SUSPEND inheritance for asynchronous task calls
   at the final effect merge while preserving WRITE.
5. Cancel/cancel-after command Hosts use linked=-1. Feed their WRITE through
   existing WriteStep/RecordSemantic(WriteFact) before sparse preparation;
   serializer-only WRITE would leave WriteSummaries/GenericUpdates unsound.
6. Retain BodyContext.Unknown(false) for legacy behavior entry points and
   known masks where supplied. A zero known mask is not an unknown contract.
7. Add target.error_status_offset alongside existing rich target facts, then
   carry the authored writer command schema, bounded empty banks, source embedding
   and C fixture migration together. Preserve stable compiler public APIs.

## Query and runtime boundaries

Query owner independently audited delta/schema target offsets, evaluator/query
writer banks and task fields; phase60..71 does not collide with kind-scoped task
phases1..3. Query will reconcile only query-owned code against the exact common
source after compiler/pointer/task reconciliation. Preserve d0 record validation
cache only for the schema-independent pin_record_valid proof within one unchanged
catalog/validation scope; carried-schema/provenance checks remain distinct.

The newly approved lifetime rule retains inferred targets until task completion,
even after their last access. Target removal before completion cancels remaining
sound/notification/effects at a safe boundary. No rollback, authored marker or
try/catch. Ordinary child-task cancellation remains; copied values do not detach
an already-bound task. Targets selected later and the inference producer still
need precise integration. The fixed-target runtime snapshotb2c82d remains a
separate source-only prototype, not an executed inference proof.

## Evidence limits

Independent read-only review supplied the compiler function map and identified
the declaration-order and sparse-WRITE interactions above. No merged candidate,
bootstrap, executable, test result or ABI compatibility is established by this
map. The corrected task evidence remains in corrected/review.md and is unchanged.

## Narrow resolution and future regression triggers

Keep rich structs/body caches and graft task fields/dispatch onto them. A small
shared preparation helper should return waiting callables plus the expanded
TaskResultDeclarations type/member tables once, before either executable-source
or pure-package body preparation; Assemble only consumes them. Do not append
the synthetic declarations again per package. Resolve task commands through the
existing rich callee path; schedule WRITE semantic facts for cancellation at
step creation and preserve the rich two-pass stability check.

The common candidate needs these focused cases, using existing C dispatchers:

- Several waiting functions returning different shapes (int, byte-preserving
  string and record), with joins in both executable source and an imported
  source-owned library. Compare accepted results and Cancelled discrimination
  on both backends. This exposes late/duplicate synthetic declaration indices,
  result tag mixups, and losses of exact source-owned call resolution.
- A helper whose only write effect is cancel/cancel-after, called through a
  second helper and used inside a mutation-sensitive append-item computation.
  Compare the native and authored compiler's exposed effect contract and
  existing append semantic observations. Cancellation must stay WRITE through
  the sparse fixed point; no assertion should depend only on private row layout.
- An authored function named Delay that shadows the built-in, plus conflicting
  native/source candidates and wrong command arguments. Preserve established
  successful call selection or the precise existing diagnostic. This detects a
  task shortcut bypassing the rich resolver.
- Asynchronous child start versus directly awaited call: both propagate WRITE
  when appropriate; only direct await propagates suspension to its caller.
  Verify eager execution and continuation order, not merely instruction counts.
- Once the lifetime runtime is integrated, bind a target, perform its final
  component access, suspend, remove it, then confirm later sound/notification
  effects do not execute and a surviving independent waiter receives Cancelled.
  Keep completed-success results unchanged after subsequent removal.

No test source was added or executed for these future interactions here. Their
fixtures must be written against the final common source and the active owner's
validation schedule; the existing separate proofs do not establish them.

## Subsequent source-only reviews

The collection resolver patch was independently read at SHA256
b64d7ebb95cbdb485a2daf4d11b5161c8213d4dd8cd507405a01f6aaceec9a28.
No blocker found: the resolver threads through native sizing and initialization,
carried field/alias links take precedence, and frozen name lookup neither retains
callback state nor falls back to the catalog. Three test cases cover metadata and
text lifetime, alias distinction/missing/cyclic resolution, and conflicting names.
Runtime execution remains pending; this is not permission to import its live source.

The query owner also identified a future integration requirement: replace the
frozen QueryImpure name-based Native.effects/helper scan with exact rich callee
resolution and supplied known effect masks/BodyContext. Preserve the existing
query header purity policy and unknown-versus-known-zero distinction. Its future
regressions need qualified/shadowed native and source-owned helper cases. This
is an owner-reported audit requirement; no query source changed here.

Collection native follow-up: owner reports f90c types27/4861 passed and resolver
cases3 passed, with2 older record harness failures. Corrected source97c1656f3c42a42ad3b349ccd7cb0768f940c1e8
awaits rerun. Root reviewed native-harness-and-union-cost.patch source only:
fixture heap/capture lifetimes and MAKE/KEEP cost/provenance match current
VALUE-copy paths, retaining conservative pointer/view alias tracking. No blocker
found. No rerun or combined compatibility proof was produced here.
