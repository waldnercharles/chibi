# Toolchain

Run `chibi` from an installed SDK or use `build/sdk/chibi` in an engine checkout.
The command finds the compiler, player and engine resources beside itself.

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
`project.kdl` runs its game instead.

With no path, `chibi run` finds the nearest `project.kdl` in or above the working
directory. If there is none, it runs the package in the working directory.

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

## Build and run

PROJECT is a project folder or its `project.kdl`. Without a path, these
commands find the nearest `project.kdl` in or above the working directory.

```sh
chibi check /path/to/game
chibi build /path/to/game
chibi run /path/to/game
```

`chibi /path/to/game` also runs a game folder containing `project.kdl`.

`check` checks the project without writing files. `build` writes
`.chibi/build/game.chibip`. `run` builds changed content and starts the player.
Pass player options after `--`. Use `chibi-player --help` to list them.

An older project must migrate before these commands can use it. The error
message gives the migration command. If a migration fails partway, the
manifest's `migrated` node records the version its sources reached, and the
next run continues from there. Fix the sources as they are; to start over from
old sources, restore `project.kdl` with them.

## Project files

A project has a `project.kdl` manifest and a content folder. This complete
example uses the installed SDK alone. Create a new disposable `my-game` folder
with the following files. Hold A or D to move the blue square.

### project.kdl

```kdl
version 39
name "My Game"
content "content"
start_scene "scenes/Start.kdl"
```

The start scene path is relative to the content folder. The manifest version
must match the project format printed by your chosen SDK's `chibi version`.
This example uses format 39. Use an SDK that supports that format.

### content/scenes/Start.kdl

```kdl
version 1
name "Start"
(array)entities {
    - {
        name "Player"
        prototype "Player"
        (array)behaviors "Move"
    }
    - name="View" prototype="View"
}
```

### content/prototypes/Player.kdl

```kdl
name "Player"
components {
    Appearance color=0x5b6ee1ff height=24.0 width=24.0
    (object)Transform
}
```

### content/prototypes/View.kdl

```kdl
name "View"
components {
    Camera height=180.0 width=320.0
    (object)Transform
}
```

### content/input.kdl

```kdl
Left {
    (array)sources "A"
}
Right {
    (array)sources "D"
}
```

### content/behaviors/Move.chibi

```chibi
Move :: behavior {
    on Tick :: (dt: number s) {
        if input.Left.held {
            self.Transform.x -= 120 * dt
        }
        if input.Right.held {
            self.Transform.x += 120 * dt
        }
    }
}
```

Choose an installed prefix explicitly to keep the compiler, player and SDK
resources together. `CHIBI_SDK` below is a shell variable for these commands;
choose any installed prefix rather than changing your normal `PATH`.
Record the SDK build identity and compiler bytes with the project:

```sh
CHIBI_SDK="${CHIBI_SDK:-$HOME/.local}"
"$CHIBI_SDK/bin/chibi" version > my-game/sdk-version.txt
shasum -a 256 "$CHIBI_SDK/bin/chibi" "$CHIBI_SDK/bin/chibi-compile" \
    "$CHIBI_SDK/bin/chibi-player" "$CHIBI_SDK/share/chibi/engine.json" \
    > my-game/sdk-sha256.txt
"$CHIBI_SDK/bin/chibi" doctor
"$CHIBI_SDK/bin/chibi" fmt --check my-game
"$CHIBI_SDK/bin/chibi" check my-game
"$CHIBI_SDK/bin/chibi" build my-game
"$CHIBI_SDK/bin/chibi" run my-game
```

Close the player to return to the shell. Behaviors, libraries and systems use
`.chibi`. Scenes, prototypes and materials use KDL.

## Format files

```sh
chibi fmt /path/to/game
chibi fmt --check /path/to/game
```

`fmt` rewrites sources and KDL assets in their canonical form. It keeps UI
markup unchanged. `--check` reports files that need formatting and writes nothing.

For a folder without `project.kdl`, `fmt` formats package sources instead.
Each source file must start with `package NAME`.

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
`--target` picks the program's system by its LLVM triple:
`x86_64-unknown-linux-gnu`, the default, `aarch64-apple-darwin` or
`x86_64-pc-windows-msvc`. LLVM's own linker links each one on Linux, with no
SDK of the other system. The macOS program links against a stub of
`libSystem`, as Zig's do, and is signed ad hoc. The Windows program,
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
fails with the position of its first unsupported form, and nothing falls back
to the player's compiled game. Native code supports numbers, `bool`, records
and unions of them, control flow and calls between the source's own functions
so far. The engine repository's `docs/native-llvm-ledger.md` tracks the rest.

An interrupted build leaves its `.work` or `.partial` folder beside the
output. The next build refuses to start until you remove it, so it never
overwrites a build that is still running.

## Export a game

Use an installed SDK to assemble a release:

```sh
chibi export /path/to/game --output /path/to/release
```

The output contains the player, compiled game and resources. It also contains
engine shaders, licenses and identity files. Add `--archive` to create a `.tar.gz`
archive beside the output.

Add `--native` to compile each library source (`Name :: library { ... }`)
with LLVM into `natives/<Name>.so`, so the player runs calls to its pure
functions natively. Pass `--llvm DIR`, the folder of `opt`, `llc` and `ld.lld`,
or set `CHIBI_LLVM_BIN`. A library the native compiler does not support yet stays
in the VM, and the export says why; a failed native build fails the export.
This works for Linux x86-64 folder exports only so far.

**Export replaces an earlier export, including files you added to it.** The
exporter uses its `.chibi-export` marker to identify its output. It refuses
to replace an existing folder without that marker.

### macOS

The output is `/path/to/release.app`, with an `Info.plist` and an ad hoc signature.
For the example above, export and launch the bundle from another directory:

```sh
"$CHIBI_SDK/bin/chibi" export my-game --output "$PWD/MyGame" --archive
APP="$PWD/MyGame.app"
(cd /tmp && "$APP/Contents/MacOS/My Game" --frames 120 --fixed-dt 0.016666667)
open "$APP"
```

The first launch exits after 120 frames; `open` starts the normal interactive
app. Move or copy the `.app` as one directory. It carries its own compiled game,
shaders and runtime resources and does not need the source project, SDK or an
engine build directory to run. Player options such as `--shader-dir DIR` remain
available for explicit overrides. A bounded hidden launch is useful for checking
Metal initialization and rendering, but it does not verify keyboard interaction:

```sh
(cd /tmp && "$APP/Contents/MacOS/My Game" --hidden --frames 2 \
    --size 320x180 --capture "$APP/../export-check.png")
```

The C tooling driver has an unattended regression for this path. From an engine
checkout, build the driver with the CPU image decoder, then run its existing
export selector through the process policy runner:

```sh
cmake -S test/tools -B build-export-test -DCMAKE_BUILD_TYPE=Debug \
    -DCHIBI_SDK_BUILD_COMPILER=ON
cmake --build build-export-test --parallel 2
python3 tools/test_policy.py --timeout 120 native \
    "$PWD/build-export-test/test_tools" --chibi-cli-export "$CHIBI_SDK/bin/chibi"
```

The selector checks the SDK identity and copies the installation into a fresh
scratch directory using copy-on-write cloning. It reads all six project files
above directly from this guide and checks them with that copy's compiler. It
runs two hidden 320x180 frames at a fixed timestep, exports an archive, extracts
and renames the app, and verifies its signature on macOS. It then removes the
original export and all scratch prerequisites, including the project, generated
`.chibi/build` tree and copied SDK. From an unrelated working directory it runs
the extracted game for two more hidden frames, without resource or shader
path overrides.

Both launches must exit successfully within 30 seconds, report two frames and
one simulation step, and produce the expected opaque black image with the
centered 24x24 blue square. C assertions decode and inspect every pixel, allowing
one byte of RGB rounding. A blank capture fails. The 120-second outer deadline
owns process cleanup; a timeout fails the check. Normal completion and assertion
failures remove the fixture. An outer timeout or signal can leave its
`/tmp/chibi-installed-*` directory for inspection and removal. The supplied SDK
and other user paths are never renamed or removed.

This selector requires a working native graphics context, but needs no visible
window, keyboard input or screen capture service. It stays separate from the
CPU-only tooling aggregate. It checks the idle scene and deployed resources;
it does not assert A/D input or movement, or removal of the caller's source,
SDK build tree or original installation. Use an SDK built from the source under
test and retain its `share/chibi/package.json` receipt with the test log. An older
installed SDK can only provide evidence for its recorded source revision.

The ad hoc signature supports local execution. It is not a Developer ID
signature or notarization for distribution to other Macs.

A project icon must be a square PNG. Supported sizes are 16, 32, 64, 128, 256,
512 and 1024 pixels.

### Linux

The output folder contains an executable named after the project and a desktop
entry. To install the entry:

1. Create a symlink to the executable in a directory on `PATH`.
2. Install the project icon in your icon theme with the name from the entry's `Icon` field.
3. Copy the desktop entry to `~/.local/share/applications`.

### Windows

The output folder contains an `.exe` and a `data` folder. A project icon must
be a square PNG of at most 256 pixels. The executable embeds the icon.
The SDK links the release C runtime statically, so the export needs no Visual
C++ redistributable.

## Export for browsers

Build the browser player from the engine checkout. This step needs Emscripten.
Browser compilation also needs Node.js.

```sh
make package-web-player
chibi export /path/to/game --web dist/web-player --output /path/to/site
python3 -m http.server -d /path/to/site
```

Open `http://localhost:8000`. The site contains the browser player and the same
game resources as a native export. It fetches those resources before the game
starts. Use a web server or static host. A `file://` URL does not work.

For a temporary local preview:

```sh
chibi web /path/to/game --player /path/to/dist/web-player
```

`web` serves on `http://127.0.0.1:8000/` until you press Ctrl+C. It then removes
the temporary export. Use `--port NUMBER` to select a port. Port 0 selects a
free port.

## Check the SDK

```sh
chibi version
chibi doctor
chibi doctor --web /path/to/dist/web-player
```

`version` prints the SDK version, build identity, compiler and format versions.
`doctor` reports each checked capability as supported, unavailable or broken.
It changes nothing. It fails only when a checked capability is broken.

## Generate documentation

```sh
chibi doc /path/to/repository
chibi doc /path/to/repository --serve
```

`doc` publishes the pages listed in `docs/site.kdl` to `build/docs`. The manifest
can select Markdown, package references and grammar rules. Other files stay private.
`--serve` previews the result locally. It accepts the same `--port` option as `web`.

Each `chibi` code block must parse. A complete package must also pass analysis.
A link to an unpublished file becomes plain text and produces a warning.

## Exit status

Each command accepts `--help`.

- 0 means success.
- 2 means the command could not start, such as an unknown option or missing project.
- Another nonzero status means the command failed.

## Platform limits

Linux and macOS support all commands, except `build --native`, which runs on
Linux only. It builds programs for Linux x86-64, macOS arm64 and Windows x64.

TODO: Windows support for `doc`.

TODO: Windows support for the local server in `web` and `doc --serve`.
