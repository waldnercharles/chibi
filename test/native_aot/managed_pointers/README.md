# Native managed pointers

Run `sh run.sh NATIVE_COMPILER LLVM_BIN [WORK]` through the shared heavy wrapper.
The authored probe emits the real collector, compiles it with LLVM at O0/O2,
and runs it with poisoned frees and exact live-allocation counts.

The current slice proves explicit stable roots, nonmoving cells, rooted and
unreachable cycles, escaped cells, changed roots, separate owners, bounded
allocation during 100,000 discarded cycles, and iterative traversal of a
10,000-cell chain. Cross-owner marks and unmatched root pops trap.

Standalone desktop programs admit numeric/bool scalar cells and
arrays of those pointers. Cells can escape creator returns; array copies
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
descriptor slots are initialized and rooted until their function returns.

Generic embedded modules and Wasm require a collector-owner adapter before
managed admission. Nominal char, record, nested and container cells remain
closed in this slice, as do pointer buffers at the foreign boundary.
Standalone waiting frames keep scalar pointer roots until completion and pin
pointer-array owners across each suspension. A by-value array parameter owns
its descriptor independently of its caller. Cancellation and failed assertions
release active owner pins before freeing the coroutine frame. Source tests
cover observer drops, inline awaits, parent-child cancellation and reuse under
normal and forced collection; raw failure remains 134 and public run reports 1
with its source and custom message.

Shared pointer-bearing frames and retained values still require family-owner
adoption and generation metadata retention before admission.
Trace and drop callbacks must outlive their cells. Drop callbacks release
external value owners without following managed pointer edges.
