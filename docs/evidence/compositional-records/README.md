# Compositional records validation

This is the historical proof for frozen commit `bb7839acdf762a722e89b0c073f58aeda4234139`.
The source-only forward port deliberately retains the accepted baseline's
format-26 checked artifacts. Its current proof and remaining publication gates
are recorded in [the integration ledger](integration-7c6ca2f5.md).

Local, unmerged work on `codex/compositional-records-on-694-20261028`, based on
`19ad570dd12acb13de066c31a9c791b018c60aa7`. These receipts describe that pinned
combination and its local changes. They do not establish compatibility with
current main or the later #694 head `91aea4ee`.

## Affected contracts

The slice adds inline records and array fields containing scalars, non-null
pointers or records. Checks cover construction, forward declarations, nested
mutation, value-copy isolation, pointer identity and field-address identity,
call/return ownership, generic child layouts, and bytecode/SLJIT parity through
both compiler stages. Native boundary cases cover retained child metadata,
producer destruction, prepared round trips, malformed child references,
layout-depth and metadata-work bounds, and hot replacement with compatible or
changed child layouts. Prepared format is 27; the compiler artifact must match.

Independent source reviews of compiler, runtime ownership and prepared metadata
reported no remaining source blockers after the fixes. Map/union fields,
nested-container element types and full LLVM aggregate lowering remain outside
this slice.

## Recorded checks

Logs remain outside the repository, under the worktree's parent directory
`/Users/cwaldner/Personal/chibi/.worktrees/compositional-records/`.
Test wall times include fixture preparation inside the test process and exclude
external configuration/build time.

| Check | Result | Test wall time | Log |
| --- | --- | ---: | --- |
| Final Release composition | 9/9 cases, 9,702 assertions; 12 authored tests per VM plus stage-one execution | 21.709s | `nominal-final-language-compositional-records.log` |
| Release asset compiler host | 11/11 | 48.239s | `candidate-language-asset-compiler-host.log` |
| Final Release native group | 649/649 across 72 selector summaries | 4.569s | `nominal-final-native.log` |
| Final Release materials group | 104/104 across 7 selectors | 1.013s | `nominal-final-materials.log` |
| Generated graph and test registrations | 8/8 | 106.226s | `candidate-graph-generated-final.log` |
| Final Release prepared | 16/18 with the two inherited failures below | 0.477s | `nominal-final-language-record-prepared.log` |
| Final child-identity regression and types | 42/42, 5,103 assertions; regression fails before the fix | 1.129s | `child-nominal-green-types.log`; red: `child-nominal-red-types.log` |
| Final ASan/UBSan types | 42/42, 5,103 assertions | 4.663s | `nominal-final-asan-types.log` |
| Standalone tooling/process integrations | 235/237; two inherited C-export failures below | 193.358s | `candidate-tools.log` |
| Final ASan/UBSan composition | 9/9 cases, 9,702 assertions, including array-field append and pointer rebinding | 237.757s | `nominal-final-asan-composition.log` |
| ASan/UBSan query program | 88/88 | 1.468s | `final-asan-language-query-program.log` |
| ASan/UBSan prepared | 16/18; same two inherited failures below | 1.668s | `final-asan-language-record-prepared.log` |
| Release semantics / queries / types | 76/76; 88/88; 41/41 | 0.121s; 0.178s; 1.140s | `final-language-semantic.log`, `final-language-query-program.log`, `final-types.log` |
| Bootstrap self-compilation | 1/1; 30,353 assertions | 395.849s | `final-bootstrap-check.log` |

Additional earlier receipts passed: record semantics 10/10, record runtime 9/9,
record documents 5/5, execution 83/83, program 205/205, modules 22/22, native calls
7/7 per backend, and the typed/collection package compiler selectors 1/1 each.
Their `release-language-*.log` receipts are in the same directory. Format checking
passed in `final-check-format.log`.

The matching `tools/compiler/bootstrap/compiler.chibip` is **21,009,142 bytes**,
SHA-256 `bd0b71ff4029137227f0daac7dbda76969fd48da19ecf6166dd7584046681add`.
The artifact and manifest were checked directly; packaging is recorded in
`final-bootstrap-package.log`. `candidate-bootstrap-manifest-check.log` verifies
the artifact hash and every source hash against the final compiler sources.

## Inherited failures at the old pin

| Selector | Baseline | Feature | Evidence |
| --- | --- | --- | --- |
| `language-record-prepared` | 14/16 | 16/18 | `baseline-language-record-prepared.log`, `final-language-record-prepared.log` |
| `language-record-state` | 1/3 | 1/3 | `baseline-language-record-state.log`, `release-language-record-state.log` |
| `language-prepared-writer-chibi` | 1/3 | 1/3 | `baseline-language-prepared-writer-chibi.log`, `release-language-prepared-writer-chibi.log` |
| `language-record-values` | exit 139, no summary | exit 139, no summary | `baseline-language-record-values.log`, `final-language-record-values.log`; supervisor exit status recorded separately |
| `language-record-compatibility` | 1/2 | 1/2 | `baseline-language-record-compatibility.log`, `final-language-record-compatibility.log` |
| `c-export-alias` | 0/2 | same two failures in the 235/237 tooling run | `baseline-c-export-alias.log`, `candidate-tools.log` |

The prepared failures are the array-owner case without an initialized managed
heap and the existing malformed-header proof assertion. The feature adds two
passing cases. This comparison identifies failures reproduced at the exact old
pin; it says nothing about whether later #694 revisions retain or fix them.
The C-export fixtures produce 146,094 bytes against their 131,072-byte bound;
the separate arm64 link fails on `behavior_function_finish_values` and
`behavior_function_value_arguments`. The unchanged baseline reproduces both
failures in 2.475s, and the exporter and fixture sources are unchanged here.

## Build timing and outstanding verification

Source-driver recovery builds disabled the asset compiler component while
generating the matching bootstrap. The final recorded Release rebuild took
4.7s configuration and 3.4s compilation; the ASan/UBSan rebuild took 3.6s and
4.3s respectively. See `final-release-build.log` and `final-asan-build.log`.
These are incremental build timings, separate from the test times above.

The normal asset-enabled `make build` completed successfully in
`final-normal-build.log`: 113.5s for the native-toolchain orchestration, including
its configuration/build work. The wrapper does not separate those phases.
Candidate format checking passed in 27.6s in `candidate-check-format.log`.
The final full format check passed in 27.1s (`nominal-final-check-format.log`),
and changed authored compiler/fixture sources pass `chibi fmt --check`.
The final asset-enabled rebuild after the recursive child-identity guard passes
in 100.6s (`nominal-final-normal-build.log`). Its ASan/UBSan source-driver rebuild
took 4.1s configuration and 6.0s compilation (`nominal-final-asan-build.log`).

The final Release fixture includes the pointer-field rebinding assertion and
checks array contents across hot replacement: the changed child uses its new
initial value while the compatible sibling array preserves its elements.
The last native change rejects distinct authored child identities beneath equal
parent IDs/layout hashes, including mixed frozen/legacy parents; native carried
and bare catalog layouts remain compatible. Its new regression fails before
the fix (41/42) and passes afterward (42/42), with an independent source review.
The broader tooling and graph receipts precede this narrow guard. Compiler
source and the matching bootstrap are unchanged by it.
The final sanitizer composition run includes all fixture assertions, including
appending to a copied array field and checking both lengths independently.

No full-suite, projects group, additional-platform, LLVM machine-code, Studio or
GPU validation is claimed. `candidate-doctor.log` reports the Vulkan loader
unavailable. Installed SDK consumer validation and reconciliation with current
main/latest #694 are also unverified here.

## Representative commands

Run from the engine worktree. Recovery deliberately uses the source build route;
normal product validation must restore `CHIBI_SDK_BUILD_ASSET_COMPILER_HOST=ON`.

```sh
make stage-zero-build GRAPH_BUILD= BUILD_DIR=build-records-release \
  BUILD_TARGET='test_language test_tools chibi-compiler-host' \
  LOCAL_CMAKE_FLAGS='-DCMAKE_BUILD_TYPE=Release -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_SDK_BUILD_ASSET_COMPILER_HOST=OFF'
CHIBI_LANGUAGE_BUILD_DIR=build-records-release/test/language \
  python3 tools/test_policy.py tool language-compositional-records
CHIBI_LANGUAGE_BUILD_DIR=build-records-asan/test/language \
  python3 tools/test_policy.py tool language-compositional-records
CHIBI_TOOLS_BUILD_DIR=build-records-asan/test/tools \
  python3 tools/test_policy.py tool types
CHIBI_RELEASE_LANGUAGE_BUILD_DIR=build-records-release/test/language \
  python3 tools/test_policy.py tool language-package-compiler-chibi-bootstrap
CHIBI_BUILD_HOST=build-records-release/sdk/chibi-build-host \
  CHIBI_GRAPH_REFERENCE=build-records-release \
  CHIBI_TOOLS_BUILD_DIR=build-records-release/test/tools \
  python3 tools/test_policy.py tool build-graph-generated
shasum -a 256 tools/compiler/bootstrap/compiler.chibip
```

Local scratch remains in `build-records/`, `build-records-release/` and
`build-records-asan/`, with their `.chibi/logs/` diagnostics. The parent directory
retains the isolated `baseline/` checkout, `ccache/` and validation logs. These
paths are local evidence, not committed products.
