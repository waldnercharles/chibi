# Native build runner

`chibi build` should compile the project's build program natively and run it, as `zig build`
compiles `build.zig`. Today it runs the program on the VM inside `chibi-build-host`, a C program.
This document says what the runner is, what it needs from other lanes, and the order in which the
pieces land. The goal is item 1, 3 and 4 of `handoffs/goal.md`: no first-party C in the toolchain.

## Today

`chibi-build-host` is `src/host/build_main.c`, `build_host.c`, `build_io.c`, `build_values.c`,
`build_catalog.c`, `build_timing.c` and `process.c`, about 4,000 lines of C. It recovers the compiler
from the seed (cached since #911), compiles one `.chibi` file with its imports, and runs a
zero-argument function on the bytecode VM. The program reaches the machine only through 40
`build.*` natives (`src/host/build_catalog.c`).

The programs are `tools/toolchain/*.chibi` and `tools/graph/**`: 18 files, about 10,400 lines, 307
functions, 7 records, 454 array-typed declarations, 23 `format_text` calls. They import only
`core:strings` and their own packages. They use strings, arrays, records, integers, booleans,
loops and the natives. They use no unions, maps, `switch` or `@error`: `Build.chibi` must parse
with the host's own syntax (see `docs/language-friction.md`), so the build graph is written in a
small subset. That subset is what the native backend must cover first.

## Target

```
chibi build ARGS
  root      = nearest ancestor holding tools/toolchain/Driver.chibi (or the project's Build.chibi)
  key       = sha256(sources of the program's closure, compiler identity, target, runtime version)
  runner    = <root>/.chibi/build-runner/<key>/runner
  if runner is missing:  compile the closure with the native driver (W17), link it with lld (W18),
                         publish by rename
  exec runner ARGS
```

The runner is the build program itself, compiled with a `main` that calls its entry and exits with
the entry's result. It reaches the OS through `core:os`, not through `build.*`. A change to the
build program recompiles once, in seconds, and every later `chibi build` is an `exec`. The
`--timeout-ms` and process-group machinery in C (`process.c`) moves into `core:os` and the
program. `chibi-build-host` and its C files (`src/host/build*.c`, `process.c`) are deleted when the runner
replaces them.

## What it needs

| Need | Why the runner needs it | Owner | State |
| --- | --- | --- | --- |
| W7 arrays | the programs are arrays of strings throughout | sdk-cmake | blocks 491 of 700 compiler functions |
| W9 strings and arrays in records | 7 records with `string[]` and `string` fields | builtin-math | landed per `goal.md` (#871); the ledger row still says no |
| W6 allocating text | `format_text`, joins, replace | core-math | partly (#768) |
| W13 imports and multi-file packages | 18 files, 6 packages | core-math | not started |
| W14 `foreign import` | `core:os` over libc | builtin-math | not started |
| `core:os` | files, directories, processes, environment, time, hashing | portable-libs | not started |
| Native runtime without C | strings, arrays, allocator and collector the native code calls; today it is the C context table `execution_native_context` | undecided | see below |
| W17, W18 | compile and link a program from `chibi build` with no `cc` | exists on Linux x86-64 | macOS and Windows links are cross-only |

The native runtime is the open decision. Item 4 of the goal forbids first-party C, so the
`execution_native_context` table in `src/execution` cannot be the runtime the runner links
against. The runtime has to be Chibi compiled natively: an allocator over `malloc` through
`foreign import`, string and array headers and their copy-on-write operations, and a collector (or
none for a short-lived process: a build program could run with a bump allocator and exit). The
runner is a good first customer because it is short-lived and single-threaded, so an arena runtime
with no collection is enough for it, while the compiler needs more.

## The `core:os` surface

Each `build.*` native, the `core:os` call that replaces it, and the libc it needs. Counts are call
sites in `tools/toolchain` and `tools/graph`.

| `build.*` (uses) | `core:os` | libc |
| --- | --- | --- |
| `read_file` (34), `write_file` (25), `copy_file` (7), `embed_file`, `byte_list` | `read`, `write`, `copy` | `open`, `read`, `write`, `close`, `fstat` |
| `file_exists` (30), `file_kind` (47), `file_stamp` (15) | `stat` (kind, size, mtime) | `stat` / `lstat`: struct layout differs per platform |
| `list_directory` (22) | `list` | `opendir`, `readdir`, `closedir`: `dirent` layout differs per platform |
| `make_directory` (28), `remove` (16), `rename` (3), `reserve_directory`, `publish_directory` | `mkdir`, `remove`, `rename`, `reserve`, `publish` | `mkdir`, `unlink`, `rmdir`, `rename` |
| `resolve_path` (9), `root` (23), `program_directory` | `realpath`, `cwd`, `executable_path` | `realpath`, `getcwd`, `_NSGetExecutablePath` or `readlink` |
| `hash_file` (26), `hash_text` (10) | `core:hash` sha256 over bytes | none: `core:hash` has adler32, crc32 and fnv, so SHA-256 is a new Chibi package (`src/support/sha256.c` goes with it) |
| `run` (12), `probe` (24), `last_stdout` (23), `last_stderr` (2) | `run`, `capture` | `posix_spawn`, `pipe`, `poll`, `read`, `waitpid` |
| `start` (1), `wait` (1) | `spawn`, `wait` | `posix_spawn`, `waitpid`, `kill` for deadlines |
| `find_executable` (15) | `find_executable` | `getenv("PATH")`, `access` |
| `environment` (8), `environment_names` | `getenv`, `environment` | `getenv`, `environ` (`_NSGetEnviron()` on macOS) |
| `arguments` (9), `platform` (41), `report` (101) | `arguments`, `platform`, `print` | `argv`, compile-time target, `write(2, ...)` |
| `join` (21), `concat` (21), `before` (46), `after` (65), `starts_with`, `lines`, `trim`, `replace` (18), `length` | `core:strings` and arrays | none |

The last row is already Chibi: `core:strings` has `Join`, `Replace`, `Trim`, `SplitLines`, `Cut`,
`HasPrefix` and the rest. Porting the programs to `core:strings` first removes 9 natives and about
190 call sites before any native work.

## What `core:os` asks of `foreign import`

For builtin-math, from the table above:

- Pointers to caller-owned byte buffers (`^u8` with a length) for `read`, `write`, `stat` and
  `dirent`; the program reads fields at per-platform offsets that `core:os` keeps as constants.
- Opaque, zeroed, aligned buffers for `posix_spawn_file_actions_t` and `posix_spawnattr_t`; they
  are only passed by pointer to the spawn calls, and 256 bytes covers both platforms.
- Variadic declarations: `open` and `fcntl` are variadic, and on arm64 macOS variadic arguments
  go on the stack, so the declaration must say how many arguments are fixed. `fopen` and `fread`
  avoid the question for files if variadic is not supported at first.
- A data symbol (`environ` on Linux) or a function (`_NSGetEnviron` on macOS).
- `errno` through `__errno_location` or `__error`.
- Strings crossing as NUL-terminated `char *`: `core:os` copies into a buffer, since a Chibi
  string is not NUL-terminated.
- A platform switch known at compile time so one `core:os` source holds both layouts.

## Order

Each step lands on its own and leaves the build working.

1. Port the programs from the text natives to `core:strings` and arrays (no behavior change).
   Records, now.
2. Run the native census over the build programs, as W21 does for the compiler, so the work above
   is counted in functions: which of the 307 pass the producer, and the first blocker of the rest.
3. Define `core:os` as a package with the surface above, implemented on the VM over the existing
   natives, and port the programs from `build.*` to `core:os`. Then the natives are an
   implementation detail of one package, and `core:os` can switch to `foreign import` without the
   programs changing. portable-libs owns the package; records ports the callers.
4. When W7, W9, W6, W13, W14 and the native runtime land, compile the programs natively with W17,
   add the `main` wrapper and the runner cache to `chibi build`, and run the same builds on both
   paths until the outputs match.
5. Delete `chibi-build-host`, `src/host/build*.c` and `process.c`, the `build.*` catalog and the
   pinned first host; `tools/bootstrap.sh` then fetches the seed compiler, not a host.

## Open decisions

- The native runtime written in Chibi: owner and shape (above). Blocks step 4 and item 4.
- Where the cache lives (`.chibi/build-runner` in the checkout, or a global `~/.cache/chibi` as
  Zig does) and what the key covers. The compiled-compiler cache of #911 chose the checkout.
- Windows: `posix_spawn` and `stat` do not exist; `core:os` needs a second implementation behind
  the same surface (`CreateProcessW`, `FindFirstFileW`).
