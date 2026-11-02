# Toolchain

Run `chibi` from an installed SDK or use `build/sdk/chibi` in an engine checkout.
The command finds core and engine resources beside itself. Programs compile
with LLVM/LLD 22 or newer. Pass `--llvm DIR` or set `CHIBI_LLVM_BIN` to the
directory containing `opt`, `llc` and the platform linker.

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

Read command-line arguments with `core:os`. Its `Arguments()` result contains
exactly the arguments passed after `--`, with the first at index 0:

```chibi
package hello

import os "core:os"

Main :: () {
    arguments := os.Arguments()
    if length(arguments) > 0 {
        print_string(arguments[0])
    }
}
```

Run `chibi Hello.chibi -- Ada` to print `Ada`. `Main` takes no parameters;
`--` separates program arguments from command options.

Every source file must start with `package NAME`. Running a file compiles the selected file and the packages it imports, so
unrelated sibling files do not affect it. It can import SDK packages with `core:NAME`.

When a program spans several files, run its package directory:

```sh
chibi .
```

This includes the package's sibling source files and supports local package
imports with `project:NAME`, alongside `core:NAME` imports. A directory with
`project.kdl` is a game project; see [Game projects](#game-projects).

With no path, `chibi run` runs the package in the working directory.

## Reload a program

On Linux x86-64, add `--watch` to reload a program between calls to `Frame`:

```sh
chibi --watch Reload.chibi
chibi run --watch Reload.chibi
chibi Reload.chibi --watch
```

`Main` runs once. Native watched programs provide zero-argument `Main` and
`Frame` functions returning `int`. The host calls `Frame` about 60 times a
second. Return 0 to continue, or a nonzero exit status to stop. Use `retain`
for a value that should survive edits and calls. Save this as `Reload.chibi`:

```chibi
package reload

Main :: () -> int {
    print_string("started")
    return 0
}

Frame :: () -> int {
    ticks: retain int = 0
    ticks += 1
    if ticks % 60 == 0 {
        print_string(format_text("tick {}", ticks))
    }
    return 0
}
```

Each accepted save replaces the running code between frames. `Main` is not called
again, the next `Frame` runs the new body, and `ticks` keeps counting: retained
values stay (converted when their type changed), ordinary locals start fresh each
call. Change `"tick {}"` to `"tock {}"` and save to see it. See
[Edit and Continue](../edit-and-continue.md) for what is patched when.

A syntax or type error prints its diagnostic and keeps the last valid code
running; the next valid save applies. A runtime failure ends the program with
a failure status. File targets watch the selected source and the core
library. Directory targets also watch sibling files and local package sources. Use
`chibi --watch .` for a program that spans several files.

`Main` and `Frame` must finish synchronously. They can start tasks; task
continuations keep the generation whose code they run until they finish.
An edit applies between calls, so it cannot interrupt a busy `Main`, and a loop
written inside `Main` is not patched; put per-frame work in `Frame`. Press
Ctrl-C to stop. `--watch` applies to standalone programs, not game projects.

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

## The native CLI

The compiler and CLI are authored in
[tools/compiler/native_driver](../../tools/compiler/native_driver). The installed
`chibi` executable compiles programs directly and invokes LLVM's tools. Build,
run and test use native executables; no VM host or bytecode fallback is involved.

The engine's runtime APIs and browser path still use VM execution. See the
[native ledger](../native-llvm-ledger.md) for supported native source forms and
remaining runtime coverage.

A project can provide an ordinary `Build.chibi` with a zero-argument `Main`
returning nothing or an `int` exit status. `chibi build` finds the nearest
`Build.chibi` in the working directory or its parents, compiles it through the
native content cache, and passes the build arguments through `core:os.Arguments`.
An engine checkout uses its authored toolchain driver instead.

## Build native programs

```sh
chibi build --native --llvm /usr/lib/llvm-22/bin /path/to/program
```

`build --native` compiles a source file or package folder to a native program
with LLVM. Its `Main` is the entry and returns the exit status:

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
`program.exe`, uses import libraries the build writes for its declared system
imports. Each target gets
its own folder. Line tables are Linux x86-64 only, so `-g` with
another target fails.
The program and its receipt go to `build/native/O2-HASH` or `O0-HASH`, named
by a hash of the source, the SDK and the tools, and the command prints the
program's path. An unchanged build reuses its folder once its receipt still
names the tools and the program's bytes and the program is executable;
otherwise the command fails and asks you to remove the folder. A changed build
never replaces an earlier program. A source that native code cannot compile yet
fails with the position of its first unsupported form. The engine repository's
`docs/native-llvm-ledger.md` tracks native language support.

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
