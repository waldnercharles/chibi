# native_link and native_link_host

The native compiler's link step: object files and vendored archives become an
executable through LLVM's lld, with no C compiler or compiler driver on `PATH`.

- `native_link` has no effects. `Plan` describes a program, `Command(plan, to)` is the
  exact lld argument vector for it, and the helpers write nothing. It is tested by
  `native_link/tests` under `chibi test tools/compiler`.
- `native_link_host` has the effects, over `core:os`: `Tool` finds an LLVM tool in the
  LLVM folder, `Object` runs `llc` over a module, `Discover` finds the C library's
  start files on Linux, and `Link` writes the macOS stubs, runs `Command`'s vector and
  publishes the program (`output.partial`, then a rename).

```chibi
import link "project:native_link"
import host "project:native_link_host"

failure := host.Object(llvm, "main.ll", "main.o", 2);
plan: link.Plan = {triple: "x86_64-unknown-linux-gnu", llvm: llvm, objects: ["main.o"],
                   archives: ["vendor/libbox3d.a"], libraries: ["m"], libpaths: [],
                   output: "program", work: "work", libc: "", interpreter: "", static: false};
failure = host.Link(plan);
```

## What each target does

| Target | Linker (in `llvm`) | Default | `static` |
| --- | --- | --- | --- |
| `x86_64-unknown-linux-gnu` | `ld.lld` | Dynamic: the system's `crt1.o`, `crti.o`, `crtn.o`, `libc.so` and dynamic linker, found by `Discover` | `-static -e _start`, no C library: the objects define `_start` |
| `arm64-apple-macosx11.0.0` | `ld64.lld` | `libSystem` and each named dylib are generated text stubs in `work`; every other symbol is looked up when the program runs (`-undefined dynamic_lookup`) | the same: a macOS program always loads libSystem |
| `x86_64-pc-windows-msvc` | `lld-link` | The C runtime and import libraries come from `libpaths` and the `LIB` variable | `/nodefaultlib`, a `kernel32.lib` written from a `.def` |

`objects` come first and define the entry (`main`, or `_start` / `mainCRTStartup` when
`static`), then `archives`, then `libraries` by name: `m` on Linux, `c++` or
`framework:Cocoa` on macOS, `user32.lib` on Windows. A macOS framework is linked from a
stub naming its install path, so no SDK is read.

## Checks

- `native_link/tests` (`chibi test tools/compiler`) checks `Command` for all three targets,
  the stubs and the llc vector, without running a tool.
- `native-link` (`tools/tests.tsv`, `test/native_aot/link/run.sh`) links a real program: a
  module calling `add` from an archive built with `llc` and `llvm-ar`, with an empty `PATH`,
  and runs it to exit status 42. It needs `CHIBI_LLVM_BIN` (the folder of `llc`, `llvm-ar` and
  the lld tools) and says it did not run without it. The build host reads collections, not
  project packages, so `run.sh` stages the two packages as collections.

Failures are `Error` values (`kind`, and the tool's exit status in `code`); the tool's
own output reaches the build's output.
