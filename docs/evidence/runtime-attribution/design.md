# Runtime copy and backend attribution prerequisite

Status: source investigation and proposed admission only, 2026-10-15. No runtime,
product, test, build, policy or authored-source changes; no test/build execution.
Revised after independent runtime-contract review of design SHA-256
`9952dc9bf9e6a85f87ded7ba65adf7816f45b8de3089875dae9fa0adf8d1ff88`.
Implementation and first measurement still require independent scope/design/admission
review. This is a narrow B04/L14 prerequisite, not completion of either package.

## Exact evidence boundary

- Engine: `waldnercharles/chibi`, clean main
  `f1bfe133afbf214de679ca3da9a12c08b6511788` (merged PR405).
- Later locally available main `dd0b2048116dc8f1ecc17603b19ac8a3c4bd84ce`
  differs from the audit base only in ten documentation paths. Runtime, test,
  supervisor and repository-instruction source provenance remains the base above;
  this is a local Git comparison, not a fresh remote-status assertion.
- Studio source only: `waldnercharles/chibi-studio`,
  `78cb4d4c47242db9f7af2b9573653d9756050e99`.
- Original plan: `docs/studio-migration-roadmap/source/plan.md`, Git blob
  `ca0e0a5341e88d120bd22174271bd66b45c6d848`; sections 6.1D, 6.4, 7.1–7.3.
- Package table: `docs/studio-migration-roadmap/packages.tsv`, Git blob
  `1ab347dad54e0ac2bc548430da4de9aab3a03f25`; B04 and L14.
- Test supervisor: `tools/test_policy.py`, Git blob
  `5e0dcd98bb544c493fb959103d22342139505c76`; unchanged hard 60 seconds.
- `sources.tsv` records exact Git blobs and SHA-256 hashes of inspected contracts.
  Line references below refer to this engine commit. Permanent source links use
  `https://github.com/waldnercharles/chibi/blob/f1bfe133afbf214de679ca3da9a12c08b6511788/`.

The original installed native SDK, SDK checksum/toolchain/build oracle, runnable
native Studio baseline, browser Studio and GPU baselines remain unavailable.
Existing native/serial-Wasm language dispatcher machinery is sufficient for a
future bounded C fixture, not a substitute for the plan's three-way Studio
comparison. Build fresh candidate artifacts; neighboring workers' binaries do
not establish this candidate's provenance.

PR400's capture reclamation is merged and must not be reimplemented. Checked
writes and operation descriptors are owned elsewhere. Do not import those
branches, alter their opcodes/contracts, or change record/layout/callback syntax.
Native DLL hotreload is already removed; preserve authored publication/state,
callback, cancellation and generation safety. Ordinary authors manage values,
not arenas, allocation regions or explicit frees.

## Findings and gap map

### Existing counters have deliberately different meanings

1. `support/xalloc.h:15–24`, `xalloc.c:8–15,27–74`: process-wide relaxed-atomic
   allocation-wrapper calls and cumulative requested sizes. Realloc counts the
   full newly requested size, not a net increase. Zero requests are normalized
   by the wrapper. There is no free/live/peak accounting. Direct library
   allocators are outside this measure; two separately read atomics are not one
   globally consistent concurrent snapshot. Leave this contract unchanged.
2. `support/arena.h:34–64`, `arena.c:27–40,67–109,246–318`: thread-local backing
   allocation and collection-mark counters; ordinary bump payloads, collectable
   allocation sizes, capacity and backing/header storage are distinct.
   `arena_usage` does not prove all counted bytes are reachable semantic values;
   sweep occurs at safe points and after a threshold. `arena_storage_bytes`
   includes capacity plus allocator-owned headers, not malloc implementation
   overhead. Summing an arena's capacity and backing bytes double-counts it.
3. `execution/module.h:37–74`, `program_storage.c:118–198`: program memory usage
   includes task result arena backing, logical instance/activation/state-bank
   sizes, activation payload and idle cache, query/reduction/worker measures.
   World change-tracking storage is shared; binding-key storage can be shared.
   These are not disjoint all-heap totals. Do not reinterpret existing fields.
4. `module.h:155–170`, `program_activation.c:371–398`,
   `runtime/behaviors/source.h:175–197`: observer CPU is inclusive process CPU;
   nested samples overlap. Profile rows retain modules. Counters proposed here
   must not install that observer, turn on inspection or modify fast-path gates.
5. `module.c:40–103`, `module.h:30–35`: module canonical storage deliberately
   excludes imported generations, backend code/cache and live instance banks.
   No proposed result may relabel it as total compiled or retained memory.

### Ownership paths that need attribution, not speculative fixes

| Area | Source-backed ownership | Existing evidence and missing information |
| --- | --- | --- |
| World field strings | `ecs.c:1729–1757` normalizes null to empty, deduplicates by text, grows an open-addressed pointer vector, and `xstrdup`s each new string. `ecs.c:1025–1028` frees all strings only at world destruction. `ecs.c:1782–1784`, `machine.c:359` and `jit_memory.c:526` reach interning from ordinary field writes/compiled IR. | Repeating identical text can reuse storage. Unique edited text remains world-owned even after a field returns to its starting text. No public intern storage gauge. This is retained storage by design, not a demonstrated leak or proof that all strings remain semantically live. First slice adds read-only gauges only. |
| Task results and roots | `program_tasks.c:100–107,289–346` snapshots successful completion into task storage; delivery's temporary union wrapper borrows result descriptors, then the waiter snapshots independently. `590–650` indexes generation-bearing handles and traces shared/instance/event/activation/captured and nested-result roots, reclaiming unobserved finished tasks. | Existing `program.c` test cases at 3296, 3530, 3565 and 3594 cover retained handles, independent waiter snapshots, last-handle release and large delivery. The last case infers one payload allocation from xalloc requested-byte growth, not actual copied bytes. Reuse task semantics and existing memory fields; add bounded direct copy attribution, not another collector. |
| Activation storage | `program_activation.c:24–42,90–143` releases completed storage or retains one bounded 64-KiB idle activation; `program_storage.c:9–116` traces collectable payload at quiescent thresholds. | Existing payload/capacity/cache counts and `activation_cache.c` already cover reuse and oversized rejection. Live activation storage, host/call buffers and all pending-action storage are not a complete public category census. Synchronous peak can exceed the after-boundary sample. Follow-on only. |
| State arenas | `program.c:180–202` creates owning snapshots and retires previous arenas. `484–530` frees retired storage after active storage references release it; `848,952` free owner/shared state. | `state_bank_bytes` is a fixed bank estimate, not current plus retired arena backing. Shared and imported state require exactly-once ownership accounting. Follow-on only; no first-slice arena census or lifetime-policy changes. |
| Detached capture boxes | `program_tasks.c:149–177` plus merged `intrinsics.c` reference-write transaction; final holder owns/free storage. | PR400 and its checked-in evidence already cover bounded replacement/producer/task lifetime. General program payload usage excludes box storage. No new reclamation work; first-slice copy counters may explain copies already made by its transaction. |
| Old modules/generations | `module.c:115–118,169–180` owns immutable reference-counted modules; imports hold dependencies. `program.c:2779–2822` replacement frees old work after committing new state. `source_profile.c:35,80–90` retains profile module generations until reset; inspector `program_inspection.c:64–68,629` retains watch generations until eviction/free. Authoring collections retain exact dependency generations (`source_collection.c:149–174`). | Retained reference count is not unique generation bytes; module IDs are logical identities and can repeat across revisions. Profiling and inspection are real owners, not proof of a runtime leak. Backend storage is excluded from canonical module sizes. Follow-on needs owner-specific, generation-deduplicated accounting without an all-runtime registry. |
| Wasm generated entries | `wasm_jit.c:19–33,44–52,97–113,281–345` owns immutable code and realm-local installed entries; retired caches are collected at the next call/cleanup on each thread. | Existing stats give successful installations, actual generated executions, failures and live entries; they do not measure JS engine heap. Serial evidence cannot establish threaded reclamation. Preserve existing stats. |
| Owning FFI records | `types/array.h:32–55`, `array.c:150–226,274–333`: ordinary copy borrows struct backing; owning snapshot copies the root record bytes, recursively preserves inline record managed fields and nominal names. `intrinsics.c:421–439` has a second layout-aware owning root copy. `host.h:15–83` synchronous arguments borrow; retained boundaries must own. | `test/tools/types.c:498–524` checks nested record snapshot producer mutation/release. A descriptor copy is not a payload snapshot. First slice counts these selected owning-copy paths; it does not invent arbitrary C FFI or claim all native/serializer/editor copies are counted. |

## Recommended first slice

Implement three narrow facilities, then one fixed C dispatcher selector:

1. Optional, caller-owned, thread-scoped managed-value copy counters.
2. Optional, caller-owned, thread-scoped actual execution-entry counters,
   including the inspected native fallback to bytecode.
3. A read-only per-world intern-table storage query computed on demand.

Keep allocation traffic, copy traffic, currently retained storage and sampled
maximums in separate report sections. Do not add them together. No global
allocation registry, no program/module ownership registry, no callbacks or
extensible event bus, no state/module census and no author-visible syntax.

### Managed copy counter contract

Proposed public header: `types/copy.h`, installed with `Chibi::types`.
A small implementation can live beside `types/array.c`; a private internal
recording declaration is enough for the few existing alternate snapshot sites.
The API owns no values and retains no runtime pointer.

`pin_copy_counts` has unsigned 64-bit counters and an overflow flag:

- `value_header_moves`: executed `pin_value` descriptor assignments in the
  six explicitly selected sites listed below. A scalar array executes selected
  moves per element. This is not a logical value-copy count, ownership-transfer
  count, generated machine-store count or all-runtime assignment census.
- `value_buffer_bytes`: only the actual borrowed task-result wrapper memcpy
  at `program_tasks.c:308`. Per-element assignments already counted above must
  not also increment this. No array-insertion-shift hook is proposed.
- `text_bytes`: exact byte spans copied for managed string/name values and
  registered record string/name fields, including each copied terminating NUL.
- `nominal_bytes`: exact copied type/nominal name spans, including NUL. Distinct
  from text payload; repeated name copies count repeatedly if the code does them.
- `record_bytes`: exact root-record `memcpy` spans, including inline fields,
  padding and pointer slots. Inline nested-record bytes are already inside that
  span and must not be counted again. Separate pointed-to managed text is counted
  when that actual copy occurs.
- `overflow`: any saturated field makes the sample inexact; fail exact-count
  assertions and print the flag rather than silently wrapping.

A checked, saturating sum of `text_bytes`, `nominal_bytes` and `record_bytes`
is the covered payload-copy total; its overflow flag also includes raw overflow.
Descriptor moves/buffer bytes remain separate. These are executed C operation
spans, not a hardware memory-bandwidth metric: compiler elimination, cache
traffic, pointer rewrites, initialization/memset, allocator metadata and
realloc's internal movement are not measured.

Record at the copying operation, not again in each enclosing snapshot caller.
Copies completed before a later validation error remain in the traffic result.
Empty strings copy one NUL, null strings copy zero. Borrowed struct copies have
zero record bytes, but still a descriptor move. A root record copy plus its
nested text copies is intentional, not double-counting the inline record.

The complete selected assignment inventory is fixed below. Count **after** each
listed assignment. Count both actual assignments if delegation executes two
listed sites; do not count an additional event merely at the requesting caller.

| ID | Existing source-level assignment | Exact meaning |
| --- | --- | --- |
| H1 | `types/array.c:88`, `array->items[index] = copy` | Final push destination assignment |
| H2 | `types/array.c:197`, `*dst = *src` | Owning snapshot descriptor assignment |
| H3 | `types/array.c:279`, `*dst = *src` | Heap-copy descriptor assignment |
| H4 | `types/array.c:302`, `*dst = *src` | Arena-copy descriptor assignment |
| H5 | `types/union.c:45`, `*out = *source` | Direct heap union-copy descriptor assignment |
| H6 | `execution/intrinsics.c:432`, `*out = *source` | Layout-aware owning root snapshot assignment |

The selected payload/buffer leaf inventory is equally fixed:

| ID | Operation and classification |
| --- | --- |
| T1 | `array.c:26` arena-string memcpy, text at callers 172, 207 and 314 |
| T2 | `array.c:288` heap string duplication, text bytes including NUL |
| N1 | The same `array.c:26` memcpy, nominal at callers 131, 198 and 233 |
| N2 | `types/index.c:142` owned-set nominal-name memcpy |
| N3 | `intrinsics.c:435` layout-name memcpy |
| R1 | `array.c:225` root-record memcpy |
| R2 | `intrinsics.c:438` layout-aware root-record memcpy |
| B1 | `program_tasks.c:308` borrowed task-result descriptor-buffer memcpy |

A small private category argument on the existing arena-string helper, or an
equally direct caller-specific recording path, separates T1/N1. A single memcpy
belongs to exactly one category. Root/array/element nominal names count again
only when another actual copy is performed. Recursive arrays/unions reach
these existing leaves without caller-side duplicate charging.

`pin_array_insert` is file-private and its only caller is `pin_array_push`, which
passes `index == count`. Its memmove at `array.c:83–87` therefore has a zero span
on every current valid public path. Omit its hook and nonzero test entirely.
Do not introduce insertion APIs, private test seams or collection changes to
exercise nonexistent work. Add such coverage only with a future real caller.

Explicit descriptor exclusions include value/aggregate initialization,
pointer-member rewrites, constructed `pin_union_value` output, result selection
at `program_tasks.c:300`, and `read_reference`'s assignments at
`intrinsics.c:460,470`. All assignments other than H1–H6 are excluded. Borrowed
wrapper B1 does not also produce a header-move count per element.

Refactoring must preserve the written meaning and exact-count fixture formulas.
Move a hook with an equivalent operation. Removing a genuinely redundant
assignment legitimately changes the count and requires updating this inventory
and fixture expectations in the same reviewed patch. Compare reports only with
their coverage/source/fixture hashes; no metric-version registry is needed.

These counters intentionally do not claim all runtime copying. Raw field
stores/loads (`types/type.c`, `intrinsics.c:76,552` and VM/JIT scalar operations),
serializers, document codecs, arbitrary host internals, compiler text copies and
libc realloc moves
remain outside this selected surface. World-intern text copies are separately
attributed from its gauge delta below. Do not print an unqualified `all_copied_bytes`.

### Scope, reset, nested scope, threading and reentry

Use separate small `pin_copy_scope` and `behavior_run_scope` types, each with
its counters, previous-scope pointer and active flag. Each subsystem has a
single TLS active-scope pointer, with direct begin/end functions. There is no
allocation, callback, registry, clock or atomic counter in the recording path.

- Caller owns a zero-initialized scope. Begin asserts the scope is inactive,
  pushes it as the current TLS scope and preserves its counters. End asserts
  this exact scope is current, pops to the previous scope, clears obsolete
  linkage/active state and leaves counts readable. Incorrect nesting/cross-thread
  ending is invalid state. A parent remains active while a child is on top.
- Reset through the subsystem reset function, which asserts the scope is
  inactive and zeros its counts/overflow. Begin/end do not destroy samples.
  Reusing the same inactive scope deliberately accumulates windows.
  This also forbids resetting an active parent hidden beneath a child. Samples
  start from an exact before/after pair or a fresh zero scope.
- Nested scopes are exclusive: only the top scope receives events. A parent
  does not also receive child events. Disjoint completed windows may be added
  with checked saturation when an inclusive result is desired. Reject starting
  the same active scope twice.
- Synchronous same-thread reentry is included in the currently active scope,
  unless the reentrant caller deliberately starts a nested scope. Helpers and
  task resumes actually executed inside the window are included. Work merely
  queued for later is not attributed before it runs.
- TLS scopes do not propagate to worker threads; a scope/counters object may
  be active on only one thread. Worker-owned samples can be merged only after
  joining and with explicit labeling. The non-atomic active flag detects API
  misuse; it is not a thread-ownership lock. Concurrent begin/read/reset and
  sharing an active object are outside the contract. Inactive transfer between
  threads requires caller synchronization. First-slice tests are serial.
- End scopes before destroying storage they belong to. `longjmp`, cancellation
  that bypasses C cleanup, or concurrent reset is not supported. `REQUIRE`
  returns from the test before pico teardown runs (`pico_unit.h:96–100,304–353`).
  The fixture therefore uses static fixture-lifetime scope objects, never active
  pointers to returned stack frames. Teardown pops known nested scopes LIFO in
  both subsystems, restores any saved isolated inspection state, then destroys
  artifacts/owners/arenas and finally clears the type catalog. Setup records
  every owned allocation immediately, before any REQUIRE can return.
- Overflow is checked before each addition and saturates at UINT64_MAX, setting
  `overflow`. Test near-limit seeded inactive scopes so overflow is deterministic
  without enormous workloads. Raw overflow is sticky until inactive reset.
  Derived totals and merged samples use checked additions and propagate flags;
  summing individually exact fields can itself overflow. A delta is inexact if
  either endpoint/window reports saturation; never subtract saturated values
  and label the result exact. Supported 32/64-bit size_t converts without loss.
- All instrumentation-only work is guarded by an active-scope check. Existing
  production string lengths can be reused; any additional heap-copy `strlen`
  needed only for T2 runs only with a scope active. Disabled scopes add dormant
  checks/calls, not extra scans, allocations, locks, clocks or observer toggles.

### Executed backend, generation and context

Proposed additive declarations live with `execution/function.h` and the
implementation beside `execution/runner.c`; the direct backend record helper
is private. Fields are `bytecode_entries`, `sljit_entries`, `wasm_entries`,
`inspected_sljit_entries`, `inspected_wasm_entries`,
`inspection_bytecode_fallbacks`, and `overflow`.

Count an actual interpreter/generated entry call, not requested compilation or
module configuration. Inspected and fallback counts are labeled subsets, not
extra entries to sum. On native, increment the selected SLJIT category directly
before calling the checked/generated entry; the fallback path records fallback
and the actual interpreter entry. On Wasm, record immediately before the loaded
`entry(&machine)` call, after successful installation; keep existing atomic Wasm
stats unchanged and cross-check their execution delta. Failed preparation or
loading has no generated-entry increment. VM recording belongs at its actual
interpreter entry. No raw-pointer-to-function replacement or fake backend.

Coverage is this interpreter/runner/Wasm entry path, including outlined calls
and resumes. Direct generated-C callers and separate direct batch/system entry
paths are explicitly not a whole-runtime entry census. No batch/worker machinery
changes belong in this slice.

Native `runner.c:19–20` can execute bytecode while the module still says SLJIT.
Use a tiny private fixture to hold a fresh artifact's inspection publication in
its existing COMPILING state, request inspection, and assert one VM entry plus
one fallback and zero SLJIT entry. Restore UNCOMPILED afterward and demonstrate
real inspected SLJIT. Move the existing four inspection-state enum values from
`function.c:414–420` into private `artifact.h` with unchanged values so the test
uses namespaced `EXECUTION_INSPECTION_*` constants with the same numeric values,
not generic names, a magic number or a new runtime failure-injection hook.
The control owns a fresh isolated artifact with normal compiled code, no
published inspection code/maps and a real requested inspection condition. Save
and restore its original state on every failure path before artifact teardown. Wasm has a different inspected-entry implementation: assert actual Wasm
entries there and failed/uncompiled entry rejection without inventing an
unsupported Wasm-to-VM fallback.

Counters contain no module, context or generation references, cannot keep a
module alive and cannot collide logical module IDs. Attribution is an explicit
consumer measurement window. The C fixture labels the exact engine commit,
fixture source hash, requested backend, observed actual counts, logical module
ID, retained module generation's caller-assigned sample ordinal, owner and
phase. Hold the module alive while using borrowed metadata. End the window
before replacement and start another labeled window for the new generation.
Do not assign a mixed nested/imported window to one owner/module unless the
consumer has established that boundary; otherwise label it thread/window total.
Builder functions with no module use an explicit not-applicable module label.
No global generation lookup or new durable language identity is introduced.

### Read-only world string gauges

Add `ecs_world_string_usage(const ecs_world *)` returning entries, payload bytes,
table backing bytes and an overflow flag. Compute on demand from existing
`field_strings`/`field_string_count`; no added fields on `ecs_world` and no writes
on the intern hot path. Payload is sum(strlen(entry)+1); table backing is
`sizeof(dyna_hdr) + acap(field_strings)*sizeof(char *)` when present, zero when
absent. Use checked arithmetic. This is retained requested backing, excluding
malloc implementation overhead, other world names, component storage and all
other allocators. Count each world only once, never once per attached program.

Call only while that world is quiescent, with no concurrent intern/create/free.
Return scalars, retain no pointers. No reset operation: these gauges describe
current owned storage. Never query a destroyed world. In this implementation,
text is never deleted before world free, so a same-world payload-byte increase
between quiescent snapshots also equals newly interned text bytes copied in that
interval. This derivation is inexact/unavailable after overflow or across world
lifetimes. Report it separately as `intern_text_copy_bytes`; do not
count the pointer table's rehash traffic as copied text. Gauge traversal is
O(table slots + text bytes) and belongs outside timed execution windows.

Maximum of sampled retained backing is `sampled_retained_max`, not process peak
or a claim that intermediate rehash old+new tables were observed. Current
retained text is not semantic-live text. The restored-field fixture should
explicitly show unique strings remaining until world teardown, not fail because
an unimplemented reclamation policy was assumed.

## Fixed C fixture and admission proposal

Use `test/language/main.c`'s existing dispatcher with one new
`--runtime-attribution` selector and one C file, pico_unit. Reuse current parse,
analysis/lowering, program, arena, private-artifact and Wasm-stat patterns; no
standalone executable, JavaScript assertions, browser, files, sleep, rendering,
network, worker pool or Studio launch. Existing dispatcher source-specific
private include rules are the precedent for the artifact control.

Six finite cases below form the revised proposed envelope. Source-level formulas
are frozen here; an eventual C implementation and its exact invocation/build
manifest still require independent review/admission before execution. No test
or runtime implementation is included in this documents-only revision.

### Case 1: exact copy-site specimens

Eight successful specimens S1–S8 and one partial-failure specimen F1 each run
once with scopes off and once with scopes on: 18 specimen executions. Each
listed operation is observed in its own fresh scope (or an equivalent exact
before/after interval) so differences cannot cancel. Setup, input construction,
cleanup and checking are outside those scopes. No program execution occurs in
this case. A descriptor operation means an invocation named in the table;
constructing borrowed C inputs outside the window is not measured work.

In the formulas H is `value_header_moves`, T/N/R/B are text/nominal/record/buffer
bytes. Omitted categories are zero. `sizeof` uses the executing target, not a
hard-coded native number. Every text size below includes its terminating NUL.
All enum/handle items are scalar, with no hidden container or managed payload.

| Specimen | Exact operations, in order | Expected selected counts per operation |
| --- | --- | --- |
| S1: scalar and push | Heap-copy one unnamed PIN_I32; push that scalar into an empty heap array | H=1; H=2 (H3 then H1). T=N=R=B=0 for both |
| S2: text leaf routes | Heap-copy null text; heap-copy empty text; heap-copy `abc`; arena-copy `abc`; owning-snapshot empty text | H=1 each; T=0,1,4,4,1 respectively; N=R=B=0 |
| S3: array leaf routes | Heap-copy unnamed scalar array of N=3; clone a named handle array directly to arena; arena-copy that named array value; owning-snapshot the named array value | H=4,3,4,4. Heap case N=0. Named cases N=5,5,25. T=R=B=0 |
| S4: union delegation | Direct `pin_union_copy`; heap union via `pin_value_copy`; arena union via `pin_value_copy_to_arena`; direct owning union snapshot | K=2 unnamed scalar children. H=3,4,4,3. N=0,0,2,2. T=R=B=0 |
| S5: borrowed versus owning record | Heap-copy a borrowed ProbeRecord descriptor; owning-snapshot the same value | H=1 each. Borrowed T=N=R=B=0. Owning T=8, N=12, R=sizeof(ProbeRecord), B=0 |
| S6: layout-aware component root | One `EXECUTION_reference_read_call.value` on a whole native ProbeRecord component reference | H=1 at H6; T=8, N=12, R=sizeof(ProbeRecord), B=0 |
| S7: owned-set nominal leaf | First `pin_set_add_owned` of named handle into a null set; repeat the identical handle | First H=1,N=24; repeat all zero. T=R=B=0 |
| S8: record containing a union | Owning-snapshot one ProbeEnvelope record with one union field holding an unnamed integer and `kid` text | H=4: root + field-union descriptor + two child descriptors; T=4,N=14,R=sizeof(ProbeEnvelope),B=0 |
| F1: partial-copy failure | Owning-snapshot BrokenRecord; first field is valid text `ok`, second descriptor has offset beyond the record | H=1,T=3,N=13,R=sizeof(BrokenRecord),B=0; return failure; discard unpublished arena |

This is 22 measured API operations per mode, 44 across off/on, and 46 selected
header assignments per complete enabled S1–F1 pass. Counter-disabled operations
leave the inactive fixture counter objects unchanged; expected results and
ownership assertions are otherwise identical. Any change to the table changes
the proposed workload and must be reviewed before execution.

The named S3 array uses root descriptor name `Elem` (5 bytes), array name `Elem`
(5), and three item descriptor names `Elem` (3×5). Direct/ordinary arena clone
copies only the array name: root/element names stay borrowed. Owning snapshot
copies root + array + each element: 5+5+15=25. The heap scalar-array input is a
separate unnamed 3-item input. This covers both `array.c:131` and `:233`, without
pretending ordinary clone has owning-record semantics.

S4's union value has nominal name `U` (2 bytes), tag 7 and two unnamed scalar
children. Heap union copies borrow the root nominal name; arena/owning union
snapshots copy it. Distinct delegated descriptor assignments both count, while
child payload copies count only once at their actual leaves.

ProbeRecord has a direct string field `label` containing `one` (4 bytes), and
one inline ProbeInner record with a name field containing `two` (4) and one
numeric field. Its descriptor name `ProbeRecord` is 12 bytes. ProbeInner's
inline storage is part of sizeof(ProbeRecord); its descriptor identity is looked
up, not copied, so no extra nominal or root-record bytes. Construct descriptors
with `offsetof` and `sizeof`, not guessed cross-target sizes. S5 producer record
backing and both text allocations are fixture-owned heap objects: mutate then
free those payloads after the owning snapshot and verify snapshot contents.
Do not read the intentionally borrowed descriptor after producer release.

S6 sets up one ECS world, entity and native reflected ProbeRecord component
outside the counter window. Create the whole-component logical reference through
the existing nine-argument `EXECUTION_reference_make_call` with verified world,
entity, component, layout and stride; read it through the existing reference
read intrinsic. This makes `reference_root_layout` non-null without exposing a
new seam or changing reference code. The preparation reference objects and
teardown are outside the window. Verify copied text after changing the source
component, then destroy the world and independent producer text before checking
the owning snapshot again. Count only H6, not `read_reference`'s excluded wrapper
assignments. This case contributes two world constructions across off/on.

S7 uses PIN_HANDLE with nominal name `ProbeHandle` (12 bytes), nonzero handle 1,
and an initially null owned set. Its first insertion copies that name at N2
and again in the item's owning snapshot at N1: 12+12=24. The duplicate creates
no new snapshot and must increment no selected counter.

ProbeEnvelope's nominal name is 14 bytes; its union children are built outside
the measurement window. It has no other managed field. BrokenRecord's nominal
name is 13 bytes; its first text field is in bounds, while the second field's
offset is sizeof(BrokenRecord)+1 and size is 1. The lookup-only project type
catalog accepts the descriptor for this isolated snapshot validation case.
Failure must occur before reading the invalid field. All early traffic remains
counted; arena cleanup does not subtract traffic.

All S1–F1 inputs have at most three array elements, two union children, one
nested inline record, two text fields and 12 payload-text bytes per specimen;
no record has recursive/cyclic types. Producer names/text and every allocated
input/snapshot are tracked in fixture-lifetime cleanup storage.

### Case 2: both scope implementations and saturation

The copy-scope schedule performs exactly ten copy operations: one scalar with
scopes off; one outer scalar; one inner `abc` copy; after inner end one outer
scalar; one scalar through a synchronous C helper with the same current scope;
one scalar after outer end (off); one after reopening outer (accumulation);
one after ending/resetting/reopening outer; and two with inactive H seeded to
UINT64_MAX-1 before begin. Close each scope before resetting it.

Use the same ten-operation schedule for execution scopes with exactly ten
actual BYTECODE entries, replacing the text operation with a scalar function
entry. One builder function has at most eight instructions and a 32-step budget.
The helper is ordinary C fixture nesting, not forbidden host-to-authored graph
reentry. This exercises each independent implementation's nesting, same-thread
nested call, end/reopen accumulation, inactive reset and raw saturation. Parent
counts exclude the child's one event. No threads or assertion-death children.

Pure report arithmetic checks (no copy or execution) exercise a sum of exact
fields UINT64_MAX-2, 2 and 1; merge UINT64_MAX-1 with 2; sticky overflow propagation;
and refusal to label saturated endpoint deltas exact. Both copy payload totals
and backend entry totals use checked arithmetic. Scope fixtures live until
teardown; no failing REQUIRE can leave TLS pointing at a returned local object.

### Case 3: world intern gauges

At most three world constructions, at most two alive, one reflected string-bearing
entity per observed world. For world A, start with `base` (5 bytes), add exactly
32 texts `edit00` through `edit31` (7 bytes each), repeat `edit31` exactly 32 times,
and restore `base` once. World B receives `other` (6 bytes) and remains untouched.
All input strings are <=7 bytes including NUL; the API test buffers remain <=40
bytes including NUL. All observations occur quiescently outside timed work.

After initial insertion, A has one entry and 5 payload bytes. After unique edits,
A has 33 entries and 229 payload bytes. Its pointer table capacities are 16
through entry 12, 32 for entries 13–24 and 64 for entries 25–33; backing equals
sizeof(dyna_hdr)+capacity*sizeof(char *). Repetitions and restoration change
neither entries nor backing and return the original interned pointer. B remains
one entry/6 bytes/capacity16. Table rehashing copies pointers, not text.
Destroy A, create fresh C and assert its empty table while B remains unchanged.
Do not read old A pointers or infer physical allocator release from fresh C.

### Case 4: task ownership and real wrapper copying

One source <=1,024 bytes defines one `task<string>` state, one string output
state, a yielding function returning its input string, a Start event accepting
that string and a Save event awaiting the handle, retaining the result and
clearing the handle. No libraries, records, worker scheduling or new syntax.
Prepare BYTECODE and the platform compiled module once each; create one owner
per program, two programs per module for copy scopes off/on: four programs and
four worlds total. Backend scopes are enabled for this correctness-only case
in both modes, explicitly distinct from the fully disabled timing companion.

Each program runs eight cycles. Each cycle constructs one heap producer payload
of 63 data bytes plus NUL, dispatches Start, mutates/frees the producer payload,
updates once to complete the yielded task, dispatches Save, then makes one
cleanup update. Count all four boundaries: 4×8×4=128 total, each budget <=1,000.
At most one task is live per program; no more than one retained result handle.
Assert exact text after the producer is destroyed, task/result backing while
held, zero result backing after final handle release and identical off/on state.
Save awaits one non-union result, so B1 copies exactly sizeof(pin_value) per
cycle, or eight*sizeof(pin_value) per enabled program. No other selected B1
caller occurs. Count wrapper bytes separately from waiter snapshot leaves.

Every compiled program must report actual compiled entries and zero unexpected
BYTECODE/fallback entries through its correctness window. BYTECODE programs
report only VM entries. This proves the counter-backed execution route for this
fixture; it does not establish all L14 ownership paths or arbitrary leak freedom.

### Case 5: actual backend branches

Two builder functions A/B, <=32 instructions each and <=1,000 budget per entry.
Function A performs four attempts in this exact order: scoped BYTECODE, scoped
normal compiled, scoped inspected compiled, then normal compiled with scopes
entirely off. Use identical scalar inputs and assert identical successful outputs.
Native function B has ordinary compiled code but no inspection code/maps: its
fifth attempt requests inspection while its isolated state is COMPILING and
must count VM1/fallback1/SLJIT0. Restore the original state; the sixth attempt
requests inspection and must enter actual inspected SLJIT. Wasm leaves B
uncompiled: its fifth attempt requests Wasm execution and fails with zero
actual generated entries. No Wasm-to-VM fallback is invented.

Thus case 5 is exactly six native attempts or five Wasm attempts; with case 2's
ten VM entries, the combined bound is 16 native or 15 Wasm attempts. Cross-check
serial Wasm execution deltas in enabled successful windows. Fully disabled A
execution retains semantic checks and unchanged-route guarantees without claiming
its counters observed the call. Saved fixture-lifetime artifact state is restored
on every failure path before either function is destroyed.

### Case 6: bounded overhead and parity

Two tiny immutable precompiled builder workloads, scalar and managed snapshot,
each <=32 instructions, no outlined calls, two backend choices, two scope modes.
One correctness warmup per workload/backend/mode followed by five trials of
128 invocations: 2×2×2×(1+5×128)=5,128 calls including warmups. Budget <=1,000;
managed text <=64 bytes including NUL and array <=8 values. Each generated call
has exactly one runner entry; host snapshot work does not add runner entries.

Fixed order: scalar then managed, BYTECODE then compiled; trial pairs are
OFF/ON, ON/OFF, OFF/ON, ON/OFF, OFF/ON. Warm each mode once before its first
trial. Each warmup temporarily enables backend counting and asserts one actual
expected entry and no unexpected fallback. End/reset warmup scopes before
trials. ON timed trials assert exactly 128 actual expected entries afterward.
OFF timed trials disable both counter scopes; their per-call counters are
unobserved. Their backend is enforced by the same immutable compiled definition
and uninspected context whose route the warmup verified, without any intervening
inspection/publication mutation. Do not claim directly measured OFF per-call
counts. An available compiled entry alone is not the enabled-mode proof.

Reset trial result/checksum/context to identical initial state; each trial owns
fresh output storage and releases it after checking. Prepare bounded host/result
storage outside timing, preserving production paths. Managed outputs retained
within one 128-call trial must have <=512 KiB of arena-used bytes (including
ordinary-arena alignment padding); assert that measured bound and report actual
arena backing separately outside timing. No cross-trial retained
payload. Begin/end diagnostic scopes outside clocked intervals. Only the 128
invocations and checksum accumulation are timed. Compilation, logging, xalloc
before/after snapshots, arena/gauge reads, semantic checks and cleanup stay out.

Report every raw monotonic duration, checksum, xalloc traffic, arena-block delta,
enabled selected-copy counts and actual backend counts. No percent speed/pass
threshold, frame-rate, tail-latency or whole-application inference.

Scopes-off measures dormant-hook code, not absence of instrumentation. A
separately reviewed/admitted control may compile the same candidate/fixture
recording hooks to no-ops and run only case 6. Its test-only definition must reach
the runtime/types object files, not just the test file. Use its own build directory,
compile-command evidence and binary hash. Omit only counter assertions, keep
semantic checks, route availability/unchanged-context checks and the same workload,
and label this control-only rather than ordinary correctness evidence. There is
no new product configuration surface and no overhead result before measurement.

### Hook-to-case proof obligations

| Hook/site | Required exact assertion |
| --- | --- |
| H1 push assignment | S1 push H=2 together with its H3 copy; no buffer bytes |
| H2 owning descriptor | S2 snapshot H=1; S3 owning array H=4; S8/F1 recursion/failure formulas |
| H3 heap descriptor | S1 scalar H=1; S4 delegated heap union H=4 versus direct H=3 |
| H4 arena descriptor | S2 arena text H=1; S3 arena array H=4; S4 arena union H=4 |
| H5 direct heap union | S4 direct union H=3, delegated union H=4 |
| H6 layout-aware descriptor | S6 H=1; wrapper assignments explicitly excluded |
| T1 arena text caller172 | S5/S6 T=8 across two registered fields; F1 T=3 before failure |
| T1 arena text caller207 | S2 owning empty text T=1; S8 union child's text T=4 |
| T1 arena text caller314 | S2 arena `abc` T=4 |
| T2 heap text | S2 null/empty/`abc` T=0/1/4 |
| N1 nominal caller131 | S3 direct and ordinary arena clone N=5 |
| N1 nominal caller198 | S3 owning root/items; S4 owning union N=2; S5/F1 root names |
| N1 nominal caller233 | S3 owning array name contributes 5 within N=25 |
| N2 owned-set nominal | S7 first insert N=24, duplicate zero; one 12-byte copy at each nominal site |
| N3 layout nominal | S6 N=12 |
| R1 record copy | S5/S8/F1 exact sizeof(record), including partial failure |
| R2 layout record copy | S6 exact sizeof(ProbeRecord), with stable layout/stride |
| B1 task wrapper | Case4 K=1: sizeof(pin_value) per Save, 8× per enabled program |
| Interpreter entry | Cases2/5 exact VM counts; cases4/6 expected BYTECODE route |
| Native generated entry/inspected subset | Case5 normal/inspected windows; cases4/6 actual compiled route |
| Native inspected fallback subset | Case5 fresh COMPILING control: VM1/fallback1/SLJIT0 |
| Wasm generated entry/inspected subset | Case5 normal/inspected and uncompiled negative; cases4/6 compiled route |
| Scope disabled/nesting/reset/overflow | Case2 separately for both scopes; case5 fully disabled parity; case6 paired timing |
| World retained gauges and same-world delta | Case3 exact counts/bytes/capacity, repetitions, untouched world and fresh lifetime |

An implementation that omits a listed consumer, records a different operation,
adds a hook, introduces another execution path or changes these bounds must
return for design/admission reconciliation before running. Do not manufacture
coverage through private APIs or expose new author-visible behavior.

First measurements must use the unchanged production supervisor, proposed routes
`tool language-runtime-attribution` and `tool wasm-runtime-attribution` once
reviewed and admitted. Native means real SLJIT plus BYTECODE; serial Wasm means
Node-hosted generated Wasm plus BYTECODE, not browser or pthread coverage.
Configuration/build timing is separate. Build fresh from the exact reviewed
candidate and record compiler, flags, sanitizer state, SDK/source provenance,
OS/architecture and binary hashes. Performance samples use unsanitized Release;
native ownership correctness should also run with available ASan/UBSan, with
LeakSanitizer limitations explicit. No broader suites or reconstructed chunks.
Existing affected-contract groups/selectors and required builds must be selected
by the coordinator under current policy; no missing required check is waived.

## Expected touched boundaries, if implementation is admitted

- `src/types/array.c`, `union.c`, `index.c`; new narrow `types/copy.h` and private
  recording declaration if needed; `sdk/CMakeLists.txt` installs the public header.
- `src/execution/intrinsics.c` only the existing layout-aware snapshot's actual
  descriptor/name/record-copy sites H6/N3/R2; no capture-write/lifetime rewrite. Coordinate any overlap
  with the operation-descriptor owner before editing.
- `src/execution/program_tasks.c` only existing borrowed result-wrapper descriptor
  memcpy attribution; no root/slot/generation/collector changes.
- `src/execution/function.h`, `runner.h`, `runner.c`, `vm.c`, `wasm_jit.c` for scoped
  entry counts; `artifact.h`/`function.c` for unchanged named inspection states.
- `src/ecs/ecs.h` and `ecs.c` for read-only intern-table inspection only.
- `test/language/main.c`, `CMakeLists.txt`, one new fixture; reviewed additions to
  `tools/test_policy.py` routes/groups and `docs/testing.md`, without changing
  the 60-second supervisor/quarantine. No existing selector removed.

All public additions are additive diagnostic C APIs. Do not change existing
`xalloc_stats`, arena, execution_memory_usage, source_profile_row,
execution_profile_sample, behavior_function_context, ecs_execution_view or
artifact layout. No prepared opcode ordinal/schema/serialization/semantic ABI
change, no authoring asset or project-version bump, and no application adapter.
If implementation reveals a needed existing-layout change, stop for fresh review
rather than hide it in this instrumentation slice.

## Follow-ons deliberately not accepted by this slice

- Complete current/retired state arenas, live activation/host/call storage,
  detached boxes and pending events without summing shared storage twice.
- Generation-owner accounting for authoring collections, source staging,
  programs/imports, profile rows, inspectors and backend/realm caches; distinguish
  unique retained generations from reference counts and module IDs.
- Confirm world string lifetime requirements under real editor text/close/reload
  workloads before choosing reclamation. Gauges alone justify no leak diagnosis.
- More task-root, owner-close, cancel, reload and FFI producer-destruction controls
  with exactly named storage owners; current finite task fixture is not all L14.
- Native crossings/batch size and uncounted payload-copy sites, async boundaries,
  UI model/layout/release phases, compiler/reload phases and process memory.
- Frozen original SDK/native Studio, candidate-native Studio and authored Studio
  three-way behavior/performance baseline. B00/B02 and B04 remain open gates;
  no invented percentage budget and no synthetic end-to-end speed claim.
