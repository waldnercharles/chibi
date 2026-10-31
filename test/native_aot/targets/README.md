# Native modules on macOS and Windows

`language-scalar-native` (`--package-compiler-chibi-native`) runs every
native scalar module on Linux x86-64. When `CHIBI_NATIVE_BUNDLE` names a
directory, it also emits each module it runs for the other targets
`native_abi` admits and builds them into that directory:

- each module's IR comes from `scalar_llvm` (or `llvm_native` for the
  arithmetic module) for `MacOSArm64()` or `WindowsX64()`, with the same
  probe adapters as the Linux run;
- `opt` and `llc` build it at O0 and O2 with the native driver's exact
  arguments, and the optimized module must declare the target's triple and
  the data layout `tools/native_aot/driver/Driver.chibi` admits for it;
- the object must have no undefined symbol, except `_fltused` and
  `__chkstk` on Windows, which the platform runtime defines: Windows code
  that uses floating point references `_fltused`, and a frame larger than a
  page calls `__chkstk`. The bundle's `windows-x64/runtime.o` defines both,
  `__chkstk` as compiler-rt's x86-64 probe;
- `<target>/modules.txt` lists each object, then each call with the value
  both VMs and the native Linux runs gave: `call SYMBOL EXPECTED COUNT
  ARGUMENTS...`.

`native-build` (`test/native_aot/native_build/BuildTests.chibi`) adds programs that
`chibi build --native --target` links on Linux, at O0 and O2, and one with a
256 KiB frame, which Windows code probes through the program's `__chkstk`.
`<target>/programs.txt` lists each as `program NAME STATUS`, optionally
followed by tab-separated UTF-8 arguments, and the program is
`<target>/programs/NAME`. The projects group, which runs `native-build`,
runs before the language group, so CI's bundle holds both.

`native-link` (`test/native_aot/link/Smoke.chibi`) adds the argument program, linked by
`native_link` with no SDK: listed once without arguments and once with `a b` and `héllo €`.
It checks `chibi_argument_count`, `chibi_argument` and `chibi_environment`. On Windows
`run.c` starts it with `_wspawnv`, so the arguments reach `GetCommandLineW` as UTF-16.

The Linux cases that supervise a hung or overflowing native call, and the
lazy-arm and lookup-table checks that rewrite or hand-write IR, stay on
Linux.

`run.c` links and runs a bundle on its own target. It links each object as a
shared library, with `clang -dynamiclib` on macOS and `lld-link /dll
/noentry /nodefaultlib` with an export per called symbol on Windows, then
loads it and calls each symbol: `chibi_scalar` with no arguments, returning
an `int32_t`, and each `probe_` adapter with its `int64_t` arguments,
returning an `int64_t`. Every value must match. Then it runs each listed
program and checks its exit status; on macOS it first marks each one
executable, since artifacts keep no file modes. The engine's test drivers do
not build on Windows yet, so `run.c` stands alone and clang builds it with
no other source.

CI's `native` job writes the bundle, and `native-targets` runs it on
`macos-15` and `windows-2022`. To do the same by hand:

```sh
CHIBI_LLVM_BIN=/usr/lib/llvm-18/bin CHIBI_NATIVE_BUNDLE=/tmp/bundle \
  build/test/language/test_language --package-compiler-chibi-native
# then, on a Mac with the bundle copied over:
clang -std=c2x -O1 -I. test/native_aot/targets/run.c -o run
./run /tmp/bundle /usr/bin/clang
```

On Windows, build `run.c` with `clang -D_CRT_SECURE_NO_WARNINGS` and pass
`lld-link.exe` as the linker. The objects come from LLVM 18, whose arm64
layout the driver admits; another LLVM major gives arm64 a different layout,
and the bundle stops at that check.
