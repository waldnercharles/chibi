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

The native driver also accepts `--root FILE_OR_DIRECTORY` in place of
`--source`/`--package` and `--output`. A directory uses `Main.chibi` first and
includes its package siblings. A file compiles only that source and its import
closure. Outputs go below the root directory's `build/native/OLEVEL-KEY/` and
include `program`, `source.ll` and `receipt.txt`.

The cache identifies source contents, compiler and LLVM binaries, target,
options and foreign archives. A verified hit runs no LLVM process. Incomplete
receipts, changed programs and interrupted `.work`/`.partial` directories fail
with a recovery diagnostic. A failed build preserves every published output.

Run the real root/cache checks on Linux x86-64 or macOS arm64:

```
sh test/native_aot/native_driver/cache.sh /absolute/path/to/chibi /absolute/path/to/llvm/bin
```
