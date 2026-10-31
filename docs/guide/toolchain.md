# Toolchain

Run `chibi` from an installed SDK or use `build/sdk/chibi` in an engine checkout.
The command finds the compiler and engine resources beside itself.

## Run a program

Create `Hello.chibi`:

```chibi
package hello

Main :: () {
    print_string("Hello, world!")
}
```

Run it with either command:

```sh
chibi Hello.chibi
chibi run Hello.chibi
```

The program prints `Hello, world!` and exits. The runner calls `Main` once.
`Main` is an ordinary function; only the program runner calls it automatically.
It must take no arguments and return either nothing or an `int` exit status
from 0 to 255. Returning nothing means success, exit status 0.

Read command-line arguments with `application.arguments()`. The returned
`string[]` includes the resolved source file or package directory at index 0,
followed by the arguments passed to the program. A function can take them as a
parameter and print the first one:

```chibi
package hello

Greet :: (arguments: string[]) {
    if length(arguments) > 1 {
        print_string(arguments[1])
    }
}
```

Call `Greet(application.arguments())` from `Main`, then run `chibi Hello.chibi -- Ada`
to print `Ada`. `Main` still takes no parameters; `--` separates program
arguments from command options.

Every source file must start with `package NAME`. Running a file compiles only
the selected file and the core library, so unrelated sibling files do not
affect it. It can import SDK packages with `core:NAME`.

When a program spans several files, run its package directory:

```sh
chibi .
```

This includes the package's sibling source files and supports local package
imports with `project:NAME`, alongside `core:NAME` imports. A directory with
`project.kdl` is a game project; see [Game projects](#game-projects).

With no path, `chibi run` looks for the nearest `project.kdl` in or above the
working directory. If there is none, it runs the package in the working
directory.

## Reload a program

Add `--watch` to keep the process open after `Main` returns:

```sh
chibi --watch Hello.chibi
chibi run --watch Hello.chibi
chibi Hello.chibi --watch
```

`Main` runs once. To keep a program running, give it a `Frame` function that
the runner calls about 60 times a second until you press Ctrl-C. `Frame` returns
nothing to continue, or a nonzero `int` exit status to stop. Use `retain` for a
value that should survive edits and calls:

```chibi
package reload

Main :: () {
    print_string("started")
}

Frame :: () {
    ticks: retain int = 0
    ticks += 1
    if ticks % 60 == 0 {
        print_string(format_text("tick {}", ticks))
    }
}
```

Each accepted save replaces the running code between frames. `Main` is not called
again, the next `Frame` runs the new body, and `ticks` keeps counting: retained
values stay (converted when their type changed), ordinary locals start fresh each
call. Change `"tick {}"` to `"tock {}"` and save to see it. See
[Edit and Continue](../edit-and-continue.md) for what is patched when.

A syntax or type error prints its diagnostic and keeps the last valid code
running; the next valid save applies. A runtime failure pauses the program, and
the next valid save resumes it. File targets watch the selected source and the core
library. Directory targets also watch sibling files and local package sources. Use
`chibi --watch .` for a program that spans several files.

`Main` and `Frame` must finish synchronously; they cannot suspend or start tasks.
An edit applies between calls, so it cannot interrupt a busy `Main`, and a loop
written inside `Main` is not patched; put per-frame work in `Frame`. Without a
`Frame`, `--watch` keeps polling but has nothing to re-run. Press Ctrl-C to
stop. `--watch` applies to standalone programs, not game projects.

## Game projects

Game projects are archived until a Chibi renderer and player replace the C
ones (`archive/README.md`, "Game-project compile path"). `chibi build PROJECT`,
`chibi run PROJECT` and `chibi PROJECT` on a folder with `project.kdl` report
that game projects are archived. `chibi check` and `chibi fmt` were archived
with the C front end.

## Test packages

```sh
chibi test /path/to/packages
```

The folder contains package files, rather than game behaviors or libraries.
Tests import local packages with `project:NAME` and SDK packages with `core:NAME`.
See [Testing](testing.md) for a complete example.

## The CLI is a Chibi program

[apps/chibi/Main.chibi](../../apps/chibi/Main.chibi) is an ordinary package
entry point. The native [launcher](../../src/host/chibi_main.c) locates its
installed source and the SDK's core library, then starts it through the same
application host used by other programs. Argument parsing, command selection,
output, build freshness and watch policy live in the authored package.

From the engine checkout, run or test that package directly:

```sh
chibi apps/chibi -- version
chibi test apps/chibi
```

The nested CLI receives its own argument vector through
`application.arguments()`. `chibi test` runs the tests through
`chibi-compile --test`, on the authored compiler with the standalone native
catalog. The CLI and other applications use the same program API.

The [program service](../../src/authoring/program_service.h) has these operations:

| Operation | Result |
| --- | --- |
| `program.open(path, entry, arguments)` | An owned program handle with a copied argument vector. `entry` may be empty to require none. Source errors are reported by `update`. |
| `program.update(handle)` | `1` when a changed generation is accepted, `0` when unchanged, or `-1` for a new failure. |
| `program.call(handle)` | Calls the entry named at `open`. Separate `status` and `result` values. Status 0 means execution succeeded; result is the authored integer return, or 0 for no return value. |
| `program.call_entry(handle, name)` | The same, for a named zero-argument function of the accepted generation, such as `Main` or `Frame`. A missing name fails with a diagnostic. |
| `program.has(handle, name)` | Whether the accepted generation has a runnable function of that name. |
| `program.diagnostic(handle)` | The current source or runtime diagnostic. |
| `program.close(handle)` | Releases the session and its generations; a closed handle cannot name another program. |

An invalid update preserves the accepted program and retained state. A valid
update uses the runtime's existing replacement and migration rules before the
next call. Call failures and an authored nonzero return are separate results;
hosts can report a diagnostic without confusing it with the program's exit
value.

Nested calls share the caller's remaining instruction budget and cancellation
checks; repeated calls spend that same budget.

Native hosts can use the exported [authoring/program.h](../../src/authoring/program.h)
session API directly. It accepts package snapshots built from source buffers,
an explicit native catalog and service bindings, and reports source-range
diagnostics. A Studio integration can supply editor buffers through this
boundary and consume its diagnostics without launching a command process.

## Build native programs

```sh
chibi build --native --llvm /usr/lib/llvm-18/bin /path/to/program
```

`build --native` compiles a folder holding one `.chibi` source to a native
program with LLVM, without a C compiler. Its first function is the program's
entry and returns the exit status:

```chibi
package program

Main :: () -> int {
    return Twice(21)
}

Twice :: (value: int) -> int {
    return value * 2
}
```

`--llvm` names the folder of LLVM's `opt`, `llc` and `ld.lld`. Without it the
command reads `CHIBI_LLVM_BIN`. `-O2`, the default, optimizes; `-O0` does not.
`-g` adds DWARF line tables, as rustc's `-C debuginfo=line-tables-only` does,
so a debugger's backtrace or a profiler places the program's code at its
source lines at either level. A build with `-g` gets its own folder.
The program is for the system the command runs on, Linux x86-64 or macOS
arm64, unless `--target` picks another by its LLVM triple:
`x86_64-unknown-linux-gnu`, `aarch64-apple-darwin` or
`x86_64-pc-windows-msvc`. LLVM's own linker links each one on either system,
with no SDK of the other. On macOS, LLVM's tools come from Homebrew's `llvm`
and `lld`; link them into one folder for `--llvm`. The macOS program links
against a stub of `libSystem` that the build writes, as Zig's do, listing the C
functions the program calls, and is signed ad hoc. The Windows program,
`program.exe`, imports only `ExitProcess` from `kernel32.dll` through an
import library the build writes, as rustc's `raw-dylib` does. Each target gets
its own folder. Line tables are Linux x86-64 only, so `-g` with
another target fails.
The program and its receipt go to `build/native/O2-HASH` or `O0-HASH`, named
by a hash of the source, the SDK and the tools, and the command prints the
program's path. An unchanged build reuses its folder once its receipt still
names the tools and the program's bytes and the program is executable;
otherwise the command fails and asks you to remove the folder. A changed build
never replaces an earlier program. A source that native code cannot compile yet
fails with the position of its first unsupported form. Native code supports numbers, `bool`, records
and unions of them, control flow and calls between the source's own functions
so far. The engine repository's `docs/native-llvm-ledger.md` tracks the rest.

An interrupted build leaves its `.work` or `.partial` folder beside the
output. The next build refuses to start until you remove it, so it never
overwrites a build that is still running.

## Export a game

`chibi export` and `chibi web`, which assembled desktop and browser releases
with the player, are archived with game projects (`archive/README.md`,
"Native player, renderer and UI host").

## Check the SDK

```sh
chibi version
chibi doctor
```

`version` prints the SDK version, build identity, compiler and format versions.
`doctor` reports each checked capability as supported, unavailable or broken.
It changes nothing. It fails only when a checked capability is broken.

## Exit status

Each command accepts `--help`.

- 0 means success.
- 2 means the command could not start, such as an unknown option or missing project.
- Another nonzero status means the command failed.

## Platform limits

Linux and macOS support all commands. `build --native` builds programs for
Linux x86-64, macOS arm64 and Windows x64 on either.
