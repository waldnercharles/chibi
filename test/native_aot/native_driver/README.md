# Native driver options

Run the actual native compiler driver against real LLVM on Linux x86-64:

```
sh test/native_aot/native_driver/run.sh /absolute/path/to/chibi /absolute/path/to/llvm/bin
```

The driver must be built from this checkout. The script checks O0/O2 execution,
Linux dynamic-default and explicit-static linkage, libc calls and allocation,
macOS/Windows cross-link formats (not execution), DWARF line tables,
a relative archive and a relocated SDK's `vendor:` archive,
and shared libraries loaded and called by a native Chibi program. Rejected
options and missing archives must preserve existing output and write no
intermediate files. Compiler invocations and compiled programs run with an
empty `PATH`; the fixture archive is built with LLVM, not a C compiler.

Shared libraries currently use the existing single-source library producer;
package imports and `--entry` are not supported for that mode. This is focused
driver coverage, not a whole-language, sanitizer, or self-host fixed-point proof.
