# Chibi toolchain migration inventory

Read this before changing or deleting project-owned Python, shell, Make, CMake,
packaging, reflection, test-supervision, browser, or release code. This file
lists current owners and the requirements for replacing them.

The goal is not to translate scripts line for line. Move policy and orchestration
into shared `.chibi` modules, keep native host capabilities narrow, then delete a
legacy implementation only after its callers and failure behavior have moved.

## Minimum final tool set

The installed product should expose one public tool, `chibi`. Compiler, build,
check, format, test, documentation, browser, export, version, and diagnostics are
commands or internal modules behind it. `chibi-player` remains a runtime product,
not a second authoring tool. A checksummed bootstrap compiler artifact may remain
for recovery, but it is not a competing implementation.

Repository build programs may call external compilers, linkers, LLVM, SLJIT,
Emscripten, platform SDKs, Git, and hosting APIs. Those dependencies do not own
Chibi policy. Python remains acceptable only inside an external application that
requires its Python API, such as Blender.

## Universal deletion gate

Before removing or bypassing an implementation:

1. Name its exact responsibility and replacement.
2. Find every source-tree, installed-SDK, CI, release, Studio, game, and external
   application caller.
3. Prove the replacement from a clean tree and, when applicable, from a relocated
   installed SDK without engine source.
4. Exercise success, invalid input, partial output, interruption, retry, cache hit,
   cache invalidation, and cleanup behavior that the responsibility owns.
5. Compare output manifests, identities, permissions, licenses, checksums, runtime
   behavior, and supported platforms where applicable.
6. Remove the old caller, implementation, tests that only test the old mechanism,
   and stale documentation together. Preserve replacement coverage.
7. Describe the checks run and any remaining recovery path in the change.

Passing a happy-path replacement is not enough. A repository-local `.chibi` file
does not replace a capability installed into the SDK. Invoking CMake from Chibi
does not replace CMake's build graph. Building a C compiler from Chibi does not
make the compiler self-hosted.

## Responsibility ledger

| Responsibility | Current owners | Replacement and deletion gate | Required evidence |
| --- | --- | --- | --- |
| Public CLI application | `apps/chibi/`, `src/host/application.c`, `src/host/application_io.c`, `src/host/application_tools.c`, `src/host/chibi_main.c` | The installed ordinary Chibi application's `Main` owns public command routing, argument validation, watching, retry, cache and exit policy. The native launcher resolves resources and starts the shared application host; native bindings supply OS operations and typed SDK/project facts. | Authored argument tests, `test/tools/chibi_cli.c`, terminal/process cleanup, nested CLI execution, installed and relocated SDK commands, and graph/CMake resource parity. |
| Standalone program sessions | `src/authoring/program.c`, `src/authoring/program_service.c`, `src/execution/program.c` | Export `Chibi::program` for source-buffer compilation, transactional replacement, diagnostics and synchronous invocation. CLI and user programs share `program.open/update/call/diagnostic/close/test`; existing runtime replacement owns retained-state migration. The private runner and compiler `--run` bridge are removed. | Native session/ownership cases, Main execution, retained state and rejected edits, imported state, independent handles, public SDK C consumers and relocation. |
| Project prepare and run | `apps/chibi/Projects.chibi`, `src/host/compile_main.c`, `src/authoring/project_compile.c`, `tools/toolchain/StageOne.chibi` | Public project compilation still uses the C catalog, including StageOne's input export and game assembly. Move the declaration, native-component, library, system and projection prerequisites before making the authored compiler route the public default. | `test/tools/chibi_cli.c`, installed-SDK build/run, cache hit and invalidation, exact player arguments, failure status and checked-game equivalence. |
| Build-program compilation | `src/host/build_host.c`, `src/toolchain/compiler_bootstrap.c`, `tools/compiler/package_compiler/`, `tools/compiler/bootstrap/` | A validated owned Chibi compiler generation replaces C semantic preparation for build programs. `CompileClosureWithEffects` carries actual native effects; the runtime-only linker loads its output. Retain other C frontend callers until their separate gates pass. | Fast load without recompilation, changed compiler/core hash detection, fixed-point recovery, damaged/version/ABI/completeness rejection, owned results, no loader writes, native-link boundaries and relocated read-only SDK invocation. |
| Build-host process execution | `src/host/process.c`, `src/host/build_host.c`, `src/host/build_main.c`, `test/language/BuildWorkflow.chibi` | Private native host supplies direct argv, cwd, allowlisted environment, bounded output and deadline, error mapping, cancellation, and process-group cleanup, plus the narrow file primitives in `docs/self-hosting-build.md`. Keep it out of game catalogs; retain native bindings as Chibi takes ownership of orchestration. | `build-process` real-child cases on Linux/macOS and `language-build-workflow` recording parity; Windows retains its existing path pending equivalent process-tree support. |
| Native graph | `tools/toolchain/Graph.chibi`, `tools/graph/` | Default native Linux and macOS route. `GRAPH_BUILD=` explicitly selects retained CMake construction. Keep reference/bootstrap until their replacement gates pass. Standalone consumer builds and public SDK/web/Studio remain separate callers. | All native `build-graph-*` routes, actual-profile verification, native/material/project/language groups and private compiler-tools SDK relocation. |
| Native configuration | `tools/toolchain/Build.chibi`, `tools/configure_inputs.py`, `tools/configure.sh`, `tools/configure_native.sh` | Installed Chibi program owns native orchestration, compiler SDK discovery and third-party archive downloads; the read-only Python signature probe and shell recovery remain until their replacement gates. Preserve explicit compiler, toolchain, cache, and user-definition precedence and correct invalidation. | `build-toolchain` authored cases plus `test/tools/package.c`, including warm configuration and web SDK recipe cases; clean and warm profile builds. |
| Dependency acquisition and cache | Done (2026-10-20). `third_party/archives.tsv` pins one prebuilt archive per target and variant, published as a GitHub release; `Build.chibi` downloads it into the clone's `.git/chibi/third_party`, shared by its worktrees, and `cmake/third_party.cmake` loads it (`third_party/README.md`). | The FetchContent declarations, configure-time patching, `dependency_package.py`, `fetchcontent_cache.py`, `cmake/dependency_package/`, the `CHIBI_DEPENDENCY_*` and `CHIBI_FETCHCONTENT_*` switches, their tests and CI cache keys are deleted. The temporary archive builder (`third_party/CMakeLists.txt`) and `cmake/third_party.cmake`'s own download leave with CMake (M6). | Offline clean `chibi build` from the cached archives and test aggregates on macOS arm64; `build-toolchain` downloads, verifies, reuses and prunes archives through a `gh` stand-in. Web and macOS ThreadSanitizer archives are built locally (release third-party-2026-10-21.2); Linux and Windows archives wait for `.github/workflows/third-party.yml`. |
| Build logs and command display | `tools/quiet.py`, Makefile `QUIET` | Build host owns bounded capture, full failure diagnostics, log retention, pruning, concurrent writers, and signal cleanup. | Focused native fixtures for success, large output, failure, interruption, pruning, and concurrent commands. |
| SDK and runtime packaging | `tools/toolchain/SDK.chibi` for graph compiler tools; `tools/package_sdk.py`, `sdk/CMakeLists.txt`, `sdk/*.cmake` for the full SDK | The Chibi compiler-tools installer carries both private hosts, compiler/core/bootstrap/build resources and dependency notices into a fresh relocatable prefix, with exact link-record checks and atomic publication. Full SDK packaging must stage and install the full compiler/bootstrap tree in `share/chibi/compiler` and its core collection in `share/chibi/core` for the private build host. Chibi packaging must also preserve target/header/object ownership, licenses and identity before the Python implementation leaves. | `package`, `package-ownership`, and `desktop-dependencies`; complete compiler payload, source-free and relocated read-only installed SDK consumers. |
| Player and Studio packaging | `tools/package_player.py`, `tools/package_studio.py` | Preserve package-kind checks, checksums, relocatable resources and RPATHs, platform executables, and atomic staging. | Existing package cases plus installed player startup and affected Studio startup/interaction checks. |
| Native reflection and generated code | `tools/reflect/`, `sdk/native.cmake`, `src/runtime/component_ids.tsv`, `cmake/tests/generation*.cmake` | Keep the native host generator until a Chibi replacement reproduces IDs, layouts, bindings, and transactional failure, including native-host generation for Emscripten targets. | Reflection range cases, generation identity/unchanged/failure fixtures, handle pins, JIT identity, native and cross-build use. |
| Internal build graph | root `CMakeLists.txt`, `cmake/`, `sdk/`, and component/test `CMakeLists.txt` files | Chibi build graph reproduces profiles, generated prerequisites, native/cross compile, include/link ownership, optional modules, and install layouts. CMake still owns SDK construction, other native-platform paths, Wasm, Studio consumers and parity/recovery builds. | Native compiler, runtime-only, browser, test-driver, installed SDK, relocated SDK, and manifest comparisons. |
| CMake consumer interface | installed `ChibiConfig.cmake`, `ChibiTargets.cmake`, and related files | Treat this separately from the internal graph. Generate and retain it while Studio or another supported CMake consumer needs it. | Build Studio and representative consumers against only a relocated installed SDK. |
| Test policy and agent supervision | `tools/test_policy.py`, `tools/agent.py`, `tools/validation_plan.py`, `tools/gpu_capabilities.py`, `src/automation/execution_policy.*` | Preserve per-operation timeouts, cancellation, process-tree cleanup, diagnostics, hardware opt-in, and reviewed Git identity. | `test/tools/test_policy.c`, `test/tools/validation_plan.c`, execution-policy cases, and affected test selectors. |
| Release publication | `.github/workflows/publish.yml`, `.github/workflows/ci.yml`, package helpers | Keep workflow scheduling and credentials in hosted YAML. Move reusable validation and assembly policy into `.chibi` programs runnable on clean workers. | Immutable-release rejection, exact assets, build identity, checksums, engine revision, and matching native/web SDK generations. |
| Browser compiler isolation | `tools/web_env.sh`, Emscripten configuration | Preserve isolation between native host generators and target compiler environments, and matching compiler/player output. | Native contamination rejection plus serial/threaded browser output ownership and browser compiler/player matching. |
| C source formatting | `tools/clang-format.sh`, `.githooks/pre-commit`, CI | Keep external clang-format 22 while maintained C exists. `chibi fmt` replaces authored-source and asset formatting, not C formatting during migration. | Existing C format check and a separate canonical authored-format corpus. |
| Stage-zero compiler | `src/language/` (including the generated Tree-sitter parser), the compiler parts of `src/authoring/`, `src/types/` and `src/execution/` | Build-program semantic preparation has moved to Chibi. Public project/catalog preparation and differential/recovery callers remain. Remove the C frontend only after those callers move and the installed compiler/build/link and checked-game gates pass. Raw native syntax facts remain a separate capability. | `make test-bootstrap`, `make test-recovery`, the semantic fixture set on both stages, and an installed clean-checkout compiler/build/link and checked-game proof with `cmake`, `clang` and `gcc` unavailable. |
| Native executables and runtime | `src/host/`, `src/toolchain/`, `src/execution/`, `src/types/`, `tools/chibi_ctl/` | Executables stay C until Chibi has a native backend that emits them (LLVM AOT, backend milestone 4 in `docs/self-hosting-build.md`). Their orchestration and policy (command dispatch, export assembly, migration, validation) move to installed `.chibi` modules first. | `test/tools/chibi_cli.c`, installed-SDK and relocated-SDK command runs, and matching bytecode/SLJIT/native semantics. |
| First-party engine modules | `src/runtime/`, `src/render/`, `src/ui/`, `src/ecs/`, `src/assets/`, `src/authoring/`, `src/automation/`, `src/capabilities/`, `src/diagnostics/`, `src/platform/`, `src/support/`, `include/chibi/` | M9: Chibi packages over narrow bindings to SDL, Cute, Box2D, Box3D, RmlUi, codecs and platform APIs. `src/ui/rml.cpp` and `test/draw_backend.cpp` stay as pass-through shims while their C++ libraries do. | Each module's existing deterministic fixtures, desktop and browser parity, and ownership tests at every binding. |
| C regression and differential tests | `test/` pico_unit drivers and their `CMakeLists.txt` files, `tools/benchmark.c` | A test leaves with the C it tests. When a Chibi component replaces its C reference, its cases move to `@test` under `chibi test`. | The replacement's `chibi test` cases and the aggregate that ran the C cases. |
| Collaboration room service | `tools/collab/server/` (Node) | Needs a Chibi network service with HTTP and WebSocket bindings, or a move to `chibi-studio`, whose browser client is its only consumer. | `test/tools/collab.c` room, protocol and storage cases, and Studio collaboration against the service. |

## CMake retirement boundaries, 2026-10-28

The build lane owns reusable graph, bootstrap and packaging modules. The public
CLI lane owns `apps/chibi`, command dispatch, project execution/reload and the
shared program-session API. Its project/build commands consume the build lane's
modules. Coordinate changes to `SDK.chibi`, graph target/resource tables and SDK
installation so application/core resources and exported session libraries remain
in the same tested SDK.

| CMake responsibility | Live callers and replacement | Removal condition |
| --- | --- | --- |
| Standalone reflection host construction | `sdk-reflect-build` feeds native SDK, runtime SDK, browser SDK and browser player recipes. Linux/macOS use `Graph.chibi`'s `SDKReflect` entry and the existing `reflect` target, writing the same executable path. Windows keeps the restricted C-only `tools/reflect/CMakeLists.txt` route. | Delete the remaining standalone project after Windows can execute the authored graph and its native reflection/dependency profile is verified. Root-project construction is not an equivalent fallback because it enables C++, compiler, native and desktop defaults. The native generator and `resource.cmake` remain for real generation/reference callers. |
| Cold first host and recovery | the driver's bootstrap step, `sdk-reflect-bootstrap`, `configure_native.sh`, `configure.sh`, root/SDK CMake. | A compatible pinned host or narrow native construction route can load the authored compiler/graph from a clean checkout, recover damaged/missing payloads, and preserve explicit toolchain selection. Never require a host to construct the first host. |
| Full public SDK construction and installation | `sdk-build`, `runtime-sdk-build`, `sdk-install`, `package-sdk`, `package-runtime-sdk`, `package_sdk.py`, SDK CMake exports. `SDK.chibi` currently installs private compiler tools. | Graph target/header/object/resource/license ownership covers every exported library and product; Chibi stages and publishes them atomically. Native/runtime packages and relocated, read-only, source-free consumers pass. SDK version must have an owner independent of CMake. |
| Browser and Windows construction | Browser SDK/player recipes, `web_env.sh`, SDK CMake; Windows native/recovery builds. | Equivalent graph target/compiler/link inventories and host/target isolation, plus Windows process-tree execution/cancellation. The host reflection executable always uses the native compiler. |
| Standalone tests, vendor archives and reference builds | `test/tools`, `test/language`, `third_party/CMakeLists.txt`, `graph-reference`, generation/package fixtures. | Authored construction runs each live product/consumer and preserves generated-file identity and failure/cleanup behavior. Remove mechanism-only fixtures with the replaced mechanism. |
| Installed CMake consumer metadata | `ChibiConfig.cmake`, `ChibiTargets.cmake` and helpers consumed by Studio and C consumers. | Keep compatibility output while those consumers use CMake. Generate it from Chibi-owned SDK metadata first; remove it only when the named consumers switch and relocated SDK proofs pass. |

`SDKReflect` accepts the retained Make toolchain definitions for C/C++ compilers,
archiver, ranlib, macOS sysroot/architecture and libclang root, including typed
`-DNAME:TYPE=VALUE` and split `-D NAME=VALUE` spellings. It rejects unsupported
definitions before writing build output. Linux/macOS warm reflection construction
does not configure CMake, fetch vendor archives or construct compiler hosts.
Libclang discovery checks the explicit root first, then `CMAKE_PREFIX_PATH`,
conventional prefixes from `CMAKE_INCLUDE_PATH` and `CMAKE_LIBRARY_PATH`, known
LLVM installations and system prefixes. Environment lists use the host path
separator, retain spaces and resolve relative prefixes from the source root.
Each selected installation supplies its own C API headers, library and builtin
headers. Its driver reports the builtin resource directory, including custom
directory names; without that driver, discovery requires a unique matching
resource directory beside the library. The three environment search variables
are not accepted as `-D` definitions; arbitrary split include/library
installations remain unsupported.

## Installed Python surface

`sdk/CMakeLists.txt` currently installs these scripts into SDK products:

```text
tools/package_sdk.py
tools/package_player.py
tools/package_studio.py
tools/configure_inputs.py
```

Remove an installed script only when the replacement is present in the installed
SDK and its source-free consumer checks pass. A wrapper that still requires the
engine checkout or Python has not crossed this gate.

## Host file primitives

The build host now reads file kinds, change stamps and directory entries,
copies, renames, removes, hashes, and lists its allowed environment
(`docs/self-hosting-build.md`). Each step's own output holds 1 MiB, and probes
keep tool reports out of the build's output. What each responsibility still
lacks:

| Responsibility | Unblocked by the primitives | Still missing |
| --- | --- | --- |
| Configuration signature (`configure_inputs.py signature`) | Metadata, directory walks, environment names and text hashing. | Nothing in the host. Without dependency packages it covers caller arguments, the allowed environment and tool identities; `cmake --build` already reruns configuration when CMake's own inputs change. |
| Dependency acquisition and cache | Done: `Build.chibi` downloads prebuilt archives with `gh`, then hashes, unpacks with `tar`, renames them into the clone's Git directory and prunes unpinned ones. | Nothing. |
| SDK, player and Studio packaging | Kinds, walks, copies, renames, removals, file hashing, exclusive directory reservation and publication without replacement. Compiler-tools installation uses them in `SDK.chibi`. | Full SDK target/header/vendor metadata and ownership, player/Studio assembly, remaining platform checks, sizes, modification-time order, symbolic-link reads, and M6. |

## Removal inventory

Every project-owned C, C++, Python, CMake, Make, shell and Node file outside
`third_party/`, as of 2026-10-19. Each group names its ledger responsibility,
its callers and one decision:

- **Delete now**: nothing calls it, or its replacement already owns the
  responsibility. Remove it with its stale documentation.
- **Replace now**: Chibi and the build host can already do the work.
- **Blocked**: names the missing Chibi capability, host binding or bootstrap
  step.
- **Keep**: an external application or library requires it.
- **Owner decision**: deletable, but only after the named person confirms.

The **host file primitives** below are those in the "Host file primitives"
section: file kinds, change stamps, directory listing, copy, rename, remove,
SHA-256 and environment names, with 1 MiB of output per step. The host still
cannot read sizes, modification-time order or link targets, create unique
staging directories, or limit a child's resources. Its instruction budget,
100 million since the build graph, also bounds authored walks over large trees.

### Python

| Files | Responsibility and callers | Decision |
| --- | --- | --- |
| `tools/test_policy.py` | Test supervision. Every `make test*` target, root `CMakeLists.txt`, `AGENTS.md`, CI through Make. | **Blocked**: a Chibi supervisor must reproduce per-operation timeouts, signal forwarding, descendant cleanup and diagnostics, and must run before the first build. Explicitly retained until then. |
| `tools/configure_inputs.py` | Native configuration probe. `tools/toolchain/Build.chibi`, SDK install. | `signature`: **Replace now**: prebuilt archives removed the dependency inputs it tracked, and the host primitives cover the rest. `Build.chibi` owns the former `compiler-sdk` probe; `package-enabled` went with the dependency packages. |
| `tools/package_sdk.py`, `tools/package_player.py`, `tools/package_studio.py` | SDK, player and Studio packaging. Makefile `package-*`, `test/tools/package.c` and `package_ownership.c`, SDK install. | **Blocked**: M6 and the host file primitives. |

### Shell

| Files | Responsibility and callers | Decision |
| --- | --- | --- |
| `tools/configure.sh` | Stage-zero configuration and its SDK and web profiles. Makefile `package-sdk`, runtime SDK, web SDK and web player targets; `configure_native.sh`; `test/tools/package.c`. | **Blocked**: `Build.chibi` owns only the `native` and `tool-host` profiles. Move `sdk-native` and `sdk-web` into it (M6); the stage-zero path stays as recovery. |
| `tools/configure_native.sh` | Cold-build recovery, no longer called by the driver, which fetches the pinned host. | **Blocked**: the first build needs a compatible native build-host runtime plus compiler payloads, or a native-output backend. The prepared compiler artifact alone does not construct that executable. Retain native construction/recovery until its replacement passes. |
| `tools/web_env.sh` | Browser compiler isolation. Makefile web SDK, web player, Studio web and wasm-language configuration; `test/tools/package.c`. | **Blocked**: moves into `Build.chibi` with the web profile (M6). |
| `tools/clang-format.sh`, `.githooks/pre-commit` | C formatting. Makefile `format` and `check-format`, CI, the pre-commit hook. | **Keep** while project C exists. |

### Make and CMake

| Files | Responsibility and callers | Decision |
| --- | --- | --- |
| Root `CMakeLists.txt`, `cmake/*.cmake`, `sdk/`, component and test `CMakeLists.txt` | Internal build graph. Makefile, `Build.chibi`, `configure.sh`, CI. | **Blocked**: M6's SDK, remaining native-platform, Wasm, Studio and parity/recovery responsibilities. Build-program compiler replacement alone does not remove them. |
| Installed `ChibiConfig.cmake` and `ChibiTargets.cmake` | CMake consumer interface. `chibi-studio`. | **Blocked**: kept while Studio builds through CMake. |

### C and C++

| Files | Responsibility and callers | Decision |
| --- | --- | --- |
| `src/language/` | Stage-zero compiler and remaining public project/catalog preparation. | **Blocked**: move remaining callers and pass the installed compiler/build/link and checked-game proof. Build programs use the Chibi compiler; public compilation and differential/recovery paths still need the C implementation. |
| `src/host/`, `src/toolchain/`, `src/execution/`, `src/types/`, `tools/chibi_ctl/` | Executables, bytecode/SLJIT execution and the value ABI. | **Blocked**: Chibi emits no native executables yet (LLVM AOT). Their policy moves to installed `.chibi` modules first. |
| `src/runtime/`, `src/render/`, `src/ui/`, `src/ecs/`, `src/assets/`, `src/authoring/`, `src/automation/`, `src/capabilities/`, `src/diagnostics/`, `src/platform/`, `src/support/`, `include/chibi/` | First-party engine modules and the public header. | **Blocked**: M9, which depends on M5 and M6. |
| `src/ui/rml.cpp`, `test/draw_backend.cpp` | Pass-through shims to RmlUi and Cute's C++ API. | **Keep** while those libraries are used. |
| `tools/reflect/` | Native reflection generator. `sdk/native.cmake`, Makefile reflect builds. | **Blocked**: its ledger gate. |
| `test/`, `tools/benchmark.c` | C regression, differential and benchmark drivers. `make test-*`, CI. | **Blocked**: each leaves with the C it tests. |

### Node and browser JavaScript

| Files | Responsibility and callers | Decision |
| --- | --- | --- |
| `tools/collab/server/` | Collaboration room service. `test/tools/collab.c`, `docs/collaboration.md`, Studio's client. | **Blocked**: its ledger gate. |

Only Python that an external application imports, such as Blender through
`bpy`, belongs to that exception; a launcher around it runs under ordinary
Python and is part of the migration set. The mesh-to-SDF converter in
`tools/mesh_sdf/` was retired on 2026-10-21 with no replacement (owner
decision): its worker required Blender 4.3.2 on Linux, and its tests and the
example that used it were already gone. Git history keeps it.
Re-run the caller inventory before each deletion because open branches, CI,
Studio, or installed SDK consumers may have changed since this document was
written.
