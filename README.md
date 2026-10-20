# Chibi

Chibi is a C23 SDK for scripting, compilation and runtime. Studio is a separate
application for authoring.

Start with the [language reference](docs/code-language.md) and the
[toolchain guide](docs/guide/toolchain.md). To see a feature work, open one of
the projects in [examples](docs/examples.md) in Studio.

Studio also runs in the browser at
[waldnercharles.github.io/chibi-studio](https://waldnercharles.github.io/chibi-studio/).
That version is built from the latest engine release.

## Products

The native product packages supply three items:

- an installed compiler SDK
- a separate Studio application
- a deployed player

Game repositories own the authored content. The player reads prepared artifacts
and the resources that they declare.

```sh
make package-sdk       # native compiler + desktop authoring SDK in dist/sdk
make package-player    # compiler-free player deployment in dist/chibi-player-<os>-<arch>
dist/sdk/bin/chibi-compile --project-root /absolute/game --output /absolute/output/game.chibip
/installed/player/bin/chibi-player /absolute/output/game.chibip --resource-root /absolute/output
```

## Play a game while you develop it

1. Put `chibi` on your `PATH`.
2. Run `chibi run` in the directory of the game. Alternatively, give the game
   directory or its `project.kdl` to `chibi run`.

```sh
make build
ln -sf "$PWD/build/sdk/chibi" ~/.local/bin/chibi
chibi run ~/games/neon-swarm        # or: cd ~/games/neon-swarm && chibi run
```

`chibi run` prepares the project into `.chibi/build/game.chibip` if the files
of the project or the compiler changed since the last run. Then it starts
`chibi-player` beside it.

The options after `--` go to the player. The `chibi-dev` command of Studio
opens a project in the same way.

## Install the SDK and Studio

You can use `chibi` and Studio in any project without the build folders of a
checkout. Install the SDK, then install Studio from its checkout:

```sh
make install                    # into ~/.local; PREFIX=/elsewhere to change it
make -C ../chibi-studio install # the same PREFIX
cd ~/games/neon-swarm && chibi-dev
```

`make install` builds a Release SDK in its own build folder. Then it installs
these items:

- `chibi`, `chibi-compile` and `chibi-player` into `PREFIX/bin`
- their shaders and licenses into `PREFIX/share/chibi`
- the libraries, headers and CMake package that Studio and other consumers
  build against

If `PREFIX/bin` is not on `PATH`, add it. macOS does not include
`~/.local/bin`. The executables find their resources relative to their own
location.

To uninstall, delete these files:

- the files that CMake lists in `build-install-sdk/install_manifest.txt`
- `PREFIX/share/chibi/engine.json`

Windows has no make wrappers. On Windows, run the commands of the target
directly.

## Commands

### chibi version

`chibi version` prints these items:

- the SDK version
- the build ID of a packaged SDK, such as the SDK that `make install` installs
  (`unpackaged` for a build tree or a plain `cmake --install`)
- the compiler that it runs
- the project format
- each prepared artifact format, by name

It reads them from `share/chibi/engine.json`. Configuration generates this
file. SDK packaging adds the build ID to it.

### chibi doctor

`chibi doctor` checks these items and changes nothing:

- The identity document of the SDK is valid.
- The compiler and the player run their `--help`. On Windows, it checks only
  that they exist.
- The SDK was built for this platform.
- The SDK has the native SLJIT backend.

`chibi doctor --web PLAYER` also checks a browser player folder and the `node`
that runs its compiler.

Each line says supported, unavailable or broken. The command fails only when
an item that it checked is broken.

### chibi check

`chibi check [PROJECT]` parses, resolves and validates a project in the same
way as `chibi build`. It uses the same compiler. But it does not migrate or
write anything.

An older project fails, and the message gives the command that migrates it.
The exit status is 0 for a valid project and nonzero for a project that is not
valid. Thus you can use the command in a pre-commit hook or in the CI of a
project.

### chibi test

`chibi test [ROOT]` runs the `@test` functions of each package below ROOT.
Refer to [the language guide](docs/code-language.md#tests-and-attributes).

- ROOT is the working directory by default.
- ROOT is the `project:` collection of the packages. It contains only package
  files. Thus the behaviors and libraries of a game project are not tests.
- Imports of `core:` use the collection of the SDK.

For each test, the command prints `PASS` or `FAIL` with the package and the
name. For a failed test, it prints where the test stopped. At the end, it
prints a count. The command fails when a test fails or a package is not valid.

Each test runs alone for a maximum of 100 million instructions. It runs on the
native backend. Without a native backend, it runs on the bytecode interpreter.

A test can call the natives that do not need a running game: core, text,
arrays, vectors, math and value operations. A test that waits or starts a task
fails.

### chibi fmt

`chibi fmt [--check] [PATH...]` rewrites the authored assets of each project in
the form that their savers write. Without a PATH, it uses the project in or
above the working directory. It rewrites these assets:

- behaviors, libraries, systems and component sources
- the KDL scenes, prototypes, graphs, editor documents, scenarios and input map

The command keeps UI markup as it is.

A PATH can be a folder without a `project.kdl`. In that case, the command
formats the package sources below it. These are the files that `chibi test`
reads. Each file must start with `package NAME`. If a file does not, the
command writes nothing.

With `--check`, the command writes nothing. It names each file that is not
canonical and fails if there is one.

Like `check`, the command does not migrate or format an older project.
`chibi_ctl --format` runs the same formatter.

## Export a game

To ship a game, use the `chibi` of an installed SDK to export it:

```sh
dist/sdk/bin/chibi export ~/games/neon-swarm --output ~/builds/NeonSwarm
~/builds/NeonSwarm/"Neon Swarm"     # runs the exported game
```

The output folder contains these items:

- the player, with the name of the project
- `data/`, with the compiled game and its resources
- `data/engine/`, with the shaders, licenses and engine identity

When you export again, the new export replaces the earlier export. This
includes the files that you added to it.

The exporter writes an empty `.chibi-export` file beside the compiled game. It
uses this file to identify its own output. It does not change a different
folder that exists.

`--archive` also writes `NeonSwarm.tar.gz` (`NeonSwarm.app.tar.gz` on macOS).

### macOS

The export is an app, `NeonSwarm.app`. The app has an `Info.plist`, the
project icon as an `.icns`, and an ad hoc signature. The icon must be a square
PNG of 16, 32, 64, 128, 256, 512 or 1024 pixels.

### Linux and other platforms

On platforms other than macOS and Windows, the folder also contains a desktop
entry, `neon-swarm.desktop`. The entry runs `Neon Swarm` from `PATH` with the
`neon-swarm` theme icon.

To install the export:

1. Make a symlink to the executable of the folder in a directory on `PATH`.
2. Put the project icon in your icon theme as `neon-swarm.png`.
3. Put the entry in `~/.local/share/applications`.

### Windows

The folder contains `Neon Swarm.exe` beside `data/`. The project icon is in
the resources of the executable. The icon must be a square PNG of a maximum of
256 pixels.

The SDK and its dependencies link the release C runtime statically in all
configurations. Thus the folder runs without the Visual C++ redistributable.

### Browsers

Export a static site with the browser player. `make package-web-player` uses
Emscripten to build the browser player into `dist/web-player`:

```sh
make package-web-player
dist/sdk/bin/chibi export ~/games/neon-swarm --web dist/web-player --output ~/builds/neon-swarm-web
python3 -m http.server -d ~/builds/neon-swarm-web    # play at http://localhost:8000
```

The site contains these items:

- `index.html`, with the title of the project
- the `chibi-player.js` and `chibi-player.wasm` of the player
- the same `data/` folder, which `files.json` lists

The page fetches those files before the game starts. Thus it needs a web
server. A `file://` URL does not work.

Any static host works. A game repository can publish on GitHub Pages with the
game web workflow. This workflow builds the site from published engine and
editor packages.

## Serve a browser build

While you develop, `chibi web` does the export and the serving:

1. It exports the site into a temporary folder of its own.
2. It serves the folder like a static host at `http://127.0.0.1:8000/` until
   you press Ctrl+C.
3. It deletes the folder.

```sh
dist/sdk/bin/chibi web ~/games/neon-swarm --player dist/web-player
```

`--port` selects a different port. Port 0 selects any free port. The command
serves only on this computer. It does not operate on Windows yet.

## Documentation site

`chibi doc [ROOT]` writes the documentation site that `ROOT/docs/site.kdl`
lists into `ROOT/build/docs`. ROOT is the working directory by default.

The manifest names each published page. Thus no other file in `docs/` goes
into the site. A page is one of these:

- a Markdown `source`
- the generated reference of the `packages` in a folder, with their import
  `collection`
- the generated rules of a Tree-sitter `grammar`

A link to a file that is not published keeps only its text. The command reports
each such link as
`SOURCE:LINE: link to TARGET is not published; written as text`. This report
does not make the build fail.

Each example that has the `chibi` fence must parse. An example that is a full
package must also analyze. If not, the build fails at the line of the example.

`--serve` then serves the site at `http://127.0.0.1:8000/` until you press
Ctrl+C. `--port` operates as for `web`. In this checkout,
`build/sdk/chibi doc --serve` shows a preview of the documentation of the
engine.

## SDK product builds

The two SDK product builds prepare a standalone host reflection tool.
`SDK_PREFIX`, `RUNTIME_SDK_PREFIX` and `PLAYER_OUTPUT` select their outputs.

Packaging does not change registered games. `make update-game-runtime`
verifies and installs the player package. You must run it explicitly.

Studio is built and packaged separately against a pinned installed SDK.

## Development

The engine checkout builds the compiler, the player, the controller and the
package-consumer checks. Studio builds in its separate repository against an
installed SDK.

```sh
make build             # sanitizer-enabled compiler/player/controller development build
make build-debug       # plain Debug products
make build-release     # Release products
make package-sdk       # compiler/native/desktop SDK, including chibi-compile
make package-player    # compiler-free deployment package
make studio-build STUDIO_SOURCE=/absolute/chibi-studio STUDIO_SDK_PREFIX=/absolute/sdk
make studio-run STUDIO_SOURCE=/absolute/chibi-studio STUDIO_SDK_PREFIX=/absolute/sdk PROJECT_ROOT=/absolute/game
make studio-dev STUDIO_SOURCE=/absolute/chibi-studio STUDIO_SDK_PREFIX=/absolute/sdk PROJECT_ROOT=/absolute/game
make package-studio STUDIO_SOURCE=/absolute/chibi-studio STUDIO_SDK_PREFIX=/absolute/sdk
make install [PREFIX=/absolute/prefix]
make prepare-project PROJECT_ROOT=/absolute/game ARTIFACT=/absolute/output/game.chibip
make player-run ARTIFACT=/absolute/output/game.chibip RESOURCE_ROOT=/absolute/resources
make controller-run PROJECT_ROOT=/absolute/game
```

### Studio builds

| Variable or target | Behavior |
| --- | --- |
| `STUDIO_SOURCE` | The default is the `chibi-studio` checkout beside the engine checkout. |
| `studio-build` | Uses Ninja and `STUDIO_CMAKE_FLAGS`. The default build type is Debug. |
| `studio-dev` | Restores `STUDIO_CHECKPOINT`. The default is the `workspace.json` of the Studio build. |

The game owns the source edits. A native change needs a new build of the SDK
or Studio product that owns it, and a restart.

The SDK of Studio is an explicit pin. The convenience build of Studio never
adds the engine checkout to the include paths. It never rebuilds that installed
SDK silently.

Package Studio in a separate Release build. That build records the SDK
manifest at configuration.

### Build targets

`BUILD_TARGET` selects root CMake products. The defaults are `chibi-compile`,
`chibi-player` and `chibi_ctl`.

- The controller is in `build/bin`.
- The compiler and the player are in `build/sdk`.
- `make controller-build` builds only the authoring tool.
- `make help` lists the supported wrappers.

The old combined `chibi` application and the root Studio launch targets are
retired.

### Requirements

Native builds need CMake, Ninja for Studio, a C23 compiler and libclang for
reflection. CMake pins the dependencies. These variables select the build:

- `NATIVE_TOOLCHAIN_FLAGS` selects the toolchain.
- `LIBCLANG_ROOT` selects the reflection installation that matches it.
- `JOBS` selects the compile parallelism.

Builds print phase summaries. The full logs are in `.chibi/logs`. `VERBOSE=1`
streams the output. SDK product builds use a standalone reflection tool.

### Windows builds

Windows builds use the clang of the LLVM installer, with CMake and Ninja. This
clang targets the Microsoft C runtime and finds the Windows SDK of Visual
Studio.

The make wrappers need a POSIX shell. Thus you must configure directly:

```sh
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=OFF \
    -DLIBCLANG_ROOT="C:/Program Files/LLVM"
cmake --build build --target chibi chibi-compile chibi-player chibi_ctl
```

- Reflection loads `libclang.dll`. Thus the `bin` folder of LLVM must be on
  `PATH`.
- The Microsoft runtime does not have some POSIX calls. C sources that link
  `Chibi::support` get these calls from `src/support/win32`.
- The C tests still need POSIX. Thus Windows builds skip them.
- Executables embed `src/host/windows/chibi.manifest`. This manifest makes the
  file names of the runtime UTF-8. Installed consumers add
  `share/chibi/windows/chibi.rc` to their executables.

## Browser Studio

Browser Studio uses an installed Emscripten SDK. This SDK contains the same
compiler, runtime and renderer packages that own those parts. It contains only
libraries. The native command line products are built separately.

```sh
make web STUDIO_SOURCE=/absolute/chibi-studio
make web-threads STUDIO_SOURCE=/absolute/chibi-studio WEB_WORKERS=4
```

These recipes do these steps:

1. Configure and package the web SDK.
2. Build the external `chibi-studio` consumer.
3. Install its HTML, JS, WASM and data into `WEB_OUTPUT`.

The serial and threaded profiles use separate default build and install
directories. The threaded SDK exports its worker ABI. Studio uses this ABI and
does not override it.

`WEB_PROJECTS` supplies an explicit list of sample folders, separated by
semicolons. Studio owns its empty starter and its authored UI. The build does
not ship the games of the engine repository implicitly.

`make web-serve` and `make web-threads-serve` use the external static server
with isolation headers. It serves the installed browser bundle. It does not
mount game source from disk.

### Publication

Browser publication is implemented:

- The release workflow of the engine supplies versioned packages.
- [Studio](https://github.com/waldnercharles/chibi-studio#automatic-browser-distribution)
  publishes the serial editor archive.
- The game web workflow hosts the player of a game and a `/dev/` workspace
  for each revision.

A successful publication does not prove that edit, save, reload, input or
export operate correctly in a browser. The
[serial browser report](https://github.com/waldnercharles/chibi-studio/blob/main/tests/acceptance/BROWSER.md)
records those interactions on named revisions. Threaded browser acceptance is
separate.

Publication belongs to those workflows. The engine has no local publisher.

## Checks and authoring

```sh
make test              # six pure C execution-policy checks
make test-tools        # C tooling/process cases under one shared deadline
make language-build
make test-language-execution
make test-language-program
make test-language-prepared
make test-list
make format
make check-format
```

### Test deadlines

Each test command has a hard deadline of 60 seconds.

- Full suites and suites that are not measured stay in quarantine.
- The retired `test_suite` wrappers reject a request. They do not build the
  old aggregates again.
- Builds and configuration have separate times.
- Public consumer checks and GPU routes run only with an admitted selector.

For the current coverage, use the policy inventory. A package check that
passes proves integrity. It does not prove rendered acceptance.

### Authoring

Author behaviors, libraries and systems in `.chibi`. Declarative assets use
KDL. Studio projections and optional layout metadata do not replace executable
source.

Shared navigation, refactoring and inspection ship in SDK authoring packages.
Studio and the controller use them.

Format authored content with `chibi fmt`. Validate it with `chibi check`. The
controller supplies the same operations.

## Code map

| Location | Responsibility |
| --- | --- |
| `sdk` | Target and header contracts that are built and installed independently |
| `src/language` | Owned syntax snapshots, semantic analysis and direct source lowering |
| `src/execution` | Executable IR, VM/SLJIT/C backends, prepared modules and instances |
| `src/capabilities` | Shared typed native signatures, stable identities and effects |
| `src/authoring` | Preparation, source navigation and refactoring, and presentation adapters |
| `src/runtime` | Behavior, system and scene support on the ECS, and optional native capabilities |
| `src/ecs` | Entity storage, reflected component layout and queries |
| `src/types` | Reflected values, catalogs, ownership and portable wire records |
| `src/assets` | Authored declarative documents and preparation adapters |
| `src/graph` | Authoring graph model, catalog, and shader and model compilation |
| `src/render` | Rendering and inspection of prepared scenes and resources |
| `src/host` | Separate compiler and player entry points |
| `tools/chibi_ctl` | Controller that uses the public owning packages |
| `tools/reflect` | Standalone native reflection generator |
| `test` | Bounded package consumers and focused fixture content |
| separate `chibi-studio` repository | Editor, application loop, UI, text bridge and browser host |

The installed targets own the public headers. Private headers stay with the
implementation that owns them. Consumers never add the `src` root of the
engine.
