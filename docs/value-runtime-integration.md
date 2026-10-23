# Value and query runtime integration

Coordination draft, 2026-10-22. This is a source audit and integration order, not
validation of the collection or query implementations.

## Checkpoints and ownership

The clean managed-pointer dependency is `f04d9d37822edb44a8fb1d1f6f68e5546eb8e049`
(prepared ABI 21). Its successor, `codex/value-semantics-20261022`, merged main
at `1bc97b7305ee91102d3ee035e4852aa5341df31c` into the local source checkpoint
`60a407503d287316945f9a80457d051ff1781233`. That integration contains no collection
or query draft source. The ABI 20 compiler/runtime publication work remains a
separate candidate.

The collection owner works in `.worktrees/value-array-cow/chibi`; the query owner
works in `.worktrees/query-iterators/chibi`. Both drafts currently name prepared
ABI 22, but their layouts and codecs differ. That number does not establish
compatibility. The collection chat has a developer restriction against tests,
commits, pushes and merges. Preserve that restriction; validation of the common
dependency is not evidence for the collection draft.

## Contracts to reconcile

| Contract | Draft differences | Joint review responsibility |
| --- | --- | --- |
| Value layouts | Query adds `PIN_QUERY` and `pin_value.schema`; collections add view containers, string ownership, and array/map backing fields. | A single types/ABI owner defines layouts and target metadata; both feature owners review consumers. |
| Schema lifetime | Query uses `schema.c` plus value/array schema pointers. Collections retain native/frozen layout closures through records. | A single schema owner reconciles metadata lifetime, field resolution, copies and tracing. |
| Cleanup and accounting | Query uses arena finalizers and external-byte resizing. Collections use cleanup attachments and shared-resource accounting. | A single arena/GC owner defines finalization and accounting; each feature supplies its release cases. |
| Pointer identity and writes | Query adds component-cell identity. Collections add pointer-backed views and projections into current logical locations. | A shared execution owner reconciles identity, checked projection and writes. |
| Value copies and captures | Query captures values through snapshot paths; collections add value-copy/drop and backing ownership rules. | Collection owner defines the copy contract; query owner audits every capture and retained result. |
| Prepared and compiler metadata | Both alter wire ceilings, target descriptions and compiler intrinsics. | Integration owner produces one combined target, reader/writer contract and compiler artifact. |

Files that cross these boundaries include `src/types/{value,array,map,record}.h`,
`src/types/{type,trace,value_wire}.c`, `src/support/arena.{c,h}`,
`src/execution/{intrinsics,function,call,program_tasks,prepared,prepared_record}.c`,
`src/toolchain/compiler_target.c`, and the Chibi prepared writer/package compiler.
Query-specific consumers include `src/execution/query.c` and `src/ecs/ecs.c`.

The owners agreed on these draft intrinsic reservations:

| IDs | Reservation |
| --- | --- |
| 46–50 | Existing managed pointers |
| 51–61 | Collections, including value copy/drop, mutation of a logical location, and union projection; 54 reserved |
| 62–79 | Unallocated |
| 80–95 | Queries; currently 80–88 assigned |

Reservations prevent collisions; they are not a frozen executable contract.
Do not publish an artifact from one draft as compatible with the other.

## Agreed source integration contract

The collection and query owners reviewed the following consolidation on
2026-10-22. This agreement does not claim a combined implementation or artifact.

- Use collection's immutable native/frozen `pin_record` as the sole owned layout
  carrier. Adapt query's parallel `pin_value.schema`, `pin_array.schema` and
  `schema.c` consumers. Add a temporary resolver argument to both native closure
  sizing and initialization. Freeze exact offsets, sizes, text-descriptor flags,
  aliases, names, field metadata and nested links before that resolver expires.
  Conflicting carried and named identity must fail instead of consulting a newer
  catalog. Preserve full carried-closure comparison: a layout hash alone does not
  completely identify a root alias target. Native/frozen metadata remains
  distinct from authored wire records.
- Use `arena_set_cleanup`/`arena_replace_cleanup` with a retained resource as
  callback data. A component cell and its completed layout have a fixed charged
  size. Remove query's arena owner vector and storage retain/release accounting;
  attach each acquired cell reference directly, deduplicating its bytes per
  arena. World ownership is separate. Cleanup releases the cell or module itself
  and must not dereference sibling arena allocations. Remove retirement-only
  cursor finalizers while preserving explicit cursor end/error state.
- Replace direct `root->value` view access with one checked logical root/path
  resolver for managed values and original-incarnation ECS cells. Reads, bounds,
  projections and nested writes share this resolution. Preserve checked engine
  publication, expected type/layout and world checks, fixed numeric view indices
  and current-pointee replacement behavior. Removal never revives on re-add.
- Preserve logical collection copies at argument evaluation, captures, row
  values and results. Reads borrow; pointers and views retain explicit identity.
  Retained native call banks survive suspension, and owned results transfer
  before child cleanup. Keep trusted `language_values` and `owned_result`
  capability contracts distinct from ordinary foreign callbacks.
- Reconcile native and Chibi prepared layouts together. Preserve host-call
  `owned_arguments` after `result_count` and before `outcome_count`, and invocation
  `owned_arguments` after `task`. Append `PIN_QUERY` without renumbering existing
  tags. Recompute one target fingerprint over the complete combined structures,
  component-root pointer shape, capabilities and intrinsic registry. Collection
  source currently expects bootstrap version 7/minimum 6; its included artifact
  is still prerequisite ABI 21/version 6/minimum 2. Neither artifact is evidence
  for the combined source.

The pointer owner coordinates this source contract and later common artifact.
The collection candidate remains isolated under its existing validation hold.
The subsequent-main checkpoint supplied by the coordinator is
`5c28191b3ea53afa232c34e37b14fd9b01024aef`; it has not been merged here. Preserve
the proven common checkpoint `22dc0d935b1478b519801597f16d547974d30c67` separately
while preparing that later integration.

## Semantic holds

Ordinary collection assignment and arguments are independent values; explicit
pointers share targets. A copied collection of pointers keeps independent slots
and shared pointees. Pointer-backed mutable views follow the collection currently
stored at their pointer, at fixed numeric indices. See [value semantics](value-semantics.md).

Checked direct mutation of external locations is approved. On 2026-10-22 the
owner also approved retained logical component handles across suspension and
storage movement. Removing the original component, entity or world permanently
invalidates the handle; subsequent access produces a recoverable invalid-target
error. Re-adding a component never revives or redirects an old handle. This
supersedes the query prototype's detached-final-value behavior. The query owner
owns that implementation. Concrete error representation and the public recovery
API must be documented before claiming complete recovery support. The owner
ruled out `try`/`catch` permanently; authored recovery must use typed values/results.

Subsequent approval: bind a task to the original target before its first
instruction. Removal cancels that task and its children
at safe boundaries, delivering typed `Cancelled` to surviving independent awaiters.
Completed successful results remain successful after later target removal.
Children inherit retained dependencies so they stay protected after their
parent completes. Pointer ownership does not imply dependency. Delayed actions
use task sequencing; `destroy_after()`
was rejected. See [the lifetime contract](value-semantics.md).

The owner later rejected requiring a call-site modifier and requested inference.
The earlier explicit-call-site rule is superseded. Subsequent discussion
rejected late cancellation after unrelated post-await effects: a component that
may still be accessed, including conditionally, must remain a dependency so
removal cancels before authored continuation. Do not bind every passively stored
pointer or expose the rejected `on health` spelling. The owner then approved
retaining an inferred dependency until task completion, including after the
last access: target removal cancels pending independent effects in that same
task. Compiler liveness optimizations must not release it early. The inference
producer and handling of targets selected later remain separate from the
private runtime target list.

The query owner owns the separate task-lifetime runtime increment in
`program_tasks.c`, `program_internal.h`, `program_activation.c`, `program.c`,
the narrow ECS cell retain/liveness helpers, and existing C test dispatchers.
The pointer owner owns authored dependency inference and documentation. The first
runtime implementation uses retained-cell liveness checks before task start,
pending-request result consumption and continuations, plus pending-task sweeps even
when a request never completes. Cell invalidation does not execute tasks or
mutate the world inline. Existing task cancellation handles child teardown and
typed waiter results. This interface is approved; implementation evidence is
still pending.

## Integration order and evidence

1. Finish and validate the common pointer/main dependency. Record the exact
   source commit, artifact hash and checks actually run.
2. Agree on one schema representation and value layout, then one arena cleanup
   and accounting protocol. Do not combine competing ownership carriers by
   simply retaining both fields.
3. Reconcile pointer identity, projections and checked writes. Preserve engine
   write validation/publication and the selected invalidation policy.
4. Audit query arguments, results, captures and cleanup against value-copy/drop
   semantics. Audit collection projections for external-origin propagation.
5. Reconcile the executable target and prepared codec, then both compiler stages
   and all execution backends. Produce a combined artifact only after those
   sources agree.
6. Run checks appropriate to the combined change under each owning chat's active
   instructions. Report source-only, built, tested, committed, pushed and merged
   states separately. A runtime seed publication is a separate coordinated step.

The integrated checks must distinguish independent copies from pointer sharing,
cover nested collections and query captures, retain schema metadata through
owner replacement, release cycles and external resources, and exercise selected
component invalidation behavior across suspension. Cache optimization needs
separate evidence for every relocation path; membership or publication versions
alone do not establish pointer address stability.

## Common dependency verification, 2026-10-22

The default Release graph build completed before the final integration fixes.
Its compiler recovery exposed repeated boxed-array boundary scans, recorded in
[language friction](language-friction.md). That performance path is already
owned by the collection work and was not changed here.

Focused checks found an inherited pointer-checkpoint regression: stage one
routed `self.Component.field` assignments into the local/pointer assignment
handler before recognizing component targets. Classifying component targets
first preserves their existing checked write path. The existing Counter,
Glide and TextState cases failed before this fix. Main's inferred-numeric-local
fixture also needed the currently required explicit `ref` call marker, pending
the separately planned removal of that syntax.

The test driver embeds compiler sources. An initial rerun reused old embedded
bytes and is not evidence for the source fix. The CMake fixture's module list
was corrected to include `Pointers.chibi`, then the Release CMake driver and
compiler host were rebuilt. All nine embedded compiler modules were compared
byte-for-byte with the source files before the successful checks below.

These commands used `CHIBI_LANGUAGE_BUILD_DIR=build-release-cmake/test/language`
and `python3 tools/test_policy.py tool SELECTOR`:

| Selector | Cases passed | Assertions |
| --- | ---: | ---: |
| `language-package-compiler-chibi` | 1 | 11,979 |
| `language-package-compiler-chibi-behaviors` | 1 | 11,812 |
| `language-package-compiler-chibi-behaviors-rest` | 2 | 19,732 |
| `language-package-compiler-chibi-collections` | 1 | 3,882 |
| `language-package-compiler-chibi-collections-compiled` | 1 | 3,652 |
| `language-record-prepared` | 8 | 1,142 |

The corrected-source bootstrap proof passed one case and 17,478 assertions,
using `CHIBI_RELEASE_LANGUAGE_BUILD_DIR=build-release-cmake/test/language` and
`language-package-compiler-chibi-bootstrap`. It produced a 13,422,214-byte seed
and checked byte-identical self-reproduction, target identity, package samples
and literal results. The matching artifact refresh completed through the Release
CMake compiler host, using that seed and source revision
`60a407503d287316945f9a80457d051ff1781233`. The host verified the seed, source and
target identity and byte-identical self-reproduction before writing the tracked
artifact and manifest. The resulting artifact is 13,422,214 bytes with SHA-256
`3fb0d760a6bafa428b46beb42bfc8759b9d65e6e470f670443fa75c868fc3fe2`.
Its manifest uses version 6, minimum 2; the runtime prepared ABI remains 21.
All 19 manifest source hashes and lengths, and the artifact hash and length,
match the files in this checkout. The earlier bootstrap run was stopped when
the source fix became necessary; it is not a passing result.

Formatting of the edited C fixtures and `git diff --check` passed.
Debug/sanitizer and Wasm checks, and the broad native, materials, projects,
full-language and tools groups, have not run for this integration. No collection
or query draft is covered by these results.
