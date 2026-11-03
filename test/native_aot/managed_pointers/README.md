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
and the surviving value at O0/O2. The heap is 56 bytes; persistent nodes are
32 bytes and unlink in constant time. Stack roots retain their 24-byte ABI.

Raw native IR clients must pin each live pointer-array owning place or fresh
descriptor around collecting calls and cell construction; borrowed Read slots
are not owners. Pin regions cannot be entered after their first push. Scalar
descriptor slots and pointer-bearing inline record slots are initialized and
rooted until their function returns. Record field descriptors retain the same
cell identity. Their exact nominal+layout tag and target-derived offset/size
are checked before access; field lookup is logarithmic in the field count.
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

Scalar/string pointer-backed views use a 32-byte cell/start/count descriptor.
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
Authored unions with one empty variant and one scalar/string view payload reuse
the same traced tag/window layout.
Their source proof covers returned union-only roots, owner replacement, switch
and `is` bindings, and either tag order. Pattern bindings remain read-only.
Ordinary source already rejects view fields in records. Views over records,
nested arrays or union elements, and immutable `text.view(range)`, are
working ordinary forms still awaiting native support. View containers, retained
views and suspended view frames also remain outside this admission.

Optional numeric/bool values and flat numeric/bool record payloads can share
managed cells. The optional tag and payload move together; nil replacement
and scoped alias writes are visible through every pointer. The cell keeps the
same 64-byte header and uses a complete optional nominal/layout identity.
Its immutable validator exposes the whole payload; inline field projection
keeps the existing flat-record rule. Backing owners and pointer/view edges
inside optional cells remain outside this slice. The original four fallible
pointer cases run at O0/O2, and the poisoned source proof checks escaped cells,
early returns and forced collection with bounded peak allocation.

String-array cells use the same descriptor and backing lifecycle as numeric
array cells. Last cell release drops the backing and its owned text values;
evaluated reads hold independent array owners before later callbacks can
replace the cell. The source proof uses embedded NUL strings, escaped cells,
value copies, scoped aliases and replacement during operand evaluation. Its
normal-threshold peak bound includes backing and text allocations per cell;
forced-collection runs retain the same zero-live requirement.
