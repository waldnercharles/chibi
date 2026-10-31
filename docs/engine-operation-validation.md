# PR407 selective recovery validation

Development checkpoint, 2026-10-25. This is not merge or release approval.

## Provenance and scope

The integration parent is main `d1154cdb8677cbfe479d4be40db7ca480d10f103`
(tree `fd29ffd46f613eb2a05b7b5eec05e172767d2e89`). Operation-specific source
was selectively recovered from public checkpoint
`f61db252418f0d2d01dc8f43a56f5ff97e9a5351`, branch
`codex/pr407-recovery-handoff-20261023`:

- Full source checkpoint tree: `ec404482ec984c4266923d9284ee9e2b6e1c7121`.
- Tree excluding its recovery metadata: `fa24367aa5284d67160118716c538207d3672cd8`.
- All 84 active-source file SHA-256 and byte-length entries in its
  [manifest](https://github.com/waldnercharles/chibi/blob/f61db252418f0d2d01dc8f43a56f5ff97e9a5351/.handoff/pr407-20261023/manifest.json)
  were verified locally.
- The checkpoint copied `d0de4a84ecf50badcab661411470bdf6f3da5fa2` content
  without that commit's ancestry. This recovery does not represent it as a merge
  or cherry-pick. Historical receipts do not validate this candidate.

Retained scope: Owner/Delay closed declarations, dormant/scalar metadata
validation, generated opcode bindings, known-operation ABI identities,
incremental identity comparison, lowerer validation, and the narrow migration-only
Self effect adapter. The SDK exports the new operation header with its declared
capabilities dependency. Graph source/header/include/link inventories retain
current main's compiler, asset, shader, and integer work.

Excluded historical changes include implicit double-to-float narrowing, integer
conversion and clamp syntax changes, integer-report deletions, native memory
accounting removal, compiler-host packaging reversions, bootstrap artifacts,
runtime pins, and unrelated compiler/SDK/build changes. The current runtime pin
remains `b1fcb5e006d8e2e16b4a39dddff6aafbd92075d1`. Public Delta stays removed;
private `BF_DELTA` is unchanged. No project-version bump is needed.

Already identical to main and not recopied: project migration, function builder,
prepared record implementation, project compilation, record-prepared runtime
tests, navigation tests, and SDK build tests.

## Independent review and checks

Two parallel Mac reviews inspected operation/compatibility and consumer/build
boundaries. A second review of the reconciled implementation found no actionable
correctness or ownership defect. The runtime-only consumer build exposed a
missing `types/text.h` include in the recovered test; it was fixed before passing.

AppleClang 21, macOS arm64, Release, sanitizers off. Native language and separate
runtime-only tooling builds used the repository-pinned cached third-party archive.
The runtime tooling configuration has compiler, native components, and desktop
packages all OFF; its prepared consumer object links only `Chibi::behavior_runtime`.
Emscripten 6.0.9 and Node executed the actual Wasm test binary. The pinned web
archive was downloaded into a task-local cache; installed Emscripten was reused.

At checkpoint `422c6569a96a790c7d08d08cdea99028723ec583`, all ten selectors
below passed after formatting and restoring mutation controls.
Each ran through the since-deleted `tools/test_policy.py --timeout 60 tool <selector>` with the
language, tooling, or Wasm build directory selected by its documented environment
variable.

| Check | Cases / assertions |
| --- | ---: |
| language-operations | 11 / 196 |
| language-format | 61 / 5327 |
| language-query-program | 65 / 11607 |
| language-incremental | 13 / 235 |
| language-numeric-exact-core | 22 / 567 |
| language-integer-semantics | 1 / 325 |
| language-record-prepared | 11 / 1658 |
| language-prepared producer | 9 / 238 |
| language-prepared runtime-only consumer | 14 / 725 |
| language-prepared process harness | 1 / 5 |
| wasm-operations | 11 / 196 |
| wasm-record-prepared | 11 / 1658 |

An independently configured installed-header object consumer included
`execution/operation.h` and linked only `Chibi::execution_contract`. It compiled
with `-Wall -Wextra -Werror`; its include roots were exclusively installed
execution_contract, types, support, and capabilities headers. No source-tree
include root was supplied.

The Graph review checked all 141 include arrays preserve every non-capabilities
entry and its order. The reduced native CMake build matched public-include order
for 67 targets; four additional differences were existing `public/math` omissions.
This is not full desktop command/link parity evidence.

## Fixed oracles and controls

- UNKNOWN ABI tests use three literal ABI-v1 hashes for explicit raw function,
  scalar-result, and action declarations, independent of generated operation
  definitions. Rebuilding with main's original `catalog.c` passed those vectors;
  the known-versus-opaque distinction test failed as expected.
- Deliberately hashing UNKNOWN as a known operation failed the fixed ABI case.
- Deliberately omitting dormant `component_field` validation failed the raw
  metadata rejection case. The matrix covers both Owner and Delay.
- Hardcoded `BF_OWNER`/`BF_DELAY` expectations check generated mappings. Execution
  checks owner `7`, immediate zero/negative delays, positive suspension, NaN
  pending state, and retained strings against literal expected results.
- The prepared producer retires its source/catalog before writing. Its separate
  consumer has no native callbacks/imports and checks owner, strings, completion
  count, and synthetic times 0.25/0.5. It overwrites the original input string
  buffer before dispatch to test ownership.

All mutation source changes were restored. The two defect controls built only the
capabilities library and relinked the existing dispatcher after an initial full
target attempt exposed the intentionally broken opaque ABI in the packaged-asset
build. No changed runtime pin or seed was used to make that control pass.

## Successor: actual backend entries and bounded consumer checks

Independent review found that selecting a compiled backend did not prove entry
into generated code. The successor wraps actual dispatch and each resume window
in the existing `behavior_run_scope`. It requires positive intended-backend
entries, zero entries into either other backend, zero inspection/size fallbacks,
zero inspected entries, and no counter overflow. Scopes end before assertions,
and each resume resets the counters. Existing semantic values and step budgets
are unchanged. An independent review checked these scope lifetimes and assertions.

| Focused successor check | Cases / assertions | Observed entries per backend |
| --- | ---: | --- |
| language-operations | 11 / 266 | Bytecode and SLJIT: 1 per dispatch, 1 at positive-delay resume |
| wasm-operations | 11 / 266 | Bytecode and Wasm: 1 per dispatch, 1 at positive-delay resume |
| language-prepared producer | 9 / 238 | Producer semantics unchanged |
| language-prepared runtime-only consumer | 14 / 767 | Bytecode and SLJIT: dispatch 1, resume 1, stored completion 2 |
| language-prepared process harness | 1 / 5 | Separate producer/consumer processes |
| c-export-alias | 2 / 8035 | Compiler-enabled tooling; fixed existing C-export assertions |
| record-c-export | 1 / 1428 | Compiler-enabled tooling; fixed existing record assertions |

All listed successor checks passed. Every observed wrong-backend, inspection,
fallback, and overflow count was zero. The counters increment at actual
interpreter/SLJIT/Wasm entry sites; selecting an artifact is insufficient to pass.
These synchronous cases do not establish direct generated-C, batch/system,
worker-thread, or browser-embedding coverage.

Compiler-enabled tooling reused the native SDK object cache through a task-local
CMake inclusion hook; repository CMake configuration was not changed for that
reuse. Its dry run estimated 65 compilations and five links, with no generation.
The two-job build completed in 8.76 seconds with maximum child RSS 145,408,000
bytes. C-export selectors took 2.127 and 1.360 seconds, with peak child RSS
61,390,848 and 77,447,168 bytes. Backend selectors took 0.967, 0.437, and 0.257
seconds; their maximum observed child RSS was 144,097,280 bytes.

The full desktop CMake reference was configured from cached dependencies without
building it. Graph command/link/header comparisons exposed and corrected two
recovery omissions: SDK-contracts include ordering for seven source files, and
the repeated capabilities archive required by reflect's direct and transitive
CMake links. Independent review confirmed both corrections. The reference's
architecture and SDK were explicitly aligned with Graph's discovered arm64/Mac
SDK profile. The bounded AppleClang Graph run passed all 13 executable
link-command comparisons and executable header staging. AppleClang
compile-command parity fails:
4 of 982 commands match; each of the other 978 reports Graph `-std=gnu23` versus
AppleClang/CMake `-std=gnu2x` as its first difference. The flag-selection code in
`tools/graph/engine/Engine.chibi` is unchanged from main. This remains a failed
AppleClang parity check; flags and comparator assertions were not weakened. The final comparison took 8.03 seconds with maximum child RSS
851,853,312 bytes. Graph used a task-local wrapper to cap jobs at two and retained
its 50-second internal deadline and the test-policy cleanup deadline.

A configure-only check of isolated main
`d1154cdb8677cbfe479d4be40db7ca480d10f103` reproduced the same flag mismatch
using `/usr/bin/cc` (AppleClang 21.0.0.21000334), CMake 4.4.3, Release, arm64,
and the same SDK. CMake's installed `Compiler/AppleClang-C.cmake` maps C23 GNU
extensions to `gnu2x`; Graph's production `Engine.chibi::Flags` hardcodes
`gnu23` for C. Its existing `Profile.chibi::CompilerId` distinguishes AppleClang.
A proposed-only fix is to extend the existing `gnu2x` condition in `Flags` to
AppleClang while preserving C++ handling. No production flag change was made
for this recovery.

The supported Homebrew/Mac-CI configuration passed the unchanged comparator on
`b99c21baa566641635729b8c1939acb8d22aec72`: **982/982 compile commands across
142 libraries, 13/13 executable links, and executable header staging** (three
cases, 1015 assertions). Exact local configuration:

- CMake 4.4.3 and Ninja; `CMAKE_BUILD_TYPE=Release`.
- `CMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang` and
  `CMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++`, Homebrew LLVM 23.1.1.
- `CMAKE_OSX_ARCHITECTURES=arm64`.
- Both `CMAKE_OSX_SYSROOT` and the Graph process's `SDKROOT` set to
  `/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk`.
- `CHIBI_ENABLE_SANITIZERS=OFF`, `CHIBI_ENABLE_CCACHE=OFF`, the same pinned
  task-local third-party archive, and `CMAKE_EXPORT_COMPILE_COMMANDS=ON`.
- `test_policy.py --timeout 120 tool build-graph`, using the cached tooling
  driver, the configure-only LLVM reference, and the two-job Graph host wrapper.

This toolchain was then selected by the retired Makefile's native default and
the Mac CI workflow. Explicit SDK agreement matters: without shared `SDKROOT`, this
installed LLVM selected the Command Line Tools SDK while the reference selected
Xcode's SDK, correctly failing the comparator. No arguments were normalized or
ignored. The successful check took 6.42 seconds with maximum child RSS
965,165,056 bytes. The header-staging test deliberately uses a failing compiler;
its expected failure output is not a failed test. No target build was performed
for either configure-only reference.

Native-catalog's root `test_tools` dependency closure was estimated at 856
compilations, 112 links, and 18 generation steps (986 total). This substantial
build was reported and left unstarted. A repeated `ninja -n test_tools` and
`ninja -t query test_tools` confirmed this is the closure of the specific
`test/tools/test_tools` executable, not an all-target build. No claim is made
for local native-catalog fixture execution.

Hosted coverage was then defined in `.github/workflows/ci.yml`: both `native`
(ubuntu-24.04) and `native-macos` (macos-15) ran the retired `make test-native`,
which built the full native drivers and dispatched `tools/test_policy.py group
native` (today `chibi build test native`; `native-catalog` was archived with
the game runtime). That group included `native-catalog`, mapped to
`build/test/tools/test_tools --native-catalog`; the full native build enabled
`CHIBI_TEST_NATIVE_RUNTIME`, and the dispatcher called
`test_tool_native_catalog()`. Workflow dispatch can validate the recovery branch;
its actual run head and results must be checked separately. Windows engine and
Windows Studio jobs are disabled in the existing CI definition; paired Studio
requires its optional `studio_sha` input. The separate compiler self-build
workflow covers macOS, Linux, and Windows plus cross-platform comparison. Task-local caches, builds, diagnostic commands, and logs remain
outside the published source tree for independent review.

## Restored task-write diagnostic protection

The history-handoff audit found ten assertions from the original PR that the
first selective recovery had omitted. They are restored verbatim in
`program_task_state_awaits_its_function_results`: successful native catalog and
parsing, semantic rejection, and the specific expected-double diagnostic are
required in addition to generic compile failure. No diagnostic spelling,
fixture value, production behavior, or numeric rule needed adaptation.

The case passed through its existing `language-program` selector (165 cases /
12,391 assertions, 0.78 seconds). The small `language-operations` regression
also passed (11 / 266, 0.16 seconds). Both retained 60-second cleanup deadlines.
The two-job cached build compiled one object and relinked the dispatcher in
2.58 seconds under a 90-second deadline, with maximum child RSS 135,266,304
bytes. Test peak RSS was 23,904,256 bytes. The complete original-scope and
per-path merge disposition is recorded in [handoff](engine-operation-handoff.md).

## Resources and remaining coverage

Builds used two jobs each (at most two independent builds overlapped). Initial
native target build took 62.29 seconds; final native rebuild took 15.76 seconds
under a 90-second cleanup deadline, including its existing packaged-asset fixture.
That final build's measured maximum child RSS was 1,847,525,376 bytes. Final ten
selector runs totaled about 3.4 seconds; maximum observed child RSS in that batch
was 218,824,704 bytes. No compiler full self-compile, seed regeneration, runtime
pin change, render, Studio session, release, or deployment was performed.

Clang-format 22.1.8 and `git diff --check` passed for changed sources. Full required
native/materials/projects/language-all/tools groups, native-catalog fixture
execution, sanitizers, Linux/Windows, and browser embedding were not run.
Full desktop Graph parity passed for the supported Homebrew configuration;
the separate inherited AppleClang compile-command mismatch remains as detailed above.
Wasm operation and record-prepared execution
does not establish a separate Wasm runtime-only operation consumer; the separate
runtime-only proof here is macOS bytecode/SLJIT. These remain explicit coverage
limits for the integration owner.

The denied action resolving review thread `PRRT_kwDOT_IpgM6s41Mg` /
`discussion_r4286144466` remains prohibited and unattempted. No old PR branch,
review state, old owner's worktree, or recovery directory was modified. The
integration owner must handle PR state and any subsequent merge authorization.
