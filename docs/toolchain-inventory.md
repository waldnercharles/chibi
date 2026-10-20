# Chibi toolchain migration inventory

Read this before changing or deleting project-owned Python, shell, Make, CMake,
packaging, reflection, test-supervision, browser, or release code. This file is a
deletion contract and current ownership map. `PLAN.md` owns the implementation
order and checkpoint.

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
7. Record the evidence and remaining recovery path in `PLAN.md`.

Passing a happy-path replacement is not enough. A repository-local `.chibi` file
does not replace a capability installed into the SDK. Invoking CMake from Chibi
does not replace CMake's build graph. Building a C compiler from Chibi does not
make the compiler self-hosted.

## Responsibility ledger

| Responsibility | Current owners | Replacement and deletion gate | Required evidence |
| --- | --- | --- | --- |
| Project prepare and run | `src/host/chibi_main.c`, `src/host/compile_main.c`, `src/authoring/project_compile.c` | Expose stable commands first, then move orchestration into installed `.chibi` modules through the build-only host. | `test/tools/chibi_cli.c`, installed-SDK build/run, cache hit and invalidation, exact player arguments, failure status. |
| Build-host process execution | `src/host/process.c`, `src/host/build_host.c`, `src/host/build_main.c`, `test/language/BuildWorkflow.chibi` | Private stage-zero host supplies direct argv, cwd, allowlisted environment, bounded output and deadline, error mapping, cancellation, and process-group cleanup, plus the narrow file primitives in `docs/self-hosting-build.md`. Keep it out of game catalogs; retain native bindings as Chibi takes ownership of orchestration. | `build-process` real-child cases on Linux/macOS and `language-build-workflow` recording parity; Windows retains stage zero pending equivalent process-tree support. |
| Native configuration | `tools/toolchain/Build.chibi`, `tools/configure_inputs.py`, `tools/configure.sh`, `tools/configure_native.sh` | Installed Chibi program owns native orchestration and compiler SDK discovery; the read-only Python signature and dependency-override probes and shell recovery remain until their replacement gates. Preserve explicit compiler, toolchain, cache, and user-definition precedence and correct invalidation. | `build-toolchain` authored cases plus `test/tools/package.c`, including warm configuration and web SDK recipe cases; clean and warm profile builds. |
| Dependency acquisition and cache | `tools/dependency_package.py`, `tools/fetchcontent_cache.py`, `cmake/dependencies.cmake`, `cmake/dependency_package/` | Vendor dependency sources in the repository, as Odin vendors its libraries (owner decision, 2026-10-19), then delete the fetch, the source and package caches and their callers rather than port them. Record each upstream revision and applied patch beside its sources and keep its license. | Offline clean configuration and build from the vendored sources on each supported platform, matching dependency revisions and patches, retained licenses, and current package/configuration cases. |
| Build logs and command display | `tools/quiet.py`, Makefile `QUIET` | Build host owns bounded capture, full failure diagnostics, log retention, pruning, concurrent writers, and signal cleanup. | Focused native fixtures for success, large output, failure, interruption, pruning, and concurrent commands. |
| SDK and runtime packaging | `tools/package_sdk.py`, `sdk/CMakeLists.txt`, `sdk/*.cmake` | Chibi modules assemble installed outputs, enforce target/header/object ownership, retain licenses and identity, and work without source checkout. | Admitted `package`, `package-ownership`, and `desktop-dependencies`; source-free and relocated installed SDK consumers. |
| Player and Studio packaging | `tools/package_player.py`, `tools/package_studio.py` | Preserve package-kind checks, checksums, relocatable resources and RPATHs, platform executables, and atomic staging. | Existing package cases plus installed player startup and affected Studio startup/interaction checks. |
| Game runtime installation | `tools/install_game_runtime.py`, `tools/kdl_asset.py` | Shared Chibi document code preserves unrelated content, rejects corrupt or foreign ownership, and updates only registered managed files. | Existing package installation preservation and corruption cases from `test/tools/package.c`. |
| Native reflection and generated code | `tools/reflect/`, `sdk/native.cmake`, `src/runtime/component_ids.tsv`, `cmake/tests/generation*.cmake` | Keep the native host generator until a Chibi replacement reproduces IDs, layouts, bindings, and transactional failure, including native-host generation for Emscripten targets. | Reflection range cases, generation identity/unchanged/failure fixtures, handle pins, JIT identity, native and cross-build use. |
| Internal build graph | root `CMakeLists.txt`, `cmake/`, `sdk/`, and component/test `CMakeLists.txt` files | Chibi build graph reproduces profiles, generated prerequisites, native/cross compile, include/link ownership, optional modules, and install layouts. | Native compiler, runtime-only, browser, test-driver, installed SDK, relocated SDK, and manifest comparisons. |
| CMake consumer interface | installed `ChibiConfig.cmake`, `ChibiTargets.cmake`, and related files | Treat this separately from the internal graph. Generate and retain it while Studio or another supported CMake consumer needs it. | Build Studio and representative consumers against only a relocated installed SDK. |
| Test policy and agent supervision | `tools/test_policy.py`, `tools/agent.py`, `tools/validation_plan.py`, `tools/gpu_capabilities.py`, `src/automation/execution_policy.*` | Preserve inherited shared deadlines, quarantine, exact admission, cancellation, process-tree cleanup, diagnostics, and reviewed Git identity. | `test/tools/test_policy.c`, `test/tools/validation_plan.c`, execution-policy cases, and re-admission of each newly selected workload. |
| Release publication | `.github/workflows/publish.yml`, `.github/workflows/ci.yml`, package helpers | Keep workflow scheduling and credentials in hosted YAML. Move reusable validation and assembly policy into `.chibi` programs runnable on clean workers. | Immutable-release rejection, exact assets, build identity, checksums, engine revision, and matching native/web SDK generations. |
| Browser compiler isolation | `tools/web_env.sh`, Emscripten configuration | Preserve isolation between native host generators and target compiler environments, and matching compiler/player output. | Native contamination rejection plus serial/threaded browser output ownership and browser compiler/player matching. |
| C source formatting | `tools/clang-format.sh`, `.githooks/pre-commit`, CI | Keep external clang-format 22 while maintained C exists. `chibi fmt` replaces authored-source and asset formatting, not C formatting during migration. | Existing C format check and a separate canonical authored-format corpus. |
| Blender worker | `tools/mesh_sdf/worker.py`, Blender-only `bpy` calls, and the worker's imported encoding support | Python may remain only for code that executes inside Blender because Blender requires its Python API. | Deterministic conversion, malformed geometry, metadata, work limits, and source provenance cases. |
| Blender launcher and publication | `tools/mesh_sdf/convert.py` and ordinary-Python support in `tools/mesh_sdf/atlas.py` | Move subprocess, deadline, resource-limit, staging, hashing, validation, and publication policy to Chibi. Keep only the thin Blender worker and support it must import. | Timeout and process cleanup, memory/work limits, atomic destination publication, provenance, deterministic atlas bytes, and corrupt/partial output rejection. |
| Stage-zero compiler | `src/language/` (including the generated Tree-sitter parser), the compiler parts of `src/authoring/`, `src/types/` and `src/execution/` | Stage one and stage two reproduce each other (M5) and the checksummed bootstrap artifact recovers the compiler from a clean checkout. Then the C compiler leaves the normal path and, after the recovery proof, the active tree. | `make test-bootstrap`, `make test-recovery`, the semantic fixture set on both stages, and a clean-checkout recovery with `cmake`, `clang` and `gcc` unavailable. |
| Native executables and runtime | `src/host/`, `src/toolchain/`, `src/execution/`, `src/types/`, `tools/chibi_ctl/` | Executables stay C until Chibi has a native backend that emits them (LLVM AOT, backend milestone 4 in `docs/self-hosting-build.md`). Their orchestration and policy (command dispatch, export assembly, migration, validation) move to installed `.chibi` modules first. | `test/tools/chibi_cli.c`, installed-SDK and relocated-SDK command runs, and matching bytecode/SLJIT/native semantics. |
| First-party engine modules | `src/runtime/`, `src/render/`, `src/ui/`, `src/graph/`, `src/ecs/`, `src/assets/`, `src/authoring/`, `src/automation/`, `src/capabilities/`, `src/diagnostics/`, `src/platform/`, `src/support/`, `include/chibi/` | M9: Chibi packages over narrow bindings to SDL, Cute, Box2D, Box3D, RmlUi, codecs and platform APIs. `src/ui/rml.cpp` and `test/draw_backend.cpp` stay as pass-through shims while their C++ libraries do. | Each module's existing deterministic fixtures, desktop and browser parity, and ownership tests at every binding. |
| C regression and differential tests | `test/` pico_unit drivers and their `CMakeLists.txt` files, `tools/benchmark.c` | A test leaves with the C it tests. When a Chibi component replaces its C reference, its cases move to `@test` under `chibi test`. | The replacement's `chibi test` cases and the admitted aggregate that ran the C cases. |
| Collaboration room service | `tools/collab/server/` (Node) | Needs a Chibi network service with HTTP and WebSocket bindings, or a move to `chibi-studio`, whose browser client is its only consumer. | `test/tools/collab.c` room, protocol and storage cases, and Studio collaboration against the service. |

## Installed Python surface

`sdk/CMakeLists.txt` currently installs these scripts into SDK products:

```text
tools/package_sdk.py
tools/package_player.py
tools/package_studio.py
tools/install_game_runtime.py
tools/kdl_asset.py
tools/configure_inputs.py
tools/dependency_package.py
tools/fetchcontent_cache.py
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
| Configuration signature (`configure_inputs.py signature`) | Metadata, directory walks, environment names and text hashing. | Nothing in the host. Once dependencies are vendored it shrinks to caller arguments, the allowed environment and tool identities; `cmake --build` already reruns configuration when CMake's own inputs change. Port it after vendoring, not before. |
| Dependency acquisition and cache | Nothing: vendoring replaces it. | No host primitives. |
| SDK, player and Studio packaging | Kinds, walks, copies, renames, removals and file hashing. | Sizes, modification-time order, symbolic-link reads, unique staging directories, the same budget, and M6. |
| Blender launcher and publication | Hashing, copies and renames. | Child resource limits. |

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
staging directories, or limit a child's resources. Its 100,000-step budget
also bounds authored walks over large trees.

### Python

| Files | Responsibility and callers | Decision |
| --- | --- | --- |
| `tools/test_policy.py` | Test policy. Every `make test*` target, root `CMakeLists.txt`, `AGENTS.md`, CI through Make, `.github/scripts/*.sh`. | **Blocked**: a Chibi supervisor must match its inherited shared deadline, signal forwarding, descendant cleanup, quarantine and admission tables, and must run before the first build. Explicitly retained until then. |
| `tools/agent.py`, `tools/validation_plan.py`, `tools/gpu_capabilities.py` | Agent supervision. Makefile `agent-*` targets, `AGENTS.md`, `docs/agent-execution.md`, `test/tools/validation_plan.c`. | **Blocked** behind `test_policy.py`, whose admission tables they import. `gpu_capabilities.py` also needs a dynamic-library binding to probe Vulkan. |
| `tools/configure_inputs.py` | Native configuration probes. `tools/toolchain/Build.chibi`, SDK install. | `signature`: **Blocked** on vendoring, which removes the dependency inputs it tracks; the host primitives already cover the rest. `Build.chibi` now owns the former `compiler-sdk` and `package-enabled` probes. |
| `tools/dependency_package.py`, `tools/fetchcontent_cache.py` | Dependency acquisition and cache. `Build.chibi`, `tools/configure.sh`, `cmake/dependency_package/`, `cmake/tests/desktop_dependencies/`, CI cache keys in `ci.yml`, `docs.yml` and `publish.yml`, `test/tools/build_toolchain.c`, `desktop_dependencies.c` and `package.c`, SDK install. | **Replace now** by vendoring the sources (owner decision, 2026-10-19), then delete them with their callers. `chibi-studio`'s CI also runs `fetchcontent_cache.py` and must stop first. |
| `tools/package_sdk.py`, `tools/package_player.py`, `tools/package_studio.py` | SDK, player and Studio packaging. Makefile `package-*`, `install_game_runtime.py`, `test/tools/package.c` and `package_ownership.c`, SDK install. | **Blocked**: M6 and the host file primitives. |
| `tools/install_game_runtime.py`, `tools/kdl_asset.py` | Game runtime installation into games registered in `.chibi/game-runtime.json`. Makefile `update-game-runtime`, `docs/native-package.md`, `sdk/README.md`, `docs/declarative-sdf-modeling.md`, `test/tools/package.c`, SDK install. | **Owner decision**: the standalone game repositories it served were retired on 2026-10-17 and `chibi export` is the distribution command. If the workflow is retired, delete it. Otherwise **Blocked** on Chibi KDL editing that preserves unrelated content, and on the host file primitives. |
| `tools/quiet.py` | Build logs for every Makefile recipe (`QUIET`). | **Blocked**: it wraps configuration before any build host exists. Needs a prebuilt host or Make's retirement (M6). |
| `tools/mesh_sdf/convert.py`, `tools/mesh_sdf/atlas.py` | Blender launcher and publication. Run by hand (`tools/mesh_sdf/README.md`). | **Blocked** on child resource limits; the host file primitives cover hashing, copies and renames. |
| `tools/mesh_sdf/worker.py` | Blender worker. `convert.py`. | **Keep**: runs inside Blender. |
| `.github/scripts/transport-boundary-audit.py` | Hosted M5 ELF audit. The `.github/scripts/*.sh` helpers; cited as not run in the PLAN checkpoint. | **Owner decision**: no workflow in this repository runs it; delete once the M5 owner confirms its hosted runs are finished. |
| `docs/studio-migration-roadmap/build_ledger.py` | Studio migration ledger generator. Its README and provenance JSON. | **Blocked**: the Studio migration is paused; this moves or retires with it. |
| `docs/prototypes/studio/serve.py` and the prototype's JavaScript | Static server and code for the Studio design prototype. Its README; the prototype is cited by `docs/editor-ui-references.md` and `docs/studio-ui-authoring.md`. | **Blocked**: Studio-owned design reference; Studio migration is paused. |
| `docs/research/cut61-iframe-profile/finish/analyze.py`, `docs/research/cut61-iframe-profile/run.py.txt` | Analysis of CUT61's historical capture, which policy forbids rerunning. Only their own record; nothing cites the record. | **Owner decision**: delete with the stale record; Git keeps the history. |

### Shell

| Files | Responsibility and callers | Decision |
| --- | --- | --- |
| `tools/configure.sh` | Stage-zero configuration and its SDK and web profiles. Makefile `package-sdk`, runtime SDK, web SDK and web player targets; `configure_native.sh`; `test/tools/package.c`. | **Blocked**: `Build.chibi` owns only the `native` and `tool-host` profiles. Move `sdk-native` and `sdk-web` into it (M6); the stage-zero path stays as recovery. |
| `tools/configure_native.sh` | Cold-build recovery. Makefile `stage-zero-build`, used when no protocol-3 build host exists. | **Blocked**: the first build has no host. Needs a prebuilt host from the bootstrap artifact; stays as stage-zero recovery. |
| `tools/web_env.sh` | Browser compiler isolation. Makefile web SDK, web player, Studio web and wasm-language configuration; `test/tools/package.c`. | **Blocked**: moves into `Build.chibi` with the web profile (M6). |
| `tools/clang-format.sh`, `.githooks/pre-commit` | C formatting. Makefile `format` and `check-format`, CI, the pre-commit hook. | **Keep** while project C exists. |
| `.github/scripts/*.sh` (five) | Hosted M5 validation helpers. No workflow in this repository; they expect a separate validation checkout. | **Owner decision**: delete once the M5 owner confirms. |

### Make and CMake

| Files | Responsibility and callers | Decision |
| --- | --- | --- |
| `Makefile` | Build, test, package and agent entry points. CI, `AGENTS.md`, documentation. | **Blocked**: M6 build graph and the test-policy replacement. Public targets give way to `chibi` commands. |
| Root `CMakeLists.txt`, `cmake/*.cmake`, `sdk/`, component and test `CMakeLists.txt` | Internal build graph. Makefile, `Build.chibi`, `configure.sh`, CI. | **Blocked**: M6. |
| Installed `ChibiConfig.cmake` and `ChibiTargets.cmake` | CMake consumer interface. `chibi-studio`. | **Blocked**: kept while Studio builds through CMake. |
| `cmake/tests/` | Fixtures for desktop dependencies, package ownership and generation. `test/tools/desktop_dependencies.c`, `package_ownership.c`, root `CMakeLists.txt`. | **Blocked**: leave with the CMake mechanism they test. |

### C and C++

| Files | Responsibility and callers | Decision |
| --- | --- | --- |
| `src/language/` | Stage-zero compiler. | **Blocked**: M5 completion and the clean-checkout recovery proof. Kept frozen as recovery until then. |
| `src/host/`, `src/toolchain/`, `src/execution/`, `src/types/`, `tools/chibi_ctl/` | Executables, bytecode/SLJIT execution and the value ABI. | **Blocked**: Chibi emits no native executables yet (LLVM AOT). Their policy moves to installed `.chibi` modules first. |
| `src/runtime/`, `src/render/`, `src/ui/`, `src/graph/`, `src/ecs/`, `src/assets/`, `src/authoring/`, `src/automation/`, `src/capabilities/`, `src/diagnostics/`, `src/platform/`, `src/support/`, `include/chibi/` | First-party engine modules and the public header. | **Blocked**: M9, which depends on M5 and M6. |
| `src/ui/rml.cpp`, `test/draw_backend.cpp` | Pass-through shims to RmlUi and Cute's C++ API. | **Keep** while those libraries are used. |
| `tools/reflect/` | Native reflection generator. `sdk/native.cmake`, Makefile reflect builds. | **Blocked**: its ledger gate. |
| `test/`, `tools/benchmark.c` | C regression, differential and benchmark drivers. `make test-*`, CI. | **Blocked**: each leaves with the C it tests. |

### Node and browser JavaScript

| Files | Responsibility and callers | Decision |
| --- | --- | --- |
| `tools/collab/server/` | Collaboration room service. `test/tools/collab.c`, `docs/collaboration.md`, Studio's client. | **Blocked**: its ledger gate. |
| `docs/reviews/studio-ux-2026-10-02/performance/browser-sample.js` | Sampler for a finished Studio UX review. Only its own record. | **Owner decision**: delete with the stale record. |

Only Python that Blender imports through `bpy` belongs to the Blender exception.
`convert.py` runs under ordinary Python and is part of the migration set. Split
shared support so the Blender worker retains only what its required API needs.
Re-run the caller inventory before each deletion because open branches, CI,
Studio, or installed SDK consumers may have changed since this document was
written.
