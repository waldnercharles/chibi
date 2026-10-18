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
| Build-host process execution | `src/host/process.c`, `test/language/BuildWorkflow.chibi`, `test/language/build_workflow.c` | Supply direct argv, cwd, allowlisted environment, bounded output and deadline, error mapping, cancellation, and process-group reaping. Keep it out of game catalogs. | Recording parity plus real spawn, cwd/env, empty and special argv, output cap, timeout, cancellation, and descendant cleanup cases. |
| Native configuration | `tools/configure.sh`, `tools/configure_native.sh`, `tools/configure_web.sh` | Chibi build modules preserve explicit compiler, toolchain, cache, and user-definition precedence and correct invalidation. | `test/tools/package.c`, including warm configuration and web SDK recipe cases; clean and warm profile builds. |
| Dependency acquisition and cache | `tools/dependency_package.py`, `tools/fetchcontent_cache.py`, `cmake/dependencies.cmake`, `cmake/dependency_package/` | Preserve exact dependency identities, applied patches, pristine snapshots, interprocess locking, atomic completion, and corrupt-cache rejection. | Direct cold, warm, concurrent, interrupted, and corrupt cache fixtures plus current package/configuration cases. |
| Build logs and command display | `tools/quiet.py`, Makefile `QUIET` | Build host owns bounded capture, full failure diagnostics, log retention, pruning, concurrent writers, and signal cleanup. | Focused native fixtures for success, large output, failure, interruption, pruning, and concurrent commands. |
| SDK and runtime packaging | `tools/package_sdk.py`, `sdk/CMakeLists.txt`, `sdk/*.cmake` | Chibi modules assemble installed outputs, enforce target/header/object ownership, retain licenses and identity, and work without source checkout. | Admitted `package`, `package-ownership`, and `desktop-dependencies`; source-free and relocated installed SDK consumers. |
| Player and Studio packaging | `tools/package_player.py`, `tools/package_studio.py` | Preserve package-kind checks, checksums, relocatable resources and RPATHs, platform executables, and atomic staging. | Existing package cases plus installed player startup and affected Studio startup/interaction checks. |
| Game runtime installation | `tools/install_game_runtime.py`, `tools/kdl_asset.py` | Shared Chibi document code preserves unrelated content, rejects corrupt or foreign ownership, and updates only registered managed files. | Existing package installation preservation and corruption cases from `test/tools/package.c`. |
| Native reflection and generated code | `tools/reflect/`, `sdk/native.cmake`, `src/runtime/component_ids.tsv`, `cmake/tests/generation*.cmake` | Keep the native host generator until a Chibi replacement reproduces IDs, layouts, bindings, and transactional failure, including native-host generation for Emscripten targets. | Reflection range cases, generation identity/unchanged/failure fixtures, handle pins, JIT identity, native and cross-build use. |
| Internal build graph | root `CMakeLists.txt`, `cmake/`, `sdk/`, and component/test `CMakeLists.txt` files | Chibi build graph reproduces profiles, generated prerequisites, native/cross compile, include/link ownership, optional modules, and install layouts. | Native compiler, runtime-only, browser, test-driver, installed SDK, relocated SDK, and manifest comparisons. |
| CMake consumer interface | installed `ChibiConfig.cmake`, `ChibiTargets.cmake`, and related files | Treat this separately from the internal graph. Generate and retain it while Studio or another supported CMake consumer needs it. | Build Studio and representative consumers against only a relocated installed SDK. |
| Test policy and agent supervision | `tools/test_policy.py`, `tools/agent.py`, `tools/validation_plan.py`, `tools/gpu_capabilities.py`, `src/automation/execution_policy.*` | Preserve inherited shared deadlines, quarantine, exact admission, cancellation, process-tree cleanup, diagnostics, and reviewed Git identity. | `test/tools/test_policy.c`, `test/tools/validation_plan.c`, execution-policy cases, and re-admission of each newly selected workload. |
| Release publication | `.github/workflows/publish.yml`, `.github/workflows/ci.yml`, package helpers | Keep workflow scheduling and credentials in hosted YAML. Move reusable validation and assembly policy into `.chibi` programs runnable on clean workers. | Immutable-release rejection, exact assets, build identity, checksums, engine revision, and matching native/web SDK generations. |
| Browser compiler isolation | `tools/web_env.sh`, `tools/web_compiler.sh`, Emscripten configuration | Preserve isolation between native host generators and target compiler environments, and matching compiler/player output. | Native contamination rejection plus serial/threaded browser output ownership and browser compiler/player matching. |
| C source formatting | `tools/clang-format.sh`, `.githooks/pre-commit`, CI | Keep external clang-format 22 while maintained C exists. `chibi fmt` replaces authored-source and asset formatting, not C formatting during migration. | Existing C format check and a separate canonical authored-format corpus. |
| Blender worker | `tools/mesh_sdf/worker.py`, Blender-only `bpy` calls, and the worker's imported encoding support | Python may remain only for code that executes inside Blender because Blender requires its Python API. | Deterministic conversion, malformed geometry, metadata, work limits, and source provenance cases. |
| Blender launcher and publication | `tools/mesh_sdf/convert.py` and ordinary-Python support in `tools/mesh_sdf/atlas.py` | Move subprocess, deadline, resource-limit, staging, hashing, validation, and publication policy to Chibi. Keep only the thin Blender worker and support it must import. | Timeout and process cleanup, memory/work limits, atomic destination publication, provenance, deterministic atlas bytes, and corrupt/partial output rejection. |
| Retired web publisher | `tools/publish_web.py`, `tools/package_web.py`, `test/tools/publish.c` | Recheck all callers and open publishing work. Move still-current identity, corruption, and concurrency contracts into the replacement before deletion. | Required publication identity, corrupt input, concurrent publication, and disabled-caller checks. |

## Installed Python surface

`sdk/CMakeLists.txt` currently installs these scripts into SDK products:

```text
tools/package_sdk.py
tools/package_player.py
tools/package_studio.py
tools/install_game_runtime.py
tools/kdl_asset.py
```

Remove an installed script only when the replacement is present in the installed
SDK and its source-free consumer checks pass. A wrapper that still requires the
engine checkout or Python has not crossed this gate.

## Current project-owned script inventory

The current top-level migration set is:

```text
tools/agent.py
tools/dependency_package.py
tools/fetchcontent_cache.py
tools/gpu_capabilities.py
tools/install_game_runtime.py
tools/kdl_asset.py
tools/package_player.py
tools/package_sdk.py
tools/package_studio.py
tools/package_web.py
tools/publish_web.py
tools/quiet.py
tools/test_policy.py
tools/validation_plan.py

tools/clang-format.sh
tools/configure.sh
tools/configure_native.sh
tools/configure_web.sh
tools/web_compiler.sh
tools/web_env.sh
```

Only Python that Blender imports through `bpy` belongs to the Blender exception.
`convert.py` runs under ordinary Python and is part of the migration set. Split
shared support so the Blender worker retains only what its required API needs.
Re-run the caller inventory before each deletion because open branches, CI,
Studio, or installed SDK consumers may have changed since this document was
written.
