`run.sh NATIVE_COMPILER LLVM_BIN` runs the same source-level directory and
inventory checks at O0 and O2. `Run.ps1` is the Windows entry. Both build the
authored runner and use LLVM-only test providers; there is no C shim.

The public `os.List` result ABI remains `(names, error)`. The raw platform
`sys.List` compatibility API still returns an empty array on failure. Its
single checked implementation distinguishes a valid empty directory from an
open, read or close failure and discards partial inventories.

POSIX clears thread-local errno immediately before `readdir` and copies the
result into preallocated bytes before leaving the failure branch. Windows
captures `GetLastError` before loop cleanup or the next OS call; `ERROR_NO_MORE_FILES`
finishes a search, while `ERROR_FILE_NOT_FOUND` from the first search succeeds
only for a directory that still exists. Names remain sorted by `os.List`.

Private core staging replaces three exact enumeration call signatures and the
checked error-read checkpoints. The LLVM providers retain real OS handles and inject open, partial-read and
close failures. Windows also forces the documented empty no-match condition.
Both providers actually close their handles even when reporting a close failure.

The probe checks complete and empty results, raw API compatibility, discarded
partial names, `engine.NativeInputs` refusal and a real native runner published
before a failed source walk. An armed runtime scope exercises instrumented
backedges. Failed workspaces are retained; successful owned workspaces are
removed. Actual Windows execution remains a hosted gate until its run passes.

`Poll.chibi` leaves a managed cell unreachable before enumeration. The provider
requests collection exactly when returning EOF, partial-read failure or empty
no-match. Its release proxy deliberately replaces errno or Windows last-error.
The result must stay correct, an actual release callback must run, and the heap
byte count must decrease before the OS error capture. Test-only LLVM wrappers
freeze that assertion at the real read, so a later caller poll cannot satisfy
it. This pins the ABI3 owner/requested layout and requires
GC statement polls to preserve thread-local OS error state. Both source probes
run at O0/O2; actual Windows execution remains the hosted acceptance gate.
