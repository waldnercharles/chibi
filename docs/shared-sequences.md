# Shared sequences

> Superseded draft: on 2026-10-22 the owner revised ordinary arrays, maps and
> builders to independent value semantics with copy-on-write. The source in this
> worktree implements the earlier shared-array model and is preserved for reuse,
> not publication. See [the revised plan](value-sequences.md). Mutable-view
> semantics are awaiting a separate decision.

## Status and scope

The owner approved the shared dynamic-array contract below on 2026-10-22.
Implementation is in progress on `codex/shared-array-objects-20261022`, based on
main `329d8fb04`. The earlier string-storage work is preserved in
`codex/shared-sequence-slices-20261021`; its pending integration is not a source
of current-main build or validation evidence.

PR #633's length-carrying string cutover is merged into this baseline. This
work adds copied range slices, explicit retained views and shared
dynamic-array objects. Managed
pointers are a dependency owned by chat
`01a15cd2-7126-7cc0-bc72-da5791aae3d5`, branch
`codex/managed-pointers-20261022`. Integrate its clean checkpoint rather than
copying an in-progress managed heap into this branch. Clean pointer checkpoint
`f04d9d378` is now integrated locally, including its ABI21 bootstrap artifact;
this feature advances prepared ABI to22 and must regenerate that artifact.

## Approved contract

- Dynamic-array assignment copies a reference to the same array object,
  including its length. Appending or writing an element through an alias
  changes that object.
- `a[start..<end]` and `a.slice(start..<end)` copy the selected elements into
  a new outer array. This supersedes the earlier shared ordinary-slice proposal.
- `a.view(start..<end)` retains the array object and a fixed numeric offset and
  length. Relocating the element buffer preserves every alias and explicit view.
  Views have a distinct collection kind; they support element writes and fixed
  range operations, while structural operations belong to the dynamic array.
- `clone()` allocates independent outer storage. Each element follows its
  normal assignment rule: managed pointer targets and nested dynamic-array
  objects remain shared. Recursive graph cloning is a separate operation.
- Reassigning an array variable rebinds that variable. Existing aliases and
  views still refer to the original object. Explicit call-site `ref` permits
  changing the caller's variable; shared object mutation does not require it.
- Fixed arrays have value semantics. Adding fixed-array syntax is separate
  from implementing sharing for the current growable `T[]` type.
- Strings remain immutable byte sequences. Assignment may share immutable
  backing; ordinary slices and clones copy the selected bytes, while explicit
  views retain backing. Unicode scalar decoding remains an explicit operation.
- Use Chibi's existing `..<` half-open range spelling. The previously approved
  strict range accepts `0 <= start <= end <= length`, including empty ranges;
  invalid bounds report recoverable errors. Strict range copy/view construction
  returns the existing boxed optional representation: nil on invalid bounds,
  a present value on success, including a present empty slice. Existing clamped
  start/count slicing remains the explicitly approved compatibility operation.

## View edits and API compatibility

The local implementation keeps a view's numeric offset and length, checking
that its complete range fits the current array before use. A view whose range
no longer fits reports a bounds error; sufficient subsequent growth makes it
usable again. The owner leaned toward this rule while approving the copy/view
split. It does not track individual element identities.

Review the existing difference between mutating array methods and
value-producing native function forms during the compiler audit. Preserve or
migrate each documented operation deliberately. Shared assignment alone does
not identify every native operation that should mutate its argument.

## Storage and lifetime design

Use one canonical `pin_array` object for each dynamic array. Keep count,
capacity, element layout and current buffer on that object. Views resolve their
offset through the object on access; they never cache an interior pointer to
a buffer that growth can replace. Central accessors must replace direct field
loads in native and Chibi compiler lowering, reference iteration, prepared
loading and exported execution paths.

Allocate mutable array objects and views in the managed-pointer heap family.
Trace view to owner, owner to current buffer and retained element metadata,
then initialized elements to their managed values. Preserve packed pointer and
managed-record tracing. Clear removed managed slots; unused capacity is not
initialized data. Imported scopes and committed hot replacements use the same
owner family. Unrelated execution owners must reject unsupported transfers.

Reference-counted string backing contains bytes only and can use the existing
arena-cleanup foundation. Generic array backing can form pointer/collection
cycles and must participate in tracing rather than relying solely on external
reference counts. Address-taking of array/map entries remains outside this
increment.

Ordinary copies retain object identity across locals, arguments, returns,
record fields, union payloads, task/state storage and host roots. Keep explicit
detached copying available for `clone()` and engine-internal historical
captures. Observer/binding caches must compare against earlier contents;
retaining the same mutable object is not a valid historical baseline. Root
walks need identity-aware traversal once collection graphs can be shared or
cyclic.

## Implementation sequence

1. Port the prior immutable string-owner and arena-cleanup source onto current
   main. Preserve current source changes and generated descriptor initialization.
   Source port complete locally, including authored string-field writes and
   failed-write cleanup. The combined native build passes; runtime validation
   remains pending.
2. Integrate the managed-pointer checkpoint. Review root registration, heap
   family checks, tracing, host boundaries and hot replacement together.
3. Introduce array-owner/view accessors and typed tracing. Separate ordinary
   identity-preserving copies, copied slices/outer clones, and historical captures.
4. Update native runtime operations and both compilers together. Audit copied
   array arguments and snapshots in the Chibi compiler's own sources so its
   algorithms retain their intended behavior after sharing changes.
5. Add the common strict range surface and clone operations. Preserve element
   metadata for every `T[]`; strings use byte offsets.
6. Update reference docs, examples and language-friction entries. Regenerate
   bootstrap artifacts from the combined compiler sources and review the
   complete diff before publishing any candidate.

Critical implementation files:

- `src/types/array.[ch]`, `src/types/text.[ch]`, `src/types/trace.c` and
  `src/support/arena.[ch]` own representation, copying and lifetime.
- `src/execution/managed.h`, `function.c`, `program_storage.c`,
  `program_bindings.c` and `program_observers.c` own roots and retained history.
- `src/runtime/arrays.c`, `arrays_definition.h` and `text_format.c` own native
  operations. `src/language/` and `tools/compiler/package_compiler/` must emit
  equivalent ownership and effect behavior.

## Verification plan and evidence

Required cases include alias writes and length changes across calls and stored
fields; view writes after repeated growth; detached outer clone with shared
pointer targets and nested arrays; empty and invalid ranges; source lifetime;
unreachable cycles; independent execution owners; suspension and hot reload;
observer/binding change detection; and native/Chibi compiler parity.

Use the owning C pico-unit dispatchers and the repository's current checks.
Verify compiler self-hosting and bootstrap recovery after both compiler paths
agree. Run checks for affected runtime and engine contracts, and report coverage
separately from build success.

Current local evidence: `make build` completed for `test_language`, `test_tools`
and `chibi-compiler-host` after the combined pointer/sequence source integration
(graph build 131.0 seconds). A final incremental build is still needed after
native callback-result and document-reader follow-ups.
The Chibi graph used its default sanitizer profile; `build-release` is the
directory name, not evidence of a Release build. `git diff --check` passes.
No test executable has been run on this branch. A session developer instruction
requires an explicit user request before adding or running tests; a focused-test
choice is pending. Bootstrap regeneration currently goes through the compiler's
bootstrap check and has therefore not run. The included pointer artifact still
uses ABI21 and cannot be used as this branch's final ABI22 bootstrap.

Both compiler source paths implement copy/view/clone operations, optional ranges
and explicit `view T[]` signatures. Owned record layouts, cycle-aware roots,
default materialization and native persistence captures are implemented in source.
The existing authored-compiler restrictions on nonnumeric state and named
collection results remain; this work adds no new state/result-bank implementation.
No change has been committed or pushed. The pointer prerequisite merge remains
uncommitted in this worktree; its source conflicts are resolved. The complete
pre-integration feature snapshot remains in stash `7a4e9b826` and source backup
patches are under the workspace `.scratch/shared-sequences-20261022` directory.
