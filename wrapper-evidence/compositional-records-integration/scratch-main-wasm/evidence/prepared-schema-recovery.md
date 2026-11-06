# Prepared schema candidate recovery

## Recovery provenance

The original shared workspace, builds and raw validation logs were lost. This
recovery starts from the surviving full main bundle at
`02f8453211dd4cc57965ba52c645f08a92e53c5e` (tree
`2e568f24f2833dfeec3103f63a5068500b0fa4f8`) in an isolated worktree. No held
feature branch is imported.

The five production/test files were restored from retained implementation text,
then each complete file was compared with the surviving publication manifest.
All five Git blob identities match the original approved candidate exactly:

| File | Recovered Git blob |
| --- | --- |
| `src/execution/instruction.h` | `3a377930dbf6ca3565024948772116c003d12ea5` |
| `src/execution/prepared.c` | `4c202231ba3c7047491d17e6484a638bc06c95f8` |
| `src/execution/prepared.h` | `f842b99d1be03d1e3ef3936cb72edd9f9041b86d` |
| `src/execution/prepared_internal.h` | `9e7bca98ba158e8a14a925aae0711a3e9e84079e` |
| `test/language/execution.c` | `b68c04bbdace5914bd82956af5846565dd62b2ed` |

The full source/test diff has SHA-256
`6e94d97dfba60d7f767da1257df52aa06bc0e739d8d541629ee91e05833246f4`, matching the
original reviewed source. The original final commit was
`2f939189708743919dbd81c17f5adc4c5ca70e39`, but its commit/evidence objects and
builds are not present in this recovery. Source identity does not transfer its old
execution results to this environment. Fresh independent review and validation
are required before publication.

The exact original negative-control patch was also recovered at
[`prepared-schema-recovery/admission-baseline.patch`](prepared-schema-recovery/admission-baseline.patch),
SHA-256 `52b255d5bb3467deb2fa08e8ad34a5e5a2490fc4f89b1e9f865973f8316197fa`.
It preserves the original layout-only `context_abi` and header writer/checker,
while adding the enum declaration refactor, private schema helper and regression.
This patch is reproducible against the base above. Historical raw execution logs
have not been recreated from summaries; new runs will have new logs.

## Unchanged compatibility design

One declaration supplies all 118 original opcode names and numeric values,
including the existing math expansions. Both the enum and prepared stable-name
table derive from it; all original ordinals remain unchanged. The existing
context/layout FNV-1a hash is extended with a NUL-terminated fixed domain
`chibi.prepared.instructions`, then each NUL-terminated stable name and its
actual numeric enum index as eight explicit little-endian bytes. No pointers,
compiler identity or struct padding are hashed.

The existing four-field header and field encoding are unchanged. The expected
identity is computed once and used for both writing and checking. Manual
`EXECUTION_PREPARED_VERSION` remains 13: unchanged-name payload, verifier or
execution-semantic changes still require its reviewed version guard. Name/ordinal
identity is compatibility detection, not authentication, collision resistance or
proof of all semantic equivalence. No public callable API, registry, authored
syntax or project-version change is added.

The new identity rejects old layout-only artifacts. Prepared packages and imports
must rebuild together because dependency fingerprints include their serialized
headers. Rebuild from authored source rather than patching artifact headers or
opcode bytes.

## Regression admission and bounds

The recovered case uses the existing C/pico_unit `--execution` selector. It adds
no executable, selector, test framework, process, file, network, GPU or rendered
frame workload, and preserves every original assertion.

- Two mappings have the same 118 entries and maximum ordinal. Only REQUIRE and
  ASSERT stable names are swapped. Context/layout, format version and pointer
  width remain constant; the production helper must return different identities.
- One valid module contains one bool slot and exactly three instructions:
  CONSTANT(false), REQUIRE(target=BUDGET,pin=7), END. There are no arguments,
  results, state, imports, hosts, component requirements or ECS world.
- Serialized and foreign-header streams must each remain below 4 KiB. Retire the
  producer, then decode and execute the same-schema module with a 16-step budget,
  preserving DATA failure and pin 7. A swapped interpretation would instead be a
  valid ASSERT with a different failure classification.
- The baseline must fail at written-header versus derived-schema identity, after
  the valid same-schema round trip. No artificial assertion failure is inserted.
- The foreign header contains the derived swapped-schema identity, not an arbitrary
  flipped bit. It must reject with exactly `prepared target ABI differs`, restore
  the full 1 MiB read budget and issue only the existing empty-plan xalloc request.
  This establishes rejection before function/artifact decode and finalization;
  it is not an RSS or leak measurement.

All execution, including aggregates, uses the production supervisor.
Configuration/build timing remains separate. ASan/UBSan runs use
`ASAN_OPTIONS=detect_leaks=0` because the environment is traced.

## Fresh recovered-environment validation

Fresh independent source review confirms the restored blobs, full diff and exact
baseline patch. clang-format 22.1.1 dry-run and source whitespace checks pass.
Configuration/builds use the shared official toolchain recovery: Clang 19.1.7,
CMake 4.4.3, Ninja 1.13.2, Emscripten 6.0.10 and Node 24.19.0. Dependency source
pins are unchanged. Compiler and dependency recovery is shared with other tasks;
this work does not weaken warnings or install a second toolchain.

After independent coordinator review, fresh focused commands through the
production supervisor produced these results:

| Source/configuration | Command | Result | Wall time |
| --- | --- | --- | --- |
| Exact recovered original layout-only baseline, native Release | `CHIBI_LANGUAGE_BUILD_DIR=build-schema-release python3 tools/test_policy.py tool language-execution` | 21 passed, 1 expected failure, 1,022 assertions, exit 1 | 0.093 s |
| Exact recovered fixed source, native Release | Same command after restoring and rebuilding fixed source | 22 passed, 0 failed, 1,029 assertions, exit 0 | 0.062 s |
| Exact recovered fixed source, native Debug ASan/UBSan | `CHIBI_LANGUAGE_BUILD_DIR=build-schema-sanitize python3 tools/test_policy.py tool language-execution` | 22 passed, 0 failed, 1,029 assertions, exit 0 | 0.149 s |
| Exact recovered fixed source, Node Wasm Release | `CHIBI_WASM_BUILD_DIR=build-schema-wasm python3 tools/test_policy.py tool wasm-execution` | 22 passed, 0 failed, 1,025 assertions, exit 0 | 0.257 s |

The fresh baseline fails exactly at `header[3] == current_abi`, after its valid
same-schema module retired the producer and executed with DATA/pin7 classification.
There was no fixture/setup failure, changed assertion or new workload. Its raw log
and exit status are retained in `prepared-schema-recovery/`. The restored final
source diff still matches the original approved hash exactly. The Wasm positive
was explicitly admitted before the native negative control while shared Clang
recovery was pending. It does not replace the negative control.

Release, sanitizer-language, sanitizer-tools and Wasm builds all passed. The fresh
frozen candidate's selected broader matrix also passed as recorded below.
No old run is being represented as current evidence. Test sequences are coordinated
serially across workers; builds and their logs are separate from test wall times.

## Reproducing the selected checks

Use Clang 19 with its ASan/UBSan runtime for native builds, Emscripten 6.0.10 for
Wasm, CMake/Ninja and the repository-pinned dependency sources. In this recovered
workspace the shared toolchain is loaded with `../chibi-tools/env.sh`; Wasm and
generated-C checks also load `../chibi-tools/emsdk/emsdk_env.sh`. Paths below are
relative to this candidate's repository root and can be replaced with matching
local build directories.

The native language builds configure `test/language` with native/desktop SDK
packages disabled. The tools build configures `test/tools` with those packages
disabled and `CHIBI_SDK_BUILD_COMPILER=ON`. Release uses `CMAKE_BUILD_TYPE=Release`;
sanitizer builds use `Debug` and C flags
`-fsanitize=address,undefined -fno-omit-frame-pointer`. The Wasm language build
uses `emcmake cmake`, Release and native/desktop SDK packages disabled. Shared
cached source directories supply tree-sitter, CSS, HTML, sljit (native) and Cute;
no native controller or renderer dependency is fetched for these targets.

```sh
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
export CHIBI_LANGUAGE_BUILD_DIR=build-schema-sanitize
export CHIBI_TOOLS_BUILD_DIR=build-schema-tools
export CHIBI_WASM_BUILD_DIR=build-schema-wasm
python3 tools/test_policy.py group language
python3 tools/test_policy.py group wasm
python3 tools/test_policy.py tool language-prepared
python3 tools/test_policy.py tool c-export-alias
python3 tools/test_policy.py tool native-tools
```

Each supervised command includes its existing fixture setup and child processes.
These selected checks do not establish full native/render/Studio coverage. Builds
use bounded parallelism and finish before the test sequence.

## Refreshed main-based candidate

The first recovered source/evidence freeze was
`d3a13ccf0cfbc4493e4c39d6b0aa9fb6c0ab1f33` (tree
`146c53551993d6e83747fff3aabef882e7e1adfb`). Its selected matrix passed, and that
freeze and its logs remain retained locally. While validation was underway,
reviewed formatter work landed as main
`1de4810d49b3acf4ead7d910d0abd2b042293e18` (tree
`273a6b6f308a24d5b6e2455c3facd74d0c1aeda1`). The coordinator required the schema
candidate to refresh onto that main before final validation.

The clean rebase produced source freeze
`9c3d4320435a801ae7c4d686d955b1aba0bbb5eb` (tree
`32841ec618b15c5556831bc1d1e3c909a7cf599e`). All five schema source/test blobs
and the scoped diff hash above remained identical. Independent review confirmed
the parent tree equals the independently validated formatter tree, with no new
interaction blocker. No held feature was integrated.

All four own-tree builds were rebuilt successfully after the rebase. Repeated
build-currency checks reported no work, and clang-format plus scoped source
whitespace checks were clean. The following matrix then ran serially on the
refreshed source freeze, with tracked worktree/index clean before and after:

| Command | Passed cases / assertions | Wall time | Exit |
| --- | --- | --- | --- |
| `group language` | 344 / 19,140 across 28 summaries | 16.456 s | 0 |
| `group wasm` | 197 / 17,518 across 16 summaries | 3.446 s | 0 |
| `tool language-prepared` | producer 5 / 187; runtime-only consumer 10 / 554; wrapper 1 / 5 | 0.296 s | 0 |
| `tool c-export-alias` | 1 / 997 | 0.585 s | 0 |
| `tool native-tools` | 116 / 28,185 | 21.512 s | 0 |

Commands use the `python3 tools/test_policy.py` prefix and environment above;
all summaries report zero failed cases. Raw logs, exit statuses and source/tree
identity are in `prepared-schema-recovery/matrix-*`. Build output is retained
separately from execution logs. The native count increase over the old-base
matrix comes from the already-reviewed formatter main update. Source changes
after this matrix are prohibited without renewed review and affected checks;
the final evidence-only freeze is followed by independent exact-head validation.

The branch and builds remain available for independent review. This work has
not published or merged the candidate; the coordinator owns publication and any
integration decision, including required checks and explicit holds.
