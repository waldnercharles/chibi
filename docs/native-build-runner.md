# Native build runner

`tools/build.sh` starts `chibi build` with the native compiler pinned in
`tools/bootstrap/native-seed.tsv`. The seed is a release asset, verified against
its SHA-256 before use. A cached seed is verified again on every invocation.
`--native-driver PATH` selects a different compiler explicitly.

LLVM comes from `--llvm DIR`, `CHIBI_LLVM_BIN`, or the directory containing `opt`
on `PATH`. The directory must contain `opt`, `llc` and the platform's linker:
`ld.lld` on Linux, `ld64.lld` on macOS. The supported native default platforms
are Linux x64 and macOS arm64. Windows filesystem and cache operations now
use kernel32 and have cross-link checks, but still need a Windows execution
result. Windows also needs a verified seed and asynchronous process operations
before this entry can select it.

```
CHIBI_LLVM_BIN=/path/to/llvm/bin sh tools/build.sh build --release
sh tools/build.sh test --llvm /path/to/llvm/bin --list
```

## Entry and program caches

The shell entry compiles `tools/toolchain/Driver.chibi` with entry `Main`, then
runs it with the build arguments. It caches the executable under
`.chibi/build-entry/KEY/driver`. The full SHA-256 key covers the driver source,
core sources, native compiler, LLVM tools, target and tool paths. A failed
compile publishes no executable and stops the command.

The authored driver compiles `Build.chibi`, `Graph.chibi`, `SDK.chibi` and the
other programs it invokes into `.chibi/build-runner/KEY/`. Their key covers the
source and entry, collections, target, and file stamps of the native compiler,
LLVM tools, `tools/toolchain`, `tools/graph` and `packages/core`. Each compilation
uses a private directory and publishes by rename. A failed compile reports its
source and stops the build. There is no automatic VM fallback.

The programs call `core:os`. Its native implementation uses `core:sys/posix` or
`core:sys/windows` through `foreign import`. It does not call the C `build.*`
natives. `Driver.chibi` no longer fetches a build host just to run these programs,
and selecting a target no longer adds `chibi-build-host` as an extra target.

## What still needs C hosts

Native execution of the build programs does not make every product native:

- `tools/graph/engine/Engine.chibi` still builds C products and uses the packaged
  compiler generator. That generator needs the build and compiler hosts.
- A non-release build still prepares release hosts for that generator unless
  `--asset-generator-hosts` is supplied.
- The installed SDK and C test drivers still name these products.
- The explicit old bytecode bootstrap route still uses the compiler host.
  Native recovery and the vendor-sample route use the native compiler.

The native CLI, native driver parity, and retained-state host replace these
callers. Delete the packaged compiler generator and its targets with that
cutover. Then remove `chibi-build-host`, `src/host/build*.c`, `process.c`, the
`build.*` catalog, and the host/bytecode pins. Deleting them before their
remaining consumers move would break the build.

## Verification

`native-self-host` uses the selected native driver as stage one by default.
It builds stages two and three and compares their full hashes. When an older
seed produces a different stage two, it also builds stage four and requires
stages three and four to match. It compiles and runs the native build driver
and graph too. Only `--compare-vm` requests the old VM comparison and its C hosts.

```
sh tools/build.sh native-self-host --llvm /path/to/llvm/bin
sh test/bootstrap/native-entry.sh
sh test/bootstrap/native-runner.sh /path/to/native/compiler /path/to/llvm/bin
```

The entry fixture checks argument boundaries, cold/warm use, content changes,
compile failures, seed corruption and invalid release checksums. The runner
regression compiles real native programs, checks reuse, rejects broken source,
checks cleanup, and verifies the repaired program runs without a build host.
