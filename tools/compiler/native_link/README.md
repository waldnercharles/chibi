# native_link and native_link_host

The native compiler's link step: object files and vendored archives become an
executable through LLVM's lld, with no C compiler or compiler driver on `PATH`.

- `native_link` has no effects. `Plan` describes a program, `Command(plan, to)` is the
  exact lld argument vector for it, and the helpers write nothing. It is tested by
  `native_link/tests` under `chibi test tools/compiler`.
- `native_link_host` has the effects, over `core:os`: `Tool` finds an LLVM tool in the
  LLVM folder, `Object` runs `llc` over a module, `Discover` finds the C library's
  start files on Linux, and `Link` writes the macOS stubs, runs `Command`'s vector and
  publishes the program (a rename from the work folder).

```chibi
import link "project:native_link"
import host "project:native_link_host"

failure := host.Object(llvm, "main.ll", "main.o", 2);
plan := link.Program("x86_64-unknown-linux-gnu", llvm, ["main.o", "start.o"], ["vendor/libbox3d.a"],
                     ["m"], [], "program", "", false);
failure = host.Link(plan);
```

## The start module and the process arguments

`link.Start(triple, static, context)` is the LLVM IR of the shim that starts a program, to be
compiled with `host.Object` and linked beside the program's own objects. It calls the entry
`chibi_scalar() -> i32` (`chibi_scalar(ptr)` with a runtime context) and exits with its result:
`main` for libc and dyld, `_start` with an `exit_group` for a static Linux program, and
`mainCRTStartup` with `ExitProcess` for a static Windows one. A static Linux program takes no
context ("" is returned).

Every start module also defines the process arguments as C-ABI functions, which `core:os`
binds with `foreign import`:

- `chibi_argument_count() -> i64`;
- `chibi_argument(index: i64, buffer: ptr, size: i64) -> i64` copies up to `size` bytes of the
  argument (no NUL) and returns its whole length, so a caller can retry with a larger buffer;
  -1 for an index out of range;
- `chibi_environment() -> ptr`, the `envp` array (null on Windows).

On Linux and macOS they read `chibi_argc`, `chibi_argv` and `chibi_envp`, which the shim sets
before the entry runs (from `main`'s arguments, or from the stack in the static `_start`). On
Windows the shim keeps the UTF-16 `chibi_command_line` (`GetCommandLineW`) and the functions
split it with the C runtime's rules and encode UTF-8; that parser (`CommandLineParser`) calls
no system function, and `test/native_aot/link/Parser.ll` runs its cases on any host.

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

Two links of the same inputs give the same bytes whatever the output is called or where the
work folder is: the linker writes `a.out` (`a.exe`) in the work folder, which is
`OUTPUT.work` unless the plan names one and is removed afterwards, lld-link gets `/Brepro`,
and the stubs carry no timestamps. `native-link` checks it.

Failures are `Error` values (`kind`, and the tool's exit status in `code`); the tool's
own output reaches the build's output.
