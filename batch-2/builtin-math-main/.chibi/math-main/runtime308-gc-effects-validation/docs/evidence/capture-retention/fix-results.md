# Bound detached captured-value storage

## Change and measured result

Detached captured locals now publish writes transactionally. The logical box
and captured paths keep their identity; a local candidate root receives the
existing write semantics, then a fresh owning snapshot becomes the committed
root. Previous storage is released only after that snapshot succeeds. Failed
writes discard their temporary storage and preserve the prior value and arena.
The attached activation path, task/box lifetime, inspector APIs, authored syntax
and public ABI are unchanged.

The independently reviewed production change is confined to 24 added lines in
`src/execution/intrinsics.c`; its Git blob is
`ab0b4724d696d11dfd352d8d6657a9720ff46958`.

After 32 fixed-size overwrites, backing bytes are:

| Host and current value | Baseline | Fixed | Current owning snapshot |
| --- | ---: | ---: | ---: |
| Native, string | 65,375 | 545 | 513 |
| Node Wasm, string | 65,263 | 529 | 513 |
| Native, two-string array element | 65,663 | 1,891 | 1,138 |
| Node Wasm, two-string array element | 65,535 | 1,843 | 1,102 |

Fixed storage has the same values after 8, 16 and 32 writes. Used bytes equal a
fresh owning snapshot of the current root. The attached control's activation
payload remains at the baseline plateau. Small and empty replacements release
old large values even when the remaining capture stays idle.

This fixes measured lifetime-dependent retention. The baseline did release the
box when its final holder died; it was not established to be an unreachable leak.

## Cost and limits

A path update's working arena may still copy the old subtree. The final snapshot
removes that subtree from committed storage. This deliberately conservative
implementation adds transient allocation/copy work and is not a throughput
optimization. For the native 8,192-byte-string fixtures:

- Self-replacement: 8,513 B retained before/after, five xalloc calls requesting
  33,444 B during the write.
- Replacement with `small`: 8,513 B to 288 B retained, three xalloc calls requesting
  8,801 B during the write.
- Nested scalar-field update beside an 8,192-byte string: 24,931 B retained
  before/after, six xalloc calls requesting 49,862 B per update.

Requested allocation bytes are not bytes copied, process peak/live memory, or
allocator overhead outside xalloc. Old, working and committed arenas coexist
transiently until success; no general heap/RSS claim is made. Future reuse or
copy-elision work needs its own semantic proof and measurement.

## Validation and negative control

The corrected bounded program selectors pass all 114 cases:

| Build | Assertions | Production-supervisor wall time |
| --- | ---: | ---: |
| Native ASan/UBSan, bytecode and actual SLJIT | 8,249 | 2.033 s |
| Node, bytecode and actual Wasm | 8,237 | 0.518 s |

Logs are `native-fixed-02.log` and `wasm-fixed-02.log`. Added coverage includes
whole-value shrink to small/empty followed by idle; nested scalar writes beside
managed siblings; self/sibling-source aliasing; pre-existing captured paths
across replacement/shrink; unchanged root and storage after invalid paths or
snapshot rejection; descendant task capture; recorded inspector snapshots across
pause/resume and cleanup; managed roots keeping task handles/results; and the
original shared-holder, escaped-snapshot, cancellation and attached controls.

Restoring only the baseline production intrinsic
`72f8a85ab8f217f4c3cc538663ab272153a80fc5` makes seven regression cases fail, with
107 cases passing, in 2.258 s. Every failure is a retained-storage assertion,
including shrink and nested scalar writes. `native-negative-control.log` preserves
the complete output. `negative-control-source.log` records restoration of the
reviewed fixed blob before rebuilding.

The frozen, clean candidate
`760fb3138ac6c359f0fa3a635067ebeb58eb51fe` then passed broader interaction checks:

| Existing command | Cases | Assertions | Production-supervisor wall time | Log |
| --- | ---: | ---: | ---: | --- |
| Native language aggregate, 28 selectors | 345 | 21,952 | 14.572 s | `native-language-group.log` |
| Node Wasm aggregate, 16 selectors | 204 | 20,388 | 2.859 s | `wasm-group.log` |
| Prepared producer plus runtime-only consumer | 16 | 746 | 0.210 s | `prepared-runtime.log` |
| Generated-C alias/debug continuation | 1 | 997 | 0.592 s | `generated-c.log` |
| Complete standalone tooling/process aggregate | 116 | 28,185 | 21.859 s | `tooling-group.log` |

Cases/assertions in multi-process commands are summed from the individual C
pico_unit summaries. The native language and Node groups include the new capture
cases. The tooling aggregate includes its own generated-C case; the focused run
above is separate evidence, not another unique case. Its expected policy-rejection
and publication-refusal output comes from negative tests; the aggregate exits
successfully with zero failed cases. Each command ran separately, serially, under
its unmodified production supervisor. Configuration/build logs are separate;
these times exclude them. All binaries came from this candidate's worktree.
The compiler-enabled tooling build reused pinned dependency sources with
`FETCHCONTENT_FULLY_DISCONNECTED=ON`; no dependency retrieval was needed.

Baseline provenance is test-only commit
`ec170c73e6c6ed7165c97837ec55ac7864f66d88`, followed by evidence-only commit
`c0ad2e8ade20ede5a8aae318696b5fef6306f56e`, rooted at
`02f8453211dd4cc57965ba52c645f08a92e53c5e`. No source, test or supervisor changed
between freezing the candidate and the broader runs.

Native leak detection is disabled because LSan is unavailable under the traced
validation host; ASan/UBSan pass. Node-hosted Wasm is actual compiled execution,
not browser validation. Full renderer/Studio coverage and editor performance
are not claimed. Build/configuration timing is excluded from test timing.

The first expanded fixture run is retained in `*-fixed-01.log`. It exposed three
fixture setup mistakes: a reserved state name, a task array missing its result
type, and unpaired project-catalog teardown. Those were independently reviewed
and corrected without changing production behavior or weakening assertions.
The earlier baseline failures and their corrections remain in the baseline
report and logs.

## Portable reproduction

From a normal checkout, use Clang 19 or a compatible supported C compiler,
CMake 3.25+, Emscripten 6.0.10, and Node (measured with v24.19.0). Build dependencies
remain pinned by the repository, including SLJIT
`a0020dd49a21020434ff838fc308626c48cd8f05` and Cute math
`96e6034aa6bc44665856ee480962a9ddab0b87cc`. No dependency was added by this change.

```sh
CC=clang-19 cmake -S test/language -B build-language \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -Wl,--gc-sections'
cmake --build build-language -j4
ASAN_OPTIONS=detect_leaks=0 CHIBI_LANGUAGE_BUILD_DIR=build-language \
  python3 tools/test_policy.py tool language-program

# EMSDK is the caller's installed Emscripten SDK root.
. "$EMSDK/emsdk_env.sh"
emcmake cmake -S test/language -B build-wasm-language -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm-language -j4
CHIBI_WASM_BUILD_DIR=build-wasm-language \
  python3 tools/test_policy.py tool wasm-program
```

On hosts where LSan works, omit the `detect_leaks=0` override. The Linux
`--gc-sections` linker flag may be replaced by the platform's dead-strip option.
For a fully offline build, set `CACHE_ROOT` to caller-provided pinned source
checkouts and add the supported CMake flags below to each configure command:

```sh
-DFETCHCONTENT_FULLY_DISCONNECTED=ON \
-DFETCHCONTENT_SOURCE_DIR_CHIBI_EXECUTION_SLJIT_SOURCE="$CACHE_ROOT/sljit" \
-DFETCHCONTENT_SOURCE_DIR_CHIBI_LANGUAGE_TREE_SITTER="$CACHE_ROOT/tree-sitter" \
-DFETCHCONTENT_SOURCE_DIR_CHIBI_MATH_SOURCE="$CACHE_ROOT/cute-framework" \
-DFETCHCONTENT_SOURCE_DIR_CHIBI_UI_HTML="$CACHE_ROOT/tree-sitter-html" \
-DFETCHCONTENT_SOURCE_DIR_CHIBI_UI_CSS="$CACHE_ROOT/tree-sitter-css"
```

Build the standalone tooling driver from the same checkout for the broader
checks. The same optional cache-source flags above apply to this configuration.

```sh
CC=clang-19 cmake -S test/tools -B build-tools \
  -DCMAKE_BUILD_TYPE=Debug -DCHIBI_SDK_BUILD_COMPILER=ON \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -Wl,--gc-sections'
cmake --build build-tools -j4

ASAN_OPTIONS=detect_leaks=0 CHIBI_LANGUAGE_BUILD_DIR=build-language \
  python3 tools/test_policy.py group language
CHIBI_WASM_BUILD_DIR=build-wasm-language \
  python3 tools/test_policy.py group wasm
ASAN_OPTIONS=detect_leaks=0 CHIBI_TOOLS_BUILD_DIR=build-tools \
  CHIBI_LANGUAGE_BUILD_DIR=build-language \
  python3 tools/test_policy.py tool language-prepared
ASAN_OPTIONS=detect_leaks=0 CHIBI_TOOLS_BUILD_DIR=build-tools \
  python3 tools/test_policy.py tool c-export-alias
ASAN_OPTIONS=detect_leaks=0 \
  python3 tools/test_policy.py short "$PWD/build-tools/test_tools"
```

No binaries from another branch are valid evidence. Raw historical logs retain
their original paths for provenance.
