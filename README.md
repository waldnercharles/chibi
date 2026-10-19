# Chibi

A C23 scripting/compiler/runtime SDK with a separate Studio authoring application.

The [architecture reference](docs/architecture.md) describes package boundaries,
source authority, runtime lifetimes and deployment contracts.
For implemented capabilities, remaining work, task ownership and the limits of
the available validation, start at [current status](docs/status.md).
To see a feature working, open one of the projects listed in
[examples](docs/examples.md) in Studio.

[Native product packages](docs/native-package.md) provide an installed compiler
SDK, a separate Studio application, and a deployed player. Game repositories own
authored content; the player consumes prepared artifacts and their declared
resources. See [runtime authoring](docs/runtime-authoring.md) for exact SDK pins
and installation into registered game workspaces.

```sh
make package-sdk       # native compiler + desktop authoring SDK in dist/sdk
make package-player    # compiler-free player deployment in dist/chibi-player-<os>-<arch>
dist/sdk/bin/chibi-compile --project-root /absolute/game --output /absolute/output/game.chibip
/installed/player/bin/chibi-player /absolute/output/game.chibip --resource-root /absolute/output
```

To play a game while developing it, put `chibi` on your `PATH` and run it from the
game's directory or with the game's directory or `project.kdl`:

```sh
make build
ln -sf "$PWD/build/sdk/chibi" ~/.local/bin/chibi
chibi ~/games/neon-swarm            # or: cd ~/games/neon-swarm && chibi
```

`chibi` prepares the project into `.chibi/build/game.chibip` when the project's
files or the compiler changed since the last run, then starts `chibi-player`
beside it. Options after the project go to the player. Studio's `chibi-dev`
opens a project the same way.

To use `chibi` and Studio in any project without a checkout's build folders,
install the SDK, then Studio from its checkout:

```sh
make install                    # into ~/.local; PREFIX=/elsewhere to change it
make -C ../chibi-studio install # the same PREFIX
cd ~/games/neon-swarm && chibi-dev
```

`make install` builds a Release SDK in its own build folder and installs
`chibi`, `chibi-compile` and `chibi-player` into `PREFIX/bin`, their shaders and
licenses into `PREFIX/share/chibi`, and the libraries, headers and CMake package
that Studio and other consumers build against. Add `PREFIX/bin` to `PATH` if it
is not there already; macOS does not include `~/.local/bin`. The executables
find their resources relative to themselves. To uninstall, delete the files
CMake lists in `build-install-sdk/install_manifest.txt` and
`PREFIX/share/chibi/engine.json`. On Windows, which has no make wrappers, run
the target's commands directly.

`chibi version` prints the SDK version, the build ID of a packaged SDK such as
`make install` installs (`unpackaged` for a build tree or a plain
`cmake --install`), the compiler it runs, the project format and each prepared
artifact format by name. It reads them from `share/chibi/engine.json`, which
configuring generates and SDK packaging completes with the build ID.

`chibi doctor` checks, without changing anything, that the SDK's identity
document is valid, that its compiler and player run their `--help` (on Windows,
that they exist), whether it was built for this platform, and whether it has
the native SLJIT backend. `chibi doctor --web
PLAYER` also checks a browser player folder and the `node` that runs its
compiler. Each line says supported, unavailable or broken, and the command
fails only when something it checked is broken.

`chibi check [PROJECT]` parses, resolves and validates a project as
`chibi build` does, with the same compiler, but migrates and writes nothing. An
older project fails with the command that migrates it. It exits 0 for a valid
project and nonzero otherwise, so it suits a pre-commit hook or a project's CI.

`chibi test [ROOT]` runs the `@test` functions (see
[the language guide](docs/code-language.md#tests-and-attributes)) of every
package below ROOT, the working directory by default. ROOT is their `project:`
collection and holds only package files, so a game project's behaviors and
libraries are not tests; imports of `core:` use the SDK's collection. It prints
`PASS` or `FAIL` with each test's package and name, where a failed test
stopped, and a count, and fails when a test fails or a package is invalid.
Each test runs alone on the native backend, or the bytecode interpreter
without one, for at most 100 million instructions. It may call the natives
that need no running game: core, text, arrays, vectors, math and value
operations. A test that waits or starts a task fails.

To ship a game, export it with an installed SDK's `chibi`:

```sh
dist/sdk/bin/chibi export ~/games/neon-swarm --output ~/builds/NeonSwarm
~/builds/NeonSwarm/"Neon Swarm"     # runs the exported game
```

The folder holds the player, named after the project, and `data/` with the
compiled game and its resources. `data/engine/` holds the shaders, licenses and
engine identity. Exporting again replaces an earlier export, including files
added to it. The exporter recognizes its own output by the empty
`.chibi-export` it writes beside the compiled game; any other existing folder
is left alone. `--archive` also writes
`NeonSwarm.tar.gz` (`NeonSwarm.app.tar.gz` on macOS).

On macOS the export is an app, `NeonSwarm.app`, with an `Info.plist`, the
project icon as an `.icns`, and an ad hoc signature. Its icon must be a square
PNG of 16, 32, 64, 128, 256, 512 or 1024 pixels. Elsewhere the folder also holds
a desktop entry, `neon-swarm.desktop`, which runs `Neon Swarm` from `PATH` with
the `neon-swarm` theme icon. To install it, symlink the folder's executable into
a directory on `PATH`, put the project icon in your icon theme as
`neon-swarm.png`, and the entry in `~/.local/share/applications`.

On Windows the folder holds `Neon Swarm.exe` beside `data/`, with the project
icon, which must be a square PNG of at most 256 pixels, in the executable's
resources. The SDK and its dependencies link the release C runtime statically
in every configuration, so the folder runs without the Visual C++
redistributable.

For browsers, export a static site with the browser player, which
`make package-web-player` builds with Emscripten into `dist/web-player`:

```sh
make package-web-player
dist/sdk/bin/chibi export ~/games/neon-swarm --web dist/web-player --output ~/builds/neon-swarm-web
python3 -m http.server -d ~/builds/neon-swarm-web    # play at http://localhost:8000
```

The site holds `index.html`, titled with the project's title, the player's
`chibi-player.js` and `chibi-player.wasm`, and the same `data/` folder, which
`files.json` lists. The page fetches those files before the game starts,
so it needs a web server rather than a `file://` URL. Any static host works.
[`docs/game-web-workflow.yml`](docs/game-web-workflow.yml) builds and publishes a
game repository on GitHub Pages; copy it to the game's `.github/workflows/web.yml`.

Both SDK product builds prepare a standalone host reflection tool. `SDK_PREFIX`,
`RUNTIME_SDK_PREFIX`, and `PLAYER_OUTPUT` select their outputs. Packaging leaves
registered games untouched; `make update-game-runtime` explicitly verifies and
installs the player package. Studio is built and packaged separately against a
pinned installed SDK.

## Development

The engine checkout builds the compiler, player, controller and package-consumer
checks. Studio builds in its separate repository against an installed SDK.

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

`STUDIO_SOURCE` defaults to the `chibi-studio` checkout beside the engine's.
`studio-build` uses Ninja and `STUDIO_CMAKE_FLAGS` (Debug by default).
`studio-dev` restores `STUDIO_CHECKPOINT`, defaulting to the Studio build's
`workspace.json`. Source edits remain owned by the game; native changes require
rebuilding the owning SDK or Studio product and restarting. Studio's SDK is an
explicit pin: its convenience build never adds the engine checkout to include
paths or silently rebuilds that installed SDK. Package Studio in a separate
Release build with the SDK manifest captured at configuration.

`BUILD_TARGET` selects root CMake products. Defaults are `chibi-compile`,
`chibi-player`, and `chibi_ctl`; the controller is in `build/bin`, compiler and
player in `build/sdk`. `make controller-build` builds only the authoring tool.
`make help` lists supported wrappers. The old combined `chibi` application and
root Studio launch targets are retired.

Native builds need CMake, Ninja for Studio, a C23 compiler, and libclang for
reflection. CMake pins dependencies. `NATIVE_TOOLCHAIN_FLAGS`, `LIBCLANG_ROOT`,
and `JOBS` select the toolchain, matching reflection installation, and compile
parallelism. Builds print phase summaries with full logs in `.chibi/logs`;
`VERBOSE=1` streams output. SDK product builds use a standalone reflection tool.

Windows builds use the LLVM installer's clang, which targets the Microsoft C
runtime and finds Visual Studio's Windows SDK, with CMake and Ninja. The make
wrappers need a POSIX shell, so configure directly:

```sh
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=OFF \
    -DLIBCLANG_ROOT="C:/Program Files/LLVM"
cmake --build build --target chibi chibi-compile chibi-player chibi_ctl
```

Reflection loads `libclang.dll`, so LLVM's `bin` folder must be on `PATH`. C
sources that link `Chibi::support` get the POSIX calls the Microsoft runtime
lacks from `src/support/win32`. The C tests still need POSIX, so Windows builds
skip them. Executables embed `src/host/windows/chibi.manifest`, which makes the
runtime's file names UTF-8; installed consumers add
`share/chibi/windows/chibi.rc` to theirs.

## Browser Studio

Browser Studio uses an installed Emscripten SDK containing the same owning
compiler/runtime/renderer packages. That SDK is library-only; native command
line products are built separately.

```sh
make web STUDIO_SOURCE=/absolute/chibi-studio
make web-threads STUDIO_SOURCE=/absolute/chibi-studio WEB_WORKERS=4
```

These recipes configure/package the web SDK, build the external `chibi-studio`
consumer, and install its HTML/JS/WASM/data into `WEB_OUTPUT`. Serial and threaded
profiles use separate default build and install directories. The threaded SDK
exports its worker ABI; Studio consumes it rather than overriding it.
`WEB_PROJECTS` supplies an explicit semicolon-separated list of sample folders;
Studio owns its empty starter and authored UI. No engine repository games are
implicitly shipped.

`make web-serve` and `make web-threads-serve` use the external static server with
isolation headers. It serves the installed browser bundle without mounting game
source from disk. Browser publication is implemented: the engine's
[release workflow](.github/workflows/publish.yml) supplies versioned packages,
[Studio](https://github.com/waldnercharles/chibi-studio#automatic-browser-distribution)
publishes the serial editor archive, and the
[game web workflow](docs/game-web-workflow.yml) hosts a game's player and
revision-specific `/dev/` workspace. Successful publication does not establish
edit/save/reload, input or export acceptance in a browser. The
[serial browser report](https://github.com/waldnercharles/chibi-studio/blob/main/tests/acceptance/BROWSER.md)
records those interactions on named revisions. See
[current execution evidence and remaining work](docs/status.md#execution-evidence)
for that distinction; threaded browser acceptance remains separate.
The old local `make publish-web` wrapper remains unavailable; use the owning
repository's publication workflow.

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

Every test command has a hard 60-second deadline. Full/unmeasured suites remain
quarantined; the retired `test_suite` wrappers reject rather than reconstructing
old aggregates. Builds/configuration are timed separately. Public consumer checks
and GPU routes run only with an admitted selector; use the policy inventory for
current coverage. A passing package check establishes integrity, not rendered
acceptance. See [testing policy](docs/testing.md#temporary-60-second-policy).

Author behaviors, libraries, and systems in `.chibi`; declarative assets use KDL.
Studio projections and optional layout metadata do not replace executable source.
Shared navigation, refactoring and inspection ship in SDK authoring packages and
are consumed by Studio and the controller. Format/validate authored content
through the controller; see [runtime authoring](docs/runtime-authoring.md).

## Code map

| Location | Responsibility |
| --- | --- |
| `sdk` | Independently built and installed target/header contracts |
| `src/language` | Owned syntax snapshots, semantic analysis and direct source lowering |
| `src/execution` | Executable IR, VM/SLJIT/C backends, prepared modules and instances |
| `src/capabilities` | Shared typed native signatures, stable identities and effects |
| `src/authoring` | Preparation, source navigation/refactoring and presentation adapters |
| `src/runtime` | ECS-backed behavior, system, scene and optional native capabilities |
| `src/ecs` | Entity storage, reflected component layout and queries |
| `src/types` | Reflected values, catalogs, ownership and portable wire records |
| `src/assets` | Authored declarative documents and preparation adapters |
| `src/graph` | Authoring graph model, catalog and shader/model compilation |
| `src/render` | Prepared scene/resource rendering and inspection |
| `src/host` | Separate compiler and player entrypoints |
| `tools/chibi_ctl` | Controller consuming public owning packages |
| `tools/reflect` | Standalone native reflection generator |
| `test` | Bounded package consumers and focused fixture content |
| separate `chibi-studio` repository | Editor, application loop, UI, text bridge and browser host |

Public headers are owned by their installed targets. Private headers stay with
the owning implementation; consumers never add the engine `src` root.

See [the implementation tracker](PLAN.md) and
[the native development workflow](docs/reload.md) for the acceptance checks and lifetime rules.
