# Native build runner

`chibi build` should compile the project's build program natively and run it, as `zig build`
compiles `build.zig`. Today it runs the program on the VM inside `chibi-build-host`, a C program.
This document says what the runner is, what it needs from other lanes, and the order in which the
pieces land. The goal is item 1, 3 and 4 of `handoffs/goal.md`: no first-party C in the toolchain.

## Today

`chibi-build-host` is `src/host/build_main.c`, `build_host.c`, `build_io.c`, `build_values.c`,
`build_catalog.c` and `process.c`, about 4,000 lines of C. It recovers the compiler
from the seed (cached since #911), compiles one `.chibi` file with its imports, and runs a
zero-argument function on the bytecode VM. The program reaches the machine only through 40
`build.*` natives (`src/host/build_catalog.c`).

The programs are `tools/toolchain/*.chibi` and `tools/graph/**`. They import `core:os` and
`core:strings`, and their own packages through the `graph` collection. They
use strings, arrays, records, integers, booleans, loops and `Error?` results. They use no unions,
maps or `switch`: `Build.chibi` must parse with the host's own syntax (see
`docs/language-friction.md`), so the build graph is written in a small subset. That subset is what
the native backend must cover first. Their only calls of `build.*` are in `core:os`'s VM branch
and in the VM branches of `ByteList` (`Engine.chibi`) and `WriteBlob` (`Generate.chibi`), which write
the C initializers the asset compiler embeds; natively those two write the same text themselves.

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

| Need | Why the runner needs it | State |
| --- | --- | --- |
| Arrays, strings, records, `Error?` results and fields in the native producer | the programs are arrays of strings and `Error?` throughout | landed, except an optional as an operand of `||` and `&&` (below) |
| Imports across files and collections | `graph:` packages | not yet: the native driver compiles one package folder and its `project:` and `core:` imports |
| A named entry | a build program has several (`Build`, `Libraries`, `Install`, ...) and the producer reads the first function of the first file | not yet |
| `core:os` over libc | files, directories, processes, environment, hashing | every call the programs make has a native body (below) |
| A native runtime without C | strings and arrays the native code calls | exists: the start module supplies the runtime context from libc's allocator |
| Compile and link from `chibi build` with no `cc` | the runner cache | the native driver compiles and `native_link` links, on every platform (the Windows `core:sys/windows` bodies are not run yet) |

## The `core:os` surface

Each program call, its `core:os` function and what a native program runs. Paths are relative to
the program's root, which is the working directory natively and `--root` on the VM.

| `core:os` | VM | Native (`core:sys/posix`) |
| --- | --- | --- |
| `ReadText`, `ReadBytes`, `WriteText`, `WriteBytes` | `build.read_file`, `write_file` | `fopen`, `fread`, `fwrite` |
| `Kind`, `Exists`, `IsFile`, `IsDirectory`, `Stamp` | `build.file_kind`, `file_stamp` | `lstat`, `stat`, `access`; the stamp reads device, inode, size, mode, mtime and ctime at per-platform offsets |
| `List` | `build.list_directory` | `opendir`, `readdir`, then a merge sort by byte, as the VM lists |
| `MakeDirectory`, `Reserve`, `Publish`, `Remove`, `Rename`, `Copy` | `build.make_directory`, ... | `mkdir` for each parent, `mkdir`, `renamex_np`/`renameat2` without replace, `unlink`/`rmdir`, `rename`; a copy is written beside its destination, given the source's permission bits and renamed over it |
| `RealPath`, `Root`, `Platform` | `build.resolve_path`, `root`, `platform` | `realpath`, `getcwd`, the target's `core:target` OS |
| `HashFile`, `HashText` | `build.hash_file`, `hash_text` | `core:hash/sha256` over the bytes |
| `FindExecutable`, `Getenv`, `Arguments` | `build.find_executable`, `environment`, `arguments` | a walk over `PATH` with `stat` and `access`, `getenv`, the start module's arguments after the program's name |
| `Run`, `Capture`, `Start`, `Wait`, `Kill` | `build.run`, `probe`, `start`, `wait_for`, `kill` | `posix_spawnp` with a process group of its own and `/dev/null` as input for `Start`; `waitpid` polled in 2 ms steps for a timeout; `kill(-pid)`. A piped program writes its output and error to one private file named for its process id in `$TMPDIR` (else `/tmp`), which `Wait` prints to standard error when the program ends, as one stream; a program never waited for leaves its file |

`Environment` and `ProgramDirectory` have no callers in the programs and no native body. On Windows
`Stamp`, `Copy`, `Reserve`, `Publish`, `RealPath`, `Root`, `FindExecutable`, `Start`, `Wait` and `Kill` report
failure until `core:sys/windows` has them.
The sample `test/compiler/samples/native/foreign_posix` runs every row natively.

## State

Probe: the native driver (`tools/compiler/native_driver`, built by `tools/native_aot/Driver.chibi`
as stage one) compiles a package folder and links it with `native_link`:

```
stage-one --compiler tools/compiler --core packages/core --output OUT --llvm DIR --package P --target TRIPLE
```

- `tools/toolchain/Driver.chibi` (`chibi build` itself), copied to a package of its own with `Main`
  first, compiles and links natively and prints the same commands as the VM for `build --dry-run --target
  test_language` and `build install --dry-run`. The program is 255 KB and runs without the build host.
- `Build.chibi`, `Graph.chibi` and `SDK.chibi` stop at an `Error?` call or field as an operand of
  `||` (`if (os.MakeDirectory(d) || os.Copy(a, b))`, `hashed.error || ...`): the producer types the
  `||` expression by its operands, so a union operand makes it a union. `if (e)`, `!e` and
  `e != nil` already work.
- The C census (`test_language --native-census`) predates `when` and `foreign import` and cannot
  read `core:os` any more; the native driver is the probe. Retire the census's package mode
  when item 4 archives it.

## Order

Each step lands on its own and leaves the build working.

1. Done: the text helpers (#1204), `core:os` on the VM (#1182, ...) and natively, the residual
   `build.*` calls in the programs, and a Driver that compiles natively.
2. Type `||` and `&&` as bool in the producer, give the native driver `--source`, `--entry` and
   `--collection`, then compile `Build.chibi`, `Graph.chibi`, `SDK.chibi` and `AssetCompiler.chibi`
   natively and fix what the producer says about them one wall at a time.
3. Run the same builds on both paths until their outputs match: the compile and link commands of
   `Graph.chibi Libraries --commands-only on`, then a real build.
4. Add the runner cache to `chibi build`: compile the closure with the native driver, link it,
   publish it by rename under `.chibi/build-runner/<key>`, and `exec` it.
5. Delete `chibi-build-host`, `src/host/build*.c` and `process.c`, the `build.*` catalog and the
   pinned first host; `tools/bootstrap.sh` then fetches the seed compiler, not a host.

## Open decisions

- Where the cache lives (`.chibi/build-runner` in the checkout, or a global `~/.cache/chibi` as
  Zig does) and what the key covers. The compiled-compiler cache of #911 chose the checkout.
- One runner per `(program, entry)`, or one program with a verb switch: `Driver.chibi` starts
  `Build.chibi`, `Graph.chibi`, `SDK.chibi` and `AssetCompiler.chibi` as child programs only
  because the VM host runs one file at a time. Natively it can call them in the same process.
- Windows: the rows above that report failure there, and a native run of the others in CI.
