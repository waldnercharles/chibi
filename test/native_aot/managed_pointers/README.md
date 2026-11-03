# Native managed pointers

Run `sh run.sh NATIVE_COMPILER LLVM_BIN [WORK]` through the shared heavy wrapper.
The authored probe emits the real collector, compiles it with LLVM at O0/O2,
and runs it with poisoned frees and exact live-allocation counts.

The current slice proves explicit stable roots, nonmoving cells, rooted and
unreachable cycles, escaped cells, changed roots, separate owners, bounded
allocation during 100,000 discarded cycles, and iterative traversal of a
10,000-cell chain. Cross-owner marks and unmatched root pops trap.

Standalone desktop programs admit numeric/bool scalar cells, flat authored
record cells with numeric/bool or pointer fields, and arrays of those pointers.
Cells can escape creator returns; array copies
retain cell identity while detaching on entry replacement. A cell descriptor
contains its nonmoving root, byte offset and stable semantic pointee tag.
Reads, writes and scoped borrows validate the tag, owner, scalar storage and
range before accessing the payload. Scoped parameters always borrow the
caller location, including readonly parameters that can alias written ones.
Nullable pointers use the all-zero descriptor for absence; presence tests,
assignment and equality preserve the original pointer type. The ordinary
checker still requires a presence guard before dereferencing a nullable value.

Source tests cover ordinary/native alias behavior, returned cells, CoW,
traversal snapshots, replacement during RHS evaluation and moved parameter
handoffs. Real programs run at O0/O2 with poisoned frees, zero final live
allocations and a fixed peak bound, both at the normal collection threshold
and with collection forced before every cell allocation. Native probes cover
descriptor rejection, shared tracing once per backing/epoch, borrowed array
imports, cross-heap rejection before epoch shortcuts and malformed pin CFGs.
Persistent roots also survive a spawning function's stack-boundary restore.
Real collection after middle, tail and head removal checks non-LIFO cleanup
and the surviving value at O0/O2. The heap is 64 bytes; persistent nodes are
32 bytes and unlink in constant time. Stack roots retain their 24-byte ABI.

Raw native IR clients must pin each live pointer-array owning place or fresh
descriptor around collecting calls and cell construction; borrowed Read slots
are not owners. Pin regions cannot be entered after their first push. Scalar
descriptor slots and pointer-bearing inline record slots are initialized and
rooted until their function returns. Record field descriptors retain the same
cell identity. Their exact nominal+layout tag and target-derived offset/size
are checked before access; field lookup is logarithmic in the field count.
Inline root tracing also follows nested scalar/pointer-only records. One trace
helper per reachable record type calls its child helpers, so shared type graphs
do not expand into every leaf path during emission. Untagged result wrappers
can be roots without becoming managed cell layouts or projectable pointees.
`InlineRecords.chibi` returns a wrapper as the sole initialized root, requests
collection through allocator callbacks, then reads its nested cell value.

The source proof includes a live ten-thousand-record chain and twenty thousand
discarded two-cell cycles, field aliases, copied pointer fields, readonly/scoped
aliases and once-only compound assignment through a replaced pointer variable.

Generic embedded modules and Wasm require a collector-owner adapter before
managed admission. Nominal char, inline-nested and non-scalar container cells
remain closed, as do pointer buffers at the foreign boundary.
Standalone waiting frames keep scalar pointer roots until completion and pin
pointer-array owners across each suspension. A by-value array parameter owns
its descriptor independently of its caller. Cancellation and failed assertions
release active owner pins before freeing the coroutine frame. Source tests
cover observer drops, inline awaits, parent-child cancellation and reuse under
normal and forced collection; raw failure remains 134 and public run reports 1
with its source and custom message.

Inline pointer-bearing record frames and retained values still require scheduler
roots, collector-family adoption and generation metadata retention before admission.
Trace and drop callbacks must outlive their cells. Drop callbacks release
external value owners without following managed pointer edges.

Raw typed cells now carry immutable layout metadata (trace, address validation,
and an optional per-image live-cell counter). Address validation follows owner,
semantic identity and range checks. A counted cell pins its image before
publication and releases that pin after its drop callback and last metadata
access. The host must keep images mapped until execution is quiescent and
both task and cell counts reach zero. Counter overflow/underflow traps.
The explicit shared-family emitter attaches its per-image counter to generated
record metadata. The generations suite keeps old cyclic record cells alive
across a layout-changing edit, then checks cancellation and collection release
their image leases before unloading. Generic library emission remains closed.
Numeric/bool array cells own their backing through typed drop callbacks. Their
whole-variable and pointer assignments acquire or transfer the incoming value
before dropping the old one; evaluated reads keep independent value owners.
The source proof covers escaped array cells, pointer-array owners, scoped
aliases, value copies, variable element writes/append and canonical foreign
byte-buffer writes. Inline pointer-bearing record arrays and array-backed
record cells still need their respective tracing and place lowering.

Pointer-backed views use a 32-byte cell/start/count descriptor.
They follow replacement and growth of that cell while retaining a fixed window;
ordinary array copies, view clones and slices keep their value semantics.
Direct views and their optional union slots are traced. The source proof runs
the original nine successful kernels, an escaped optional-only root under
collection, and initial/late mutation failure checks. A separate 4096-append
entry requires at most 32 allocations; the current implementation uses 13.
Fatal failure probes check their category with poisoned frees, while successful
probes also require zero live allocations. String reads acquire an independent
text owner before later callbacks can replace the backing. Writes preserve the
incoming owner before releasing the old element; clones and slices copy through
the ordinary typed array lifecycle, including each text owner.
Authored unions with one empty variant and one admitted view payload reuse
the same traced tag/window layout.
Their source proof covers returned union-only roots, owner replacement, switch
and `is` bindings, and either tag order. Pattern bindings remain read-only.
Immutable text views acquire the existing counted string owner; uncounted
host or literal bytes are copied before the view escapes. Half-open ranges
count bytes, including embedded NULs and partial UTF-8 sequences; invalid
bounds give absence and valid empty ranges give a present empty string.
Bracket slices and clones independently copy their bytes. The source proof
checks returned temporaries, callback evaluation order, strict bounds, and
4096 counted windows without a new backing allocation per window, and 4096
uncounted windows with an independently owned copy for each.
Pointer-free owned records, unions and nested arrays can also be view elements.
A read acquires its nested owners before subsequent callbacks; a write owns the
incoming element before detaching or releasing the old value. Clone and slice
use the same typed array lifecycle. Every view operation carries its expected
full element schema. The schema graph includes reachable union definitions and
preserves identities under unrelated insertion and local record reindexing.
The two-image proof checks compatible access, changed-schema rejection and old
callback-image leases at O0/O2. Source ownership proofs include the original
nested-array/union lending cases, view-only roots and callback collection.
Ordinary source already rejects view fields in records. GC-bearing elements,
view containers and retained views remain outside this admission.

Waiting functions keep direct windows and their exact optional/authored-union
wrappers in persistent frame roots. Each slot/local uses the same 32-byte root
node as existing collector frames and traces the window's cell. Completion and
cancellation unlink these nodes before freeing the frame. The source proof
covers primitive and owned aggregate views, reads/writes after suspension,
optional and union parameters, a returning spawning caller, nested cancellation,
and collection requested by allocator/release callbacks. The reload proof changes
the incoming element schema while an old worker keeps its original view/code.
View promises keep a separate persistent root through final suspension until
its last observer is released. Direct windows use the window tracer; exact
optional/authored-union results use the existing inline tracer. Observer copies
refer to the same cell, so writes through one window remain visible through
another. Promise proofs cover late and repeated joins, immediate optional
results, completed handles, cancellation, callback collection and failed tasks.
The reload worker also holds a completed view promise from the old schema.

Ordinary source also accepts direct-view results and handles for those results;
a handle for an optional-view result retains the ordinary union-result rejection.
The retained ordinary test runner forbids scheduling, so the async acceptance
probes compile calls under a false branch, with an invalid-after-yield body as
a control. They establish source acceptance, not resumed-runtime behavior.
Synchronous ordinary probes execute views over pointer elements and records
containing pointer or pointer-array fields. Those GC-bearing element forms are
remaining native parity work, separate from frame and promise roots for admitted views.

Optional numeric/bool values and flat numeric/bool record payloads can share
managed cells. The optional tag and payload move together; nil replacement
and scoped alias writes are visible through every pointer. The cell keeps the
same 64-byte header and uses a complete optional nominal/layout identity.
Its immutable validator exposes the whole payload; inline field projection
keeps the existing flat-record rule. Backing owners and pointer/view edges
inside optional cells remain outside this slice. The original four fallible
pointer cases run at O0/O2, and the poisoned source proof checks escaped cells,
early returns and forced collection with bounded peak allocation.

Optional string cells retain the whole optional identity and own only a present
text payload. Nil clears the payload without inspecting its bytes. Constructors
and explicit, boxed-variable or scoped writes import borrowed text before
releasing the previous owner. The raw proof clobbers borrowed present bytes and
checks that Nil clears a valid nonzero inactive field. The source proof covers
Nil, present empty strings, snapshots before callbacks, returned/parameter cells
and assertion cleanup. Its emitted-control check rejects a missing Nil skip
before compiling or executing the program. The two-image proof writes an actual
returned optional literal into an older cell, clobbers and unloads the writer,
then reads and releases the old cell. Removing the import fails at O0/O2.

Scalar string cells own an immutable text descriptor. Copies retain counted
owners and import borrowed bytes; replacing through a pointer, boxed variable
or scoped reference acquires the incoming owner before dropping the old one.
Evaluated reads survive later replacements. The cell's validator and final text
release carry the same image lease as other managed payload callbacks. Source
and raw probes cover dynamic NULs, returned cells, borrowed-byte clobbering,
scoped aliases, empty replacement, and precise tag/storage rejection. The
image proof writes a returned literal through actual source lowering into an
older cell, clobbers the writer bytes, unloads that image, and reads the old cell
before its final owner release. Bypassing only that source copy fails at O0/O2.

String-array cells use the same descriptor and backing lifecycle as numeric
array cells. Last cell release drops the backing and its owned text values;
evaluated reads hold independent array owners before later callbacks can
replace the cell. The source proof uses embedded NUL strings, escaped cells,
value copies, scoped aliases and replacement during operand evaluation. Its
normal-threshold peak bound includes backing and text allocations per cell;
forced-collection runs retain the same zero-live requirement.

The family heap includes callback depth and collection state. Collector
requests during a guarded callback or active sweep defer to a legitimate
spilled-root safe point; callback return never collects transient values.
The raw collector proof checks these requests below threshold, recursive
release requests, depth bounds and nested construction outside collection.
Constructing a managed cell during sweep traps before allocation or linking.

Source collection requests use a pending-bit test after operand ownership is
complete and at loop headers. `ManagedTrack` updates one initialized root table
at local ownership changes; statement temporaries register only across their
poll. This keeps emitted metadata linear in ownership events rather than
statements times live owners. Suspended tables live in coroutine frames, with
embedded addresses initialized after `coro.begin`. Empty scoped-reference
entries are skipped. The raw native API requires owners to clear registrations
before their descriptors cease to be live.

All generated allocator and release calls in collector programs and shared
family images use the same callback guard, including array/map/text helpers,
the scheduler, value comparisons and retained snapshots. Pointer-free standalone
programs retain the five-pointer context and avoid collector checks. Shared
images poll even when they contain no pointers, since older images can still
own cells. The callback source audit constructs dead cells inside allocations,
requests collection from both allocate and release, and checks loop progress,
early returns, assertion cleanup and cancellation with bounded peak allocation.

A requested poll preserves the caller's POSIX errno or Windows last-error value
around collection. POSIX accessors remain weak when the source has no strong
import, so a freestanding Linux program can use the null-accessor path without
libc. `weak.sh` executes that path at O0/O2; the directory provider suite checks
that release callbacks really clobber error state before the OS error capture.
`growth.sh` bounds LLVM growth for 100 and 1000 live pointer-array locals.

Pointer and nullable-pointer promises use one 32-byte persistent root node for
the typed result slot. Completion unlinks local roots but keeps that node until
the last observer destroys the frame. Inline awaits transfer the pointer before
clearing the child slot and destroying the child; collection can resume only
after the caller's root is initialized. The source proof includes collection
before the first join, repeated joins, successful nil versus cancellation, and
old-generation promises read after incoming-generation allocation.

A pointer parameter whose body can yield or await captures a managed cell.
The existing forwarding graph propagates this rule through synchronous callers,
so no borrowed stack reference survives a task's suspension. The source proof
keeps captures alive after their spawning caller returns and cancels them under
allocation pressure. Borrowed native reference parameters, pointer-array
results and view promises remain closed.

Waiting functions may read or replace a plain optional cell through an immediate
managed-address access. The temporary record reference cannot enter a local,
call argument or later statement, and control flow cannot enter its read or
write/readback chain. Borrowed record-reference parameters remain rejected.

Pointer-only nested record promises reuse the same typed result slot and
persistent root. Child-record trace helpers follow initialized inline fields;
record copies share pointer identities while scalar fields retain value
semantics. The source proof covers repeated joins, by-value waiting parameters,
eager completion, cancellation and propagated failures. Reload keeps an old
nested promise's cyclic graph alive while the incoming generation uses a
changed cell layout.

Pointer-array promises and inline records containing pointer arrays use the
existing copy/drop graph. Each join acquires its own descriptor; inline await
moves and clears the child's promise before destruction. One 32-byte promise
root survives final suspension. Owning frame locations use an eight-byte active
address table grouped by static type, with direct trace loops per type. Borrowed
record snapshots do not remain roots after their backing owner is replaced.
The source proof includes completed-before-join collection, repeated joins,
copy-on-write mutation, nested pointer and text fields, cancellation and both
caller and child failures with poisoned zero-live-allocation checks. Reload
keeps an old completed array promise alive and preserves independent joins.
Arrays/maps whose elements contain collector edges and view promises remain
outside this result domain.

Value-only records may combine direct pointer fields with pointer-array and
text owners. Their structural root metadata does not make them cell layouts.
`InlineOwners.chibi` checks synchronous copies, repeated promise joins, by-value
waiting parameters and cancellation under collection. Taking the address of an
owned record remains rejected until its cell layout owns and traces those fields.
