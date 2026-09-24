# Chibi

A C23 visual programming editor for authored game behaviors, prototypes, and scenes.

## Development

```sh
make run          # optimized editor for normal interaction
make play         # optimized standalone game
make debug        # plain Debug editor
make debug-san    # Debug editor with ASan and UBSan
make build        # build the sanitizer-enabled Debug profile
make build-tracy  # build native RelWithDebInfo with Tracy enabled
make run-tracy    # run the native Tracy profile
make test         # model, runtime, native UI, generation, and loader checks
make test-fast TEST_ARGS=--graph-optimize  # focused plain Debug check
make sdl-package  # build pinned SDL once for macOS worktrees
make mcp          # run the local MCP authoring server over stdio
make mcp-setup    # generate local Codex MCP configuration
make dev          # watch sources, rebuild, and reload the running editor
make format       # explicitly format project C sources
make check-format
make web          # static browser application; requires Emscripten
```

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
and `make web-serve`. Its manifest selects `scenes/Sandbox.yaml`.
Use `make debug PROJECT=.` for the Arena3D reference, or
`make debug PROJECT=. SCENE=scenes/OrbitPinball.yaml` for pinball.
Add `ARGS=--play` to launch into Play.

Run `make help` for the complete target list and the purpose of each target.

The [frame-time optimization roadmap](docs/research/frame-time-roadmap.md)
records current Arena measurements, remaining performance options and the
acceptance criteria for consistent 60 FPS and higher refresh rates.
The [Tracy workflow](docs/tracy.md) explains the optional native timeline
profile and its MCP analysis server.

When available, Chibi MCP is the default interface for authoring shaders,
behaviors, prototypes, and scenes. See [the MCP workflow](docs/mcp.md) for
the public tools and validation sequence.

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

When ccache is installed, native builds under the source tree can reuse matching
objects across worktrees. Debug and development builds normalize compilation
paths so their debug information still resolves local sources. New native build
directories save pinned dependency source snapshots to
`CHIBI_FETCHCONTENT_CACHE_DIR` (by default,
`${XDG_CACHE_HOME:-$HOME/.cache}/chibi-fetchcontent`). Later build directories
copy these snapshots using filesystem cloning when available. Each worktree can
patch Cute and generate files without changing the shared snapshot. Dependency
build directories and linked libraries remain private, except for an installed
SDL package. Run `ccache --show-stats` to check hits and evictions; increase its
size limit if it is continually full.

On macOS, `make sdl-package` builds the pinned SDL 3.4.0 archive once and
installs its static and shared libraries under
`${XDG_CACHE_HOME:-$HOME/.cache}/chibi-sdl3`. Future native build directories
automatically use that package when the compiler and SDK match. Set
`CHIBI_SDL3_PACKAGE_ROOT` to use another cache location. If the package is
absent, Cute fetches and builds SDL in the worktree as before.

`make dev ARGS="--fps 60"` starts the native development host. Save a source file
to build a new generation. Build diagnostics go to `.chibi/build.log`.
A failed build or rejected import leaves the current generation running. A
successful import replaces the editor and runtime together. Host, framework,
font, and shader changes request a manual restart: close the editor and run `make dev` again.

New development directories use Ninja when it is installed. To switch an older
Make directory, use a fresh directory: `make dev DEV_BUILD_DIR=build-dev/ninja`.
Existing directories retain their generator. With GNU Make versions before 4,
changed sources require a clean build because timestamp precision can miss relinks.

The workspace checkpoint preserves unsaved scene, prototype and graph edits,
Undo/Redo, selection, graph views, navigation, docking and native window geometry. Play restarts from the
authored scene with its previous running or paused intent. Live simulation
memory and in-progress pointer gestures are recreated. Checkpoints live in
`.chibi/workspace.json`; source documents are saved only through editor Save.
When a behavior source changed after the checkpoint was written, the editor
asks whether to reload that source or keep the checkpointed workspace copy.

## Code map

| Location | Responsibility |
| --- | --- |
| `include/chibi` | Versioned application lifecycle contract shared with the host |
| `src/host` | Window lifetime, module replacement, checkpoint files, native source watching |
| `src/editor` | Workspace, documents, graph canvas, panels and native UI |
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
