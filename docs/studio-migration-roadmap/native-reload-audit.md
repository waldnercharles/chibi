# Native reload residual audit

15 October 2026. Read-only source/build audit, not a runtime test. This closes the narrow B06 question for the exact snapshots below; it does not claim that future changes or unexamined external/custom builds cannot add another loader.

## Result

Native application hot replacement is already removed at the audited engine/Studio snapshots. No new native-reload deletion feature is justified. Keep `.chibi`/authored-asset publication and state/callback-generation safety, workspace checkpoint recovery, generated-code testing and the cleanup of retired build settings.

## Exact scope

- Engine baseline `02f8453211dd4cc57965ba52c645f08a92e53c5e`; current merged main `1de4810d49b3acf4ead7d910d0abd2b042293e18` changes only source minimizer, its format tests and test admission, leaving inspected native-lifecycle paths unchanged.
- Studio `78cb4d4c47242db9f7af2b9573653d9756050e99`.
- Read engine Makefile, CMake/build sources, native host/tool paths, current reload documentation, source publication and execution replacement. Search tracked non-vendored engine source/build/tools/tests for `CHIBI_DEVELOPMENT_RELOAD`, `chibi_dll`, `check-reload`, `check-dev`, `dlopen/dlsym/dlclose`, Windows loading APIs and SDL object-loading APIs. Inspect remaining hits rather than assuming every dynamic load is app reload.
- Read Studio CMakeLists and relevant host/application lifecycle source through GitHub, checking direct linkage and absence of loader calls. The complete baseline inventory identifies all host/product translation units; this was not a new full-body audit of all 155 sources.

## Evidence

1. [Engine Makefile, lines 167–181](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/Makefile#L167-L181): `studio-build` configures/builds the separate consumer; `studio-run` and `studio-dev` launch the same `bin/chibi-dev`. The dev path adds `--checkpoint`, not a watched application library.
2. [Studio CMakeLists, lines 64–94](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/CMakeLists.txt#L64-L94), fetched blob `a99d57a1c990b6b166c87078ab88e5adc61c61ab`: `studio` is STATIC; executable includes host/main, state_file, stacktrace and registry, directly linked to `studio` and SDK libraries.
3. [Studio host main](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/host/main.c), blob `1a20a6ecb90909414d522669d1170dacbe4b8b26`: direct `chibi_application_create`/`destroy`, checkpoint fallback, no native library loader.
4. [Studio application](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/application.c), blob `bded23eb1e0f9c9dcb6e2dc46b9a50e4f42bd906`: directly linked create/frame/destroy lifecycle; no native application code-replacement entry.
5. [Current reload documentation](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/docs/reload.md): CUT65 removed application DLL, watcher, staging descriptors and native reload targets. C changes rebuild/restart; web C/Wasm rebuild/refresh. Authored updates and checkpoints remain. [Historical refactor plan](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/docs/refactor-plan.md#L1-L6) explicitly labels its old reload implementation historical and removed.

## Remaining references that should stay

- [Dependency cache argument cleanup](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/tools/dependency_package.py#L163-L174) removes the retired `CHIBI_DEVELOPMENT_RELOAD` key before reusing old package arguments. It does not enable reload. Deleting this cleanup could resurrect an obsolete option in inherited arguments.
- [Generated C export test](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/tools/c_export.c#L479-L528) compiles a temporary shared library and loads generated functions to verify C-emitted operand/continuation behavior. This is compiler-output validation, not application hot replacement. The associated `dlopen/dlsym/dlclose` calls are not removal candidates under the user's request.
- External dependency libraries may be static/shared/imported for ordinary linking. Such CMake type declarations are not evidence of a hot-reload system.
- [Authored source commit](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/runtime/behaviors/source_publication.c#L260-L280) still stages and commits source modules only when execution is not in flight.
- [Execution replacement](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/program.c#L2779-L2825) validates expected old module, cancels old system work, rebinds owner/program references and frees old generations. These safety mechanisms are relevant to `.chibi` edits, state and captured callbacks and must not be removed as native-reload machinery.

## Disposition

B06 is complete as a bounded read-only audit with **no native hot-reload implementation removal needed** at these snapshots. The broader migration still needs its `.chibi` state/layout/callback-publication tests and ordinary workspace recovery. Do not create a deletion PR solely to satisfy an earlier assumption. If future source discovery identifies a live native replacement path, reopen the audit with its exact caller/build target and shared dependencies before changing it.

No code or build configuration changed; no application/test binary was executed.
