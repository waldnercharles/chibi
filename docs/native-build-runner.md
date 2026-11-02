# Native build runner

`tools/build.sh` starts `chibi build` with the native compiler pinned in
`tools/bootstrap/native-seed.tsv`. The seed is a release asset, verified against
its SHA-256 before use. A cached seed is verified again on every invocation.
`--native-driver PATH` selects a different compiler explicitly.

LLVM/LLD 22 or newer is required. The tools come from `--llvm DIR`,
`CHIBI_LLVM_BIN`, or the directory containing `opt` on `PATH`. The directory must contain `opt`, `llc` and the platform's linker:
`ld.lld` on Linux, `ld64.lld` on macOS. The supported native default platforms
are Linux x64 and macOS arm64. Windows filesystem, cache and asynchronous process
operations use kernel32 and have Windows execution checks. The authored runner's
Windows paths, private directories, cleanup and receipts have also passed on
Windows. The default bootstrap entry still needs a verified Windows seed.

Windows metadata stamps can stay equal across rapid same-size rewrites. The
native compiler's output cache and the shell entry hash source and tool bytes.
The C graph and evidence checks use content fingerprints and sorted directory
inventories on Windows. The authored runner uses the same content fingerprints
for its sources and tools there. It rejects changes during a cold compilation
before publishing. The source watcher runs on Linux and macOS; its Windows port must
also account for this limit.

```
CHIBI_LLVM_BIN=/path/to/llvm/bin sh tools/build.sh build --release
sh tools/build.sh test --llvm /path/to/llvm/bin --list
```

## Entry and program caches

In an engine checkout, the native CLI compiles `Driver.chibi` through its normal
content cache with the checkout's core and graph collections. It enters the
checkout while compiling and running the driver, then restores the caller's
working directory on success or failure. This process-wide directory change is
serialized by the CLI. It does not start a shell or a build host.

The shell entry compiles `tools/toolchain/Driver.chibi` with entry `Main`, then
runs it with the build arguments. It caches the executable under
`.chibi/build-entry/KEY/driver`. The full SHA-256 key covers the driver source,
core and graph sources, native compiler, LLVM tools, target and tool paths. A failed
compile publishes no executable and stops the command.

The authored driver compiles `Build.chibi`, `Graph.chibi`, `SDK.chibi` and the
other programs it invokes into `.chibi/build-runner/KEY/`. Their key covers the
source and entry, collections, target, and file fingerprints of the native compiler,
LLVM tools, `tools/toolchain`, `tools/graph` and `packages/core`. Fingerprints include
content hashes on Windows. Each compilation
uses a private directory and publishes by rename. A failed compile reports its
source and stops the build. There is no automatic VM fallback.

The programs call `core:os`. Its native implementation uses `core:sys/posix` or
`core:sys/windows` through `foreign import`. It does not call the C `build.*`
natives. `Driver.chibi` no longer fetches a build host just to run these programs,
and selecting a target no longer adds `chibi-build-host` as an extra target.

## Products and remaining compatibility tools

The default graph products are the authored native compiler/CLI and asset
compiler service. Building or installing them does not discover a C compiler or
start a VM host. The installed CLI finds core and graph packages relative to its
own prefix. A project's `Build.chibi` is compiled through the native content cache
and its `Main` runs with the build arguments.

The pinned seed builds the current compiler first. That compiler builds the
asset service, whose runtime helpers may be newer than the seed. Selecting the
asset service or a target that uses it also selects the current compiler.

Explicit legacy C test and bootstrap targets remain while their consumers move.
The old bytecode bootstrap comparison is requested only with `--compare-vm`.
Native recovery and vendor samples use the native compiler.

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
The Windows checkout consumer additionally builds both default native products
from a checkout descendant with an empty `PATH`, missing C/VM tools and unset
`CHIBI_CORE`, then runs the produced CLI against its packaged core.
