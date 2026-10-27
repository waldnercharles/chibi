# Managed pointers

Approved and implemented on 2026-10-22 on `codex/managed-pointers-20261022`,
then finished on `claude/project-thread-7x506x` on 2026-10-24 over the shared
prepared limits of #688. The validation ledger below records what has actually
passed.

The query integration on `codex/query-iterators-20261022` extends this local
implementation with retained component references and owned native schemas.
The earlier validation ledger below records the pointer branch's checks;
query integration needs its own validation evidence.

## Contract

Assignment copies the value of its type. A pointer assignment copies a
reference to the same mutable value. Pointers to ordinary language values keep
their targets alive, including after a function returns. Users do not select shared or unique
ownership, write `move`, or break reference cycles manually.

The separate sequence work owns collection assignment, copy-on-write storage,
mutable views and resizing. This query candidate preserves pointer identity
through collection copies and traces pointer elements; it does not establish
that collection migration. Explicit `ref` still identifies calls that can
rebind the caller's variable in this candidate.

```chibi
actor: Actor = {health: 100}
copy := actor
p := &actor
q := p
copy.health = 50 // actor is unchanged.
q.health = 80   // actor, p and q observe 80.
```

- `^T` is a non-null managed pointer to `T`; `&value` takes its address and
  `pointer^` accesses its target. Field access dereferences automatically.
- `^T?` permits absence. Non-null pointers cannot be initialized implicitly to
  an invalid address. Nullable pointer fields allow recursive records to be
  constructed before their links are connected.
- The pointer constructor binds before postfix array/optional suffixes:
  `^T[]` is an array of pointers; `^(T[])` points to an array value.
- Copying a record copies each field value. A pointer field therefore continues
  to refer to the same target after its containing record is copied.
- Assigning a pointer redirects that pointer variable. Assigning through it
  replaces the target's value while preserving the target's identity.
- Existing `ref` parameters remain invocation aliases. `F(ref p^)` passes the
  managed pointee as a writable argument. Pointer and integer conversions are
  not implicit, and managed pointers have no address arithmetic.
- `&value` supports writable locals, value parameters and fields of flat
  authored records. Ordinary native record locals and authored records with
  inline record fields remain outside this address-taking contract. Query
  bindings provide component references explicitly with `component: ^T`;
  fields reached through an existing managed pointer can be projected without
  borrowing a component address. Addresses of array/map entries and
  existing invocation `ref` parameters are rejected until their identity and
  lifetime contracts are implemented. A pointer to an entire array/map variable
  is a separate case.
- Component references support reflected native layouts, including nested
  native records, strings and narrow enums. A live reference observes native
  component writes and publishes its writes through the component's owning
  write API. Removing the component, destroying its entity or destroying its
  world permanently invalidates that component incarnation. A later add has a
  different identity; references never revive or follow a reused entity slot.
  Reads, writes and field projections on an invalid target return the runtime
  diagnostic `Component reference target is no longer available`. The embedding
  caller receives the existing execution failure and diagnostic; the activation
  stops. Authored code has no catch or checked recovery operation yet. An
  invalidated reference keeps its identity and does not become `nil`.
- Native component values and references retain exact layout metadata,
  including nested schemas, offsets, string representation and write ranges.
  These schemas do not extend the fields ECS components can contain: managed
  pointer, query, task and union fields remain unsupported in component storage.
- Managed pointers are process-local values. Persistence and native/ECS
  boundaries must explicitly support their lifetime or reject them; they must
  never serialize an address as an integer. Native calls reject arguments
  that hold managed pointers, except engine kernels marked `borrows`, such as
  the array operations, which read them only during the call. Program load
  accepts those kernels too. A borrowing call skips the argument walk only
  through its own callback, and its bank values are validated when the
  definition freezes.
- The Chibi-written compiler currently rejects named pointer results and
  arrays of nullable pointers (`^T?[]`). Ordinary pointer results and arrays
  of non-null pointers work. These follow its existing named-result and boxed
  optional-collection limits and need explicit parity work before expansion.
- A member of a dereferenced pointer is a field of its pointee, even when the
  call that returned the pointer, such as `get(nodes, 0).item`, names its
  output the same. Both compilers read the field.
- Unreachable cycles are reclaimed automatically at execution safe points.
  Collection timing is not an observable resource-cleanup guarantee. Files and
  other external resources retain their own close/scope rules.

## Runtime design

Use Chibi's existing precise nonmoving collector in `support/arena.c`.
It already follows allocation tracers and sweeps unreachable cycles. The new
managed heap belongs to the execution owner shared by imports and activations;
an address-taken local becomes a typed cell from its initialization. Pointer
copies preserve cell identity, and field projections retain their root cell.
They must not retain a frame address or an entire invocation.

Component references use retained ECS cells. Each cell resolves its current
row while valid, so storage movement and suspension do not retain a row address.
Removal, entity destruction, world destruction or a failed storage lookup
permanently invalidates the cell. Invalid cells retain their schema metadata
and identity without retaining a final component payload.
The pointer descriptor retains the cell; the collector releases that ownership
through an arena finalizer. The cell's last external release also removes an
unused attached cell, so the world does not keep unreferenced cells alive.
Each cell owns an immutable native schema closure from `types/schema.c`.
The same schema representation travels with native value snapshots, allowing
copying and tracing to resolve nested types independently of the catalog.
Retained cell metadata contributes to the owning arena's payload accounting and
collection admission. Invalidating a cell does not copy its component value.
A cell shared across owning arenas contributes once to each owner's reported payload.

Collection must trace all live roots: persistent values, current/suspended
activations, action arguments, task results, cached values and queued work.
Standalone host execution needs an explicit heap/root lifetime contract.
Host root registration snapshots value metadata and preserves pointer identity.
Registered slots have stable addresses; replacing their contents requires
registering the slot again. Separate execution owners reject pointer transfers
before changing state or queuing events. Imports and committed replacements
belong to one heap family. Destroying a heap clears its registered host slots.
Collection while synchronous native/JIT code is running is deferred until
there are accurate spilled roots; ordinary return/update safe points already
exist. Tests must prove collection after roots disappear, not just cleanup at
program destruction.

The existing task-box holder counts are not a general object-graph ownership
scheme. Do not reuse them to make cycles appear supported. Likewise, do not
allocate managed targets in resettable frame storage.

## Implementation

1. Append a distinct `PIN_POINTER` runtime value kind. Add pointer-sized
   layouts, identity-preserving snapshots, allocation tracing, and explicit
   persistence handling in `src/types/` and the execution/prepared ABI.
2. Add managed make/read/write/field/reference operations in
   `src/execution/`, backed by typed cells and the owner heap. Connect complete
   root traversal and safe-point collection.
3. Add pointer grammar, formatting and signature reading. Extend native type
   identity, semantic analysis and lowering under `src/language/`. Permit
   recursive record layouts through pointer fields only.
4. Implement the same language behavior in the Chibi-written compiler under
   `tools/compiler/package_compiler/`. Share the runtime intrinsic ABI, preserve
   the full pointee type, and keep ordinary value-copy rules distinct from
   physical pointer storage.
5. Integrate examples, diagnostics and bounded tests. Recheck the explicit-ref
   prerequisite against this newer main; its previous results belong to the
   older checkout.

Nim ORC was examined as prior art. It depends on reference-counted headers,
compiler-generated destructors and reference updates throughout the program.
Reusing Chibi's existing tracing collector avoids adding that second ownership
system. Reference: [Nim ORC source at the inspected revision](https://github.com/nim-lang/Nim/blob/2b9e94433dd9c3e8e138f8bc6fb52060d54c436a/lib/system/orc.nim).

## Known limitations

The independent runtime review of #695 left these for follow-up work:

- **Pointee layouts across hot reload.** Matching preserved state compares the
  pointer header, not the pointee's layout. If a replacement changes a record
  that preserved cells hold (for example, a field added to `Actor` while
  `saved: ^Actor` is kept), those cells keep the old layout. Reads through them
  then fail with "Reference read differs from its declared result" until a
  restart. Nothing reads freed or misaligned memory. The fix is to compare the
  layouts reachable through pointers when state is matched.
- **Retired heaps.** Each committed replacement keeps the previous heap in the
  family until the program is destroyed, even once it holds no live cell.
  Collection and ownership checks grow linearly with the number of reloads.
- **Elements through a pointer to an array.** `p^[i]` reads and `p^[i] = v`
  writes copy the whole cell value, and an address-taken array local leaves the
  in-place array path. Loops over large `^(T[])` targets are quadratic until
  writes go into the cell in place and reads copy only the item.

## Verification ledger

Passed on macOS arm64 in this checkout, using the existing pico-unit dispatchers:

- Release and sanitizer builds of the affected language driver and compiler
  host, plus the tools driver used below.
- Syntax (23 cases), formatting (59), source editing (20), semantic analysis
  (65), borrowing (27), execution (36) and program lifecycle (167).
- Source tokenization, signature reading, type construction, named binding,
  record/union members, prepared writing and scalar lowering in the
  Chibi-written compiler. Record preparation, prototype documents, record
  services and package imports also pass.
- Package compiler scalar, type, import and collection checks. The collection
  check compares native and Chibi compiler source points/watches and executes
  their results on the VM and JIT. Its 12 pointer calls cover aliases, returned
  locals, fields, record/array copies, distinct allocations, nullable pointers,
  value parameters, cycles, grouped types, widening and whole-array targets.
  Compiling that sample through the JIT also passes under sanitizers.
- The compiler's argument-sharing check passes without new exemptions. A
  union-construction regression verifies independent ordinary array payloads
  and retained managed-pointer identity. Pointer helpers use fresh results or
  local bindings to avoid copying every compiler argument at hot call sites.
- Rooted and unreachable cycles, suspended activations, task results, imports,
  registered host roots, queued events and replacement retention; cross-owner
  rejection and persistence refusal. Execution, program lifecycle, semantics,
  borrowing, types, imports and record services pass under sanitizers.
- Independent read-only reviews of the compiler/ref integration and runtime
  ownership, native boundaries, prepared ABI and replacement failure paths.
- Eight prepared-record cases pass under sanitizers, including repeated-header
  work accounting and rejection under an insufficient budget. The compiler
  host's layout-work limit is unchanged. Native-boundary cases reject malformed
  aggregate storage before callbacks and accept supported narrow enum layouts.
- Self-hosting bootstrap passes: stage two reproduces the compiler byte for
  byte and agrees on the sample programs and floating-point literals (16,775
  assertions; 305.6 seconds in this run).

- Artifact generation and isolated recovery pass. Recovery ran with only the
  runtime executable, compiler sources and core packages, with an empty
  environment and unavailable external compiler commands. It reproduced all
  13,104,075 artifact bytes and the recorded SHA-256.

Passed on Linux x64 (clang 18, ASan/UBSan) at the review-round head of #695,
which supersedes the macOS artifact figures above:

- `make test-build`, `test-native`, `test-materials`, `test-projects`,
  `test-language-all`, `test-tools`, `check-format` and `package-sdk`.
- Package compiler collections, including compound writes that read their
  target once before the value (`p^ += Set(p)`, an addressed local, and
  fields through a pointer and an addressed record), plain-write widening and
  the same-type rule for compound writes.
- A program that returns an `append`-built pointer array, holds it across a
  forced safe-point collection and writes through it; a rebound borrowing
  callback that fails before it runs; malformed borrowing arguments rejected
  at definition freeze.
- `make bootstrap-artifact`, the `compiler-bootstrap` checks and
  `make test-recovery`. Isolated recovery from `git archive HEAD` sources with
  an empty environment reproduced all 14,224,585 artifact bytes and SHA-256
  `faf6bf0a28025ede5782350022ba9c12807697244f6a583813f88d0e405ef4e5`.

Independent source review of the runtime contracts on d650e456e covered three
areas: cell and projection construction and mutation, heap adoption and
teardown, and VM/JIT frames, roots and references. It fixed six defects, each
with a case:

- Entity event payloads refuse pointers before queuing (`record_services`).
- A staged replacement roots each preserved pointer rather than the state
  value holding it, so state that keeps a pointer beside a native record
  reloads (`program_replacement_preserves_pointers_beside_inline_records`).
- Build entries and site generators run with their own managed heap
  (`private_programs_allocate_managed_pointers`, run by the build-process
  group).
- No collection runs while a reducing system is paused, because the paused
  activations are listed by no collection
  (`program_paused_reducing_systems_keep_pointer_targets`).
- Packed arrays trace items only for authored records, whose layout travels
  with the array (`types_collection_traces_packed_arrays_of_unknown_native_types`).
- Unrooting clears the host slot (`execution`).

JIT pointer equality now stores its `bool` result as one byte, as string
equality does. Every reader already loaded one byte, so no case can fail
before this change. A suspended union read from state was reported as unrooted,
but a local owns a copy of the union it reads;
`program_suspended_union_reads_keep_pointer_targets` pins that.

Prepared ABI is 23; bootstrap capability version is 6,
while minimum remains 2 because the compiler sources do not use pointer syntax.
Local test logs and diagnostic scratch remain in `.chibi/managed-pointers/`;
the temporary seed and isolated recovery copies were removed after validation.

Use the existing C pico-unit dispatchers and checks appropriate to each changed
contract. The owner revoked the global test-duration ceiling on 2026-10-22;
the compiler cutover chat owns its repository-wide removal. Build timing is
separate from test timing. Renderer, Studio, web and cross-platform recovery
coverage are outside these local results.
