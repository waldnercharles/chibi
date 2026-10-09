# Chibi

A C23 scripting/compiler/runtime SDK with a separate Studio authoring application.

The [architecture reference](docs/architecture.md) describes package boundaries,
source authority, runtime lifetimes and deployment contracts.

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
make prepare-project PROJECT_ROOT=/absolute/game ARTIFACT=/absolute/output/game.chibip
make player-run ARTIFACT=/absolute/output/game.chibip RESOURCE_ROOT=/absolute/resources
make controller-run PROJECT_ROOT=/absolute/game
```

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

The serial web SDK package and external Studio build/link passed. Browser
execution remains unverified; those checks do not establish interactive acceptance.
`make web-serve` and `make web-threads-serve` use the external static server with
isolation headers. It serves the installed browser bundle without mounting game
source from disk. Publication integration remains pending and rejects explicitly.
Historical publishing scripts and measurements do not certify this new product.
No publishing is part of the extraction work.

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
