# Native managed pointers

Run `sh run.sh NATIVE_COMPILER LLVM_BIN [WORK]` through the shared heavy wrapper.
The authored probe emits the real collector, compiles it with LLVM at O0/O2,
and runs it with poisoned frees and exact live-allocation counts.

The current slice proves explicit stable roots, nonmoving cells, rooted and
unreachable cycles, escaped cells, changed roots, separate owners, bounded
allocation during 100,000 discarded cycles, and iterative traversal of a
10,000-cell chain. Cross-owner marks and unmatched root pops trap.

This collector is not source-pointer admission. Source stack roots, exact
pointee identity checks and pointer-array isolation are the next slice.
Pointer-bearing suspended frames and retained values require scheduler roots,
collector-family adoption and generation metadata retention before admission.
Trace and drop callbacks must outlive their cells. Drop callbacks release
external value owners without following managed pointer edges.
