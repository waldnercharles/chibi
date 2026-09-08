# c-blueprints

A C23 visual programming editor for authored game behaviors, prototypes, and scenes.

## Development

```sh
make build        # static native application, warnings and sanitizers enabled
make run
make test         # model, runtime, native UI, generation, and loader checks
make dev          # watch sources, rebuild, and reload the running editor
make format       # explicitly format project C sources
make check-format
make web          # static browser application; requires Emscripten
```

Native builds require CMake, a C23 compiler, and libclang. Dependencies
are pinned in CMake. On macOS, the Makefile uses Homebrew
LLVM when installed: Apple's Clang 17 ASan runtime does not correctly unregister
dynamic-library globals during unload. Set `NATIVE_TOOLCHAIN_FLAGS` for Make or
`CC` and `CXX` for CMake to select another compiler.

`make dev ARGS="--fps 60"` starts the native development host. Save a source file
to build a new generation. Build diagnostics go to `.blueprints/build.log`.
A failed build or rejected import leaves the current generation running. A
successful import replaces the editor and runtime together. Host, framework,
font, and shader changes request a manual restart: close the editor and run `make dev` again.

For faster incremental builds, choose Ninja in a new development directory:
`CMAKE_GENERATOR=Ninja make dev DEV_BUILD_DIR=build-dev-ninja`.
Existing directories retain their generator. With GNU Make versions before 4,
changed sources require a clean build because timestamp precision can miss relinks.

The workspace checkpoint preserves unsaved scene, prototype and graph edits,
Undo/Redo, selection, graph views, navigation, docking and native window geometry. Play restarts from the
authored scene with its previous running or paused intent. Live simulation
memory and in-progress pointer gestures are recreated. Checkpoints live in
`.blueprints/workspace.json`; source documents are saved only through editor Save.

## Code map

| Location | Responsibility |
| --- | --- |
| `include/blueprints` | Versioned application lifecycle contract shared with the host |
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
