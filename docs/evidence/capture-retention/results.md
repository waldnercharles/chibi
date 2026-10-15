# Managed-capture retention baseline results

## Result

On base `02f8453211dd4cc57965ba52c645f08a92e53c5e`, a detached captured local
retains obsolete managed snapshots across repeated writes while any task keeps
the box. The finite workload has a constant current value, so the growth is
historical retention rather than growing application data. This is not evidence
of an unreachable leak. No production code has changed in this baseline.

The corrected admitted selectors pass:

| Host | Backend execution | Cases | Assertions | Supervisor wall time |
| --- | --- | ---: | ---: | ---: |
| Linux Clang 19.1.7, ASan/UBSan | Bytecode and actual SLJIT | 109 | 7,872 | 1.813 s |
| Node v24.19.0, Emscripten 6.0.10 | Bytecode and actual Wasm | 109 | 7,860 | 0.537 s |

Both runs have the production 60-second ceiling, including test fixture setup.
Configuration and compilation are separate. The fixture explicitly verifies
`execution_module_backend` for every new scenario. Native/Node assertion counts
differ because the existing selector has platform-specific cases.

## Constant-value retention

The rows below are after 32 overwrites, while both tasks and all three captured
references still live. A fresh owning snapshot is the reference size of the
current value, not a general heap-liveness measure. Used bytes include ordinary
arena alignment; backing includes retained blocks and allocator headers.

| Host | Value | Current snapshot | Box used | Box capacity | Box backing |
| --- | --- | ---: | ---: | ---: | ---: |
| Native | 512-byte string + NUL | 513 | 34,215 | 65,151 | 65,375 |
| Wasm | 512-byte string + NUL | 513 | 33,751 | 65,151 | 65,263 |
| Native | Two strings, element 0 replaced | 1,138 | 55,365 | 65,407 | 65,663 |
| Wasm | Two strings, element 0 replaced | 1,102 | 53,556 | 65,407 | 65,535 |

Bytecode and the actual compiled backend agree within each host. The small
native/Wasm differences reflect layouts/alignment and headers. Full start,
8-write, 16-write, 32-write and cleanup measurements are in
`native-baseline-02.log` and `wasm-baseline-02.log`.

The attached-string control has no box arena allocation during the 32 writes.
At updates 8, 16 and 32, activation payload/capacity is stable at 7,677/13,833
native and 7,397/13,553 Wasm. When its declaring frame finishes, its one remaining
keeper gets a single 513-byte snapshot (545 native/529 Wasm backing).

## Lifetime and value correctness

- After writer completion, the remaining keeper reads the last value correctly
  and retains exactly the prior box backing allocation.
- After writer cancellation at 16 writes and one update, two writer refs are
  released while the keeper still reads the correct value. Backing is 32,511 B
  native / 32,415 B Wasm for strings; 32,799 B / 32,687 B for arrays.
- Final-holder cancellation stops execution immediately, but its finished
  activation and held box remain until the next program safe point. The case
  checks both this state and exactly one subsequent update; then zero holders
  and zero activation payload/capacity remain.
- Normal final-holder completion likewise leaves zero holders and payloads.
- Owner detachment at 16 writes leaves zero instances, activations and payloads.
- Original and eighth-write state snapshots survive subsequent writes and task
  cleanup. Every string byte is checked, including the unchanged second array
  element. Alias reads reach the current shared box value.

## Scope and caveats

These are internal retained-storage observations, not process RSS or LSan proof.
LSan is unavailable under this host's tracing; native runs explicitly use
`ASAN_OPTIONS=detect_leaks=0`. ASan and UBSan pass. Source inspection confirms
`box_release` frees the arena and box at the final holder; post-release tests
observe no remaining roots and do not dereference freed storage.

The selected native and Node-hosted Wasm paths passed. Browser execution, Studio,
other managed shapes, reload, large-to-empty shrink, whole-language aggregates,
full native/render suites and real editor performance were not run or implied.
The three cases preserve every original assertion in the selected dispatcher.
This baseline does not change capture syntax, value ownership or allocation APIs.

## Reproduction

Use the existing dot cloud filesystem and pinned sources. From this worktree:

```sh
source /workspace/shared/chibi-language-foundations/native-env.sh
cmake -S test/language -B build-language -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -Wl,--gc-sections' \
  -DFETCHCONTENT_SOURCE_DIR_CHIBI_EXECUTION_SLJIT_SOURCE=/workspace/shared/chibi-language-foundations/chibi/build-language-clang/_deps/chibi_execution_sljit_source-src \
  -DFETCHCONTENT_SOURCE_DIR_CHIBI_LANGUAGE_TREE_SITTER=/workspace/shared/chibi-language-foundations/chibi/build-language-clang/_deps/chibi_language_tree_sitter-src
cmake --build build-language -j4
ASAN_OPTIONS=detect_leaks=0 CHIBI_LANGUAGE_BUILD_DIR=build-language \
  python3 tools/test_policy.py tool language-program

source /workspace/shared/chibi-language-foundations/emsdk/emsdk_env.sh
emcmake cmake -S test/language -B build-wasm-language -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_CHIBI_EXECUTION_SLJIT_SOURCE=/workspace/shared/chibi-language-foundations/chibi/build-language-clang/_deps/chibi_execution_sljit_source-src \
  -DFETCHCONTENT_SOURCE_DIR_CHIBI_LANGUAGE_TREE_SITTER=/workspace/shared/chibi-language-foundations/chibi/build-language-clang/_deps/chibi_language_tree_sitter-src \
  -DFETCHCONTENT_SOURCE_DIR_CHIBI_MATH_SOURCE=/workspace/shared/chibi-language-foundations/chibi/build-language-clang/_deps/chibi_math_source-src \
  -DFETCHCONTENT_SOURCE_DIR_CHIBI_UI_HTML=/workspace/shared/chibi-language-foundations/chibi/build-language-clang/_deps/chibi_ui_html-src \
  -DFETCHCONTENT_SOURCE_DIR_CHIBI_UI_CSS=/workspace/shared/chibi-language-foundations/chibi/build-language-clang/_deps/chibi_ui_css-src
cmake --build build-wasm-language -j4
CHIBI_WASM_BUILD_DIR=build-wasm-language \
  python3 tools/test_policy.py tool wasm-program
```

The existing native environment supplies dependency cache paths. No dependencies
were added. The two ignored build directories are retained for the separately
reviewed next step. There are no new standalone binaries or project copies.

## Admission history

The coordinator found the initial fixture capability-ID collision before any
execution; ID 63 was independently verified and used. The first admitted runs
are preserved as `*-baseline-01.log`: all original 106 cases passed, three new
cases failed due missing scalar defaults and premature cancellation cleanup
assertions. Their wall times were 1.953 s native and 0.527 s Node. Those fixture
issues were fixed and independently re-admitted in staged patch SHA-256
`349e4dcbec71e3d67b135994d08473ac177c10c50b99cfbd9189d42cc9244f4d`.
No failed outcome is suppressed or recast as a runtime fix.

## Exact clean-head verification

The test-only baseline is commit
`ec170c73e6c6ed7165c97837ec55ac7864f66d88`. After verifying a clean worktree,
both complete selectors were rerun against that exact source tree. The retained
`native-exact-head.log` and `wasm-exact-head.log` contain their full output.
Only evidence documentation/log files were added afterward; runtime/test source
and both built executables are unchanged.
