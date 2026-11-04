# Graph-c compilation, runtime still pending

The updated registered graph-c entry at source checkpoint `63f0e52bca036ff93230ff0b7e10a8374442af68` compiled successfully with the admitted current compiler, pinned LLVM22 and two workers. Its source includes the portable-cookie regression. Compilation took 3.0807 seconds; the complete owned phase took 7.2842 seconds within its unchanged 95-second total limit, including five seconds for cleanup. Both compiler streams were empty. No produced program executed. These are correctness-run timings, not a benchmark.

Independent review checked all 4,088 tracked source files, 252 compiler/core equivalents, the 14-file conditional import projection, full tool/runtime/search/Python closure and the prior focused cookie proof. Before any launch, review caught and corrected a missing expired-budget refusal immediately before Popen. The correction and superseded controls remain preserved; the original phase budget was not reset or enlarged.

Separate final review admitted the actual 293,456-byte ELF EXEC, SHA-256 `046fabf20b00f394cecf4307c7f1758ff17fad7694b31bc3da9dac889321120f`. It uses the expected Linux interpreter and libc only, with no RPATH/RUNPATH. Complete source/tool/artifact identities, all nine authoritative phase evidence files and exact output/scratch census passed. The owner reached authoritative ECHILD without errors, signals or surviving children.

Final evidence was written 87.7157 seconds before the original deadline. One exact timing observation is retained: the intent's filesystem mtime/ctime is 0.000673 seconds earlier than the captured userspace start. Source order and exclusive creation after start, the accepted window, and all later evidence were independently verified. No exact nanosecond equivalence between filesystem and userspace clock captures is claimed; no deadline was changed.

Independent compile/artifact admission SHA-256: `d9a7c2ce6e16da3a3e2eeea94bc15184a975e556fcfe28c562417887d6983e7d`. Exact identities and results are in `graph-c-compile-results.json`. Verified archive `chibi-graph-c-compile-validated-20261104.tar.gz`: `3690b8403860b8d9072f08b8639f2c1713c52ffe997fc08e65425ebb3421a324`, 17,342,774 bytes, 4,128 regular files and one source symlink. All member bytes, modes and link targets were reread.

## Runtime boundary

This is not a graph-c test pass. Runtime remains refused by both public and owned-worker entry points. Its authored 20ms shell-group cancellation can create killed descendants, whereas the unchanged generic outer owner rejects every adoption. A separate narrow ownership design must identify the exact cancellation subtree before orphaning and reject every missed or unexplained identity, while retaining all assertions and the original 125-second envelope. No exception or runtime acceptance is supplied by this compile result.

## Separate host failure

The fresh strict GCC host build passed the earlier cookie failure but stopped at `src/types/array.c:2769` with `-Werror=dangling-pointer` on publication of a local equality graph to a thread-local pointer. Graph ran 2.2603 seconds; the complete phase failed after 4.2337 seconds, with clean ECHILD, all postfailure fences intact and no archives or host executable. No warning was suppressed.

Its seven completed objects have genuine records matching the corrected native serialization. All 582 dependency occurrences across 122 paths passed independent full-width identity and SHA checks. The failed 148-file partial build remains preserved in `chibi-compiler-host-lifetime-failure-20261104.tar.gz`, SHA-256 `0e33964f1240790f29e88ecfd9cc0e6df92d2215b57d158c3887779a84f902f3`, 44,349,880 bytes. This partial evidence does not certify a full build.

A minimal equality-cleanup source candidate and paired strict-unit test are separate pending work. Ordinary VM/native parity, C integration, actual Windows execution, native/interpreted stamp precision, the separate pointer-view work and exact-head integration remain open. PR #1331 is still held; the old terminal-unknown bootstrap and excluded union-leak investigation were not rerun.
