`run.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN` copies the authored compiler into an SDK prefix,
moves that prefix, and compiles reflected catalog requests with an empty PATH
and failing C compilers. The relocated service has no SDK source tree, LLVM,
legacy host or VM beside it.

The scalar closure's digest was checked against the VM-hosted compiler, then
loaded and observed to return 42. Optional `LEGACY_HOST LEGACY_ASSET_ARTIFACT`
arguments repeat that differential and execution check. The ordinary regression
also checks identical source diagnostics, full-width unsigned catalog values,
malformed requests and preservation of existing output files on rejection.

`catalog.json` pins the current 64-bit prepared layout, ABI and instruction
vocabulary. Changes to that runtime contract require deliberately refreshing
both this fixture and the reference closure digest.

The shell compiles an authored test driver before removing the toolchain from
the execution environment.

The native service reads `REQUEST OUTPUT DIAGNOSTIC [BUDGET]`. A positive
decimal u64 arms the shared native compilation quota, four billion steps by
default. Exhaustion exits 125. Status 0 publishes a
prepared closure atomically. Status 1 leaves OUTPUT alone and writes the first
source diagnostic as decimal document, line and column, each followed by a
newline, then the message bytes. Status 2 reports a request or environment error
on stderr and leaves existing outputs alone. The desktop caller owns fresh
private paths for this protocol.

`bridge.sh ASSET_COMPILER NATIVE_DRIVER LLVM_BIN C_COMPILER` builds a real C
consumer against the desktop facade, process supervisor and support code.
The consumer links no compiler host or VM. It moves the SDK prefix before
checking closure bytes, source diagnostics, shared quota exhaustion, cancellation,
persistent cache hits and SDK-relative native service discovery. Its runtime
has an empty PATH and failing C compiler selections.

On Windows, run `Bridge.ps1 -Compiler STAGE3/chibi.exe -Llvm LLVM_BIN` after
native self-hosting. It locates clang-cl and the installed Visual Studio SDK
through vswhere; the SDK environment belongs only to the C compiler process.
The consumer embeds the SDK UTF-8 manifest and runs from a relocated path
containing Unicode, with no C compilers or LLVM in its runtime PATH. Windows
retains its existing behavior of compiling without persistent binary caching.
