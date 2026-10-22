# Value and query runtime integration

Coordination draft, 2026-10-22. This is a source audit and integration order, not
validation of the collection or query implementations.

## Checkpoints and ownership

The clean managed-pointer dependency is `f04d9d37822edb44a8fb1d1f6f68e5546eb8e049`
(prepared ABI 21). Its successor, `codex/value-semantics-20261022`, is integrating
main at `1bc97b7305ee91102d3ee035e4852aa5341df31c`. That integration contains no
collection or query draft source. The ABI 20 compiler/runtime publication work
remains a separate candidate.

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
| 51–60 | Collections, including value copy/drop, mutation of a logical location, and union projection |
| 61–79 | Unallocated |
| 80–95 | Queries; currently 80–88 assigned |

Reservations prevent collisions; they are not a frozen executable contract.
Do not publish an artifact from one draft as compatible with the other.

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

The corrected-source bootstrap proof and matching artifact refresh are still
pending. The earlier bootstrap run was stopped when the source fix became
necessary; it is not a passing result. Formatting of the edited C fixtures and
`git diff --check` passed. Debug/sanitizer and Wasm checks, and the broad native,
materials, projects, full-language and tools groups, have not run for this
integration. No collection or query draft is covered by these results.
