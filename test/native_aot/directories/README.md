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

Private core staging replaces only the three exact enumeration call signatures.
The LLVM providers retain real OS handles and inject open, partial-read and
close failures. Windows also forces the documented empty no-match condition.
Both providers actually close their handles even when reporting a close failure.

The probe checks complete and empty results, raw API compatibility, discarded
partial names, `engine.NativeInputs` refusal and a real native runner published
before a failed source walk. An armed runtime scope exercises instrumented
backedges. Failed workspaces are retained; successful owned workspaces are
removed. Actual Windows execution remains a hosted gate until its run passes.

Integration gate: compiler-inserted GC statement polls must preserve errno and
Windows last-error while running deferred callbacks. The original O0/O2 proof
uses a compiler without those polls. A callback-clobbering composition control
is required before accepting this listing implementation with the new poll runtime.
