# Chibi

A C23 visual programming editor for authored game behaviors, prototypes, and scenes.

[Native engine packages](docs/native-package.md) let a separate game workspace
author assets through Studio and MCP using prebuilt executables.

The tracked `projects/arena2d` sample is the default project. External games can
use copied prebuilt executables in their ignored `.chibi/runtime` directory,
and can optionally be mounted at the ignored `projects/local-game` path for
Studio and browser authoring. Registered native builds and web publishes
deliver runtime updates from the engine side.
See [external game setup](docs/native-package.md#local-external-projects).

## Development

```sh
make run          # optimized editor for normal interaction
make play         # optimized standalone game
make debug        # plain Debug editor
make debug-san    # Debug editor with ASan and UBSan
make build        # build sanitizer-enabled Debug app and controller
make package-native # package native engine, Studio, and MCP for external games
make test-tools   # run essential C tooling checks
make build-tracy  # build native RelWithDebInfo with Tracy enabled
make run-tracy    # run the native Tracy profile
make test         # model, runtime, native UI, generation, and loader checks
make test-fast TEST_ARGS=--graph-optimize  # focused plain Debug check
make mcp          # run the local MCP authoring server over stdio
make mcp-setup    # generate local Codex MCP configuration
make dev          # watch sources, rebuild, and reload the running editor
make format       # format project C sources with clang-format 22, as CI does
make check-format
make web          # static browser application; requires Emscripten
make publish-web  # publish the latest origin/main commit to GitHub Pages
```

`make publish-web` fetches and builds the latest `origin/main` commit in a
temporary checkout, publishes the compiled files to `gh-pages`, and waits for
the live deployment marker and matching asset hashes. It requires Emscripten,
the native build tools, and an authenticated `gh` session with repository push and Pages access. Local
engine edits are excluded, and `main` is never pushed. Project links tracked in the published engine commit are snapshotted from
their game repository’s committed `HEAD`; local game edits and ignored mounts
are excluded.
`BUILD_SOURCES.json` records the engine and game commits. Set `JOBS` to control build
parallelism and `CHIBI_COLLAB_URL` to configure the collaboration server.
If `main` advances during the build, rerun the command.

`make run` and `make play` use the fully optimized `Release` profile. Their
launch targets build only the application; `make build-release` still builds
all native targets. `make debug` uses a plain `Debug` profile, while
`make debug-san` uses
ASan and UBSan by default. Use `make debug-san SANITIZERS=thread` when a TSan
run is useful; TSan cannot be linked into the same binary as ASan, so the one
target switches the sanitizer mode in the shared Debug build directory.

Native builds use Ninja for new build directories when available and run up to
eight compile jobs by default. Set `JOBS` to tune this for your machine or
concurrent worktrees. An unchanged configuration is reused; changed Make flags
or CMake cache settings trigger configuration again.

`make test-fast TEST_ARGS=--graph-optimize` builds only the test executable in
plain Debug and runs that selector. Use another `test_suite` selector for the
area you changed. `make test` remains the full ASan and UBSan gate.

Neon Swarm is the default project for native launches, editor profiles, MCP,
and `make web-serve`. Its manifest selects `scenes/Sandbox.kdl`.
Use `make debug PROJECT=.` for the Arena3D reference, or
`make debug PROJECT=. SCENE=scenes/OrbitPinball.kdl` for pinball.
Add `ARGS=--play` to launch into Play. `PROJECT` works the same for
`make web-serve`, and `SCENE` is optional everywhere: without it a project
opens at its manifest's start scene. In the browser, add
`?scene=scenes/<file>.kdl` to the editor URL to open another scene.

File > Open Project and the command palette switch between the repository's
projects (`project.kdl` and `projects/*`) without relaunching. `make web-serve`
mirrors all of them into the browser, so `?project-root=projects/<name>` also
opens one there.

The static `make web` build keeps projects in the browser's IndexedDB and
reopens the last one, starting with Neon Swarm. Opening a sample the first time
copies it into a project of your own. File > Open Project and the command
palette list your projects and the samples not yet copied (Arena3D, Neon Swarm
and Studio); `?project-root=projects/<sample>` opens a sample,
`?project=<name>` one of your projects, and `?new` a fresh copy of the sample. File > Export Project downloads a zip, and Import Project opens a zip as
a new project. A project that cannot open, such as one saved at another project
version, offers Export Project and New Project. `make web-serve` saves to the
local project instead.

Gameplay can be recorded and replayed as a `scenarios` asset. In Studio, Record
Gameplay restarts the scene and records until Stop or Stop Recording, then
saves `scenarios/<Scene> Recording.kdl`; the command palette lists each
scenario to replay on a loop until Stop. Scenarios can also be written by hand
(see [the MCP workflow](docs/mcp.md#scenario-assets)). `ARGS='--play --scenario
Firefight'`, or `?play&scenario=Firefight` in the browser, replays one on a loop
in the game. The native controller exposes `profile_scenario` for headless
measurements; browser frame summaries provide web timing.

Run `make help` for the complete target list and the purpose of each target.

The [frame-time optimization roadmap](docs/research/frame-time-roadmap.md)
records current Arena measurements, remaining performance options and the
acceptance criteria for consistent 60 FPS and higher refresh rates.
The [Tracy workflow](docs/tracy.md) explains the optional native timeline
profile and its MCP analysis server.

When available, Chibi MCP is the default interface for authoring shaders,
behaviors, prototypes, and scenes. See [the MCP workflow](docs/mcp.md) for
the public tools and validation sequence.

To edit an asset directly in Studio, open it from Assets and switch the
area to Text Editor. The KDL editor highlights syntax and offers reflected
component, field, enum and asset completions as you type or with Control+Space.
Diagnostics mark invalid source. Live preview keeps the last valid asset while
you finish an edit. Saving writes the valid source exactly as typed, preserving
comments and spacing.

### Zed

Open the repository root in Zed and run `Build (Debug)` from `task: spawn` once
after cloning. The project settings use clangd and the checked-in
`.clang-format`; the build creates the compilation database that powers
completion, diagnostics, navigation, and formatting. `.zed/tasks.json` also
provides editor, Arena3D, development-reload, test, render-check, and release
tasks. `.zed/debug.json` provides CodeLLDB launch profiles for the editor,
Arena3D, and the test suite.

Native builds require CMake, a C23 compiler, and libclang. Dependencies
are pinned in CMake. On macOS, the Makefile uses Homebrew
LLVM when installed: Apple's Clang 17 ASan runtime does not correctly unregister
dynamic-library globals during unload. Set `NATIVE_TOOLCHAIN_FLAGS` for Make or
`CC` and `CXX` for CMake to select another compiler.
Fresh Linux Make builds prefer installed Clang, matching native CI; existing
CMake caches and explicit compiler or toolchain selections are preserved.
Native configuration preserves an explicitly selected SDK; otherwise it uses
the selected compiler's SDK for both framework lookup and compilation.

Native and web builds automatically build or reuse immutable dependency packages
under `${XDG_CACHE_HOME:-$HOME/.cache}/chibi-dependency-packages`. Set
`CHIBI_DEPENDENCY_PACKAGE_CACHE_DIR` to choose another location. All worktrees
use this cache regardless of which app created them or where their folders live.
Packages include Cute and its compiled dependencies, SDL, RmlUi, FreeType, Zep,
enkiTS, SLJIT, tree-sitter, and bundled C libraries where enabled. Native, browser,
threaded browser, and incompatible toolchain or build options use separate entries.
Dependency pins, patches, compiler and SDK identity, and build flags determine
reuse. A failed or concurrent first build cannot publish an incomplete package.
Application objects and generated files remain in each checkout's build directory.
The Tracy Python server keeps a private build because its launcher needs local
bindings; ordinary Tracy client libraries are shared. External source overrides
also retain private builds so local dependency edits remain in use. Set
`CHIBI_DEPENDENCY_PACKAGES=0` to request a private dependency build explicitly.

Dependency sources are seeded from pristine snapshots in
`${XDG_CACHE_HOME:-$HOME/.cache}/chibi-fetchcontent`, configurable through
`CHIBI_FETCHCONTENT_CACHE_DIR`. APFS clones or Linux reflinks share unchanged
source storage while package builds apply their own patches independently.

When ccache is installed, native builds and Release web builds also reuse matching
application objects across worktrees. Native Debug and development builds normalize
compilation paths so debug information resolves local sources. `ccache --show-stats`
reports hits and evictions. On APFS, ccache's optional `file_clone` setting can share
object storage across builds; it disables cache compression and benefits repeated
objects most. Cache and worktree directories must be on a compatible filesystem.

Build and test commands print phase summaries and save full output in `.chibi/logs`.
Failures print bounded diagnostic excerpts and the full log path. Use `VERBOSE=1`
to stream output. Old completed logs are limited to 20 files and 64 MiB; running
logs and the current phase are retained. `make build BUILD_TARGET=chibi_ctl` builds
only the controller; `BUILD_TARGET=` requests the default CMake target set.
`make test` still runs the complete required checks. Ordinary builds do not create
a separate runtime distribution; launch commands refresh registered games, or use
`make update-game-runtime` explicitly.

`make dev ARGS="--fps 60"` starts the native development host. Save a source file
to build a new generation. Build diagnostics go to `.chibi/build.log`.
A failed build or rejected import leaves the current generation running. A
successful import replaces the editor and runtime together. Host, framework,
font, and shader changes request a manual restart: close the editor and run `make dev` again.

New development directories use Ninja when it is installed. To switch an older
Make directory, use a fresh directory: `make dev DEV_BUILD_DIR=build-dev/ninja`.
Existing directories retain their generator. With GNU Make versions before 4,
changed sources require a clean build because timestamp precision can miss relinks.

The workspace checkpoint preserves the open project, unsaved scene, prototype and graph edits,
Undo/Redo, selection, graph navigation and native window geometry. Play restarts from the
authored scene with its previous running or paused intent. Live simulation
memory and in-progress pointer gestures are recreated. Checkpoints live in
`.chibi/workspace.json`; source documents are saved only through editor Save.
When a behavior source changed after the checkpoint was written, the editor
keeps the checkpointed copy; File > Reload on that behavior replaces it with the
source.

The editor follows its project on disk, so edits from another program, MCP or
version control appear as they are saved. Graph assets and systems apply at
once, even while playing; prototypes and the open scene reload when the runtime
stops, keeping the view and selection. A document with unsaved edits keeps them
and offers Reload. `make web-serve` sends the same edits to the browser.

## Code map

| Location | Responsibility |
| --- | --- |
| `include/chibi` | Versioned application lifecycle contract shared with the host |
| `src/host` | Window lifetime, module replacement, checkpoint files, native source watching |
| `src/editor` | Workspace, documents, graph editing and the RmlUi Studio |
| `src/graph` | Graph model, node catalog, documents, compilation and execution |
| `src/runtime` | ECS-backed behavior, system and scene execution |
| `src/ecs` | Entity storage, component layout and queries |
| `src/assets` | Authored asset formats and loading |
| `src/render` | Scene rendering, materials, shapes and shaders |
| `src/support` | Allocation, arrays, text and small shared utilities |
| `src/platform` | Integration with the actual framework headers |
| `tools/reflect` | Native reflection generator and identity manifest checks |
| `cmake` | Build generation, development descriptors and immutable staging |
| `test` | Tests and fixture-only reflection packs, assets and helpers |
| `content` | Assets shipped with the editor |

Internal headers live beside their implementations. The `include` tree contains
only the boundary another binary consumes. `cmake/sources.cmake` records compilation
ownership separately from the conceptual folder layout.

See [the implementation tracker](docs/refactor-plan.md) and
[the reload contract](docs/reload.md) for the acceptance checks and lifetime rules.
