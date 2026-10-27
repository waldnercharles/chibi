# Opt-in Debug optimization experiment

This is an experiment, not a CI policy change. The normal sanitizer profile is
still Debug (`-g`, effectively `-O0`), ASan + UBSan, with assertions enabled.
Release is `-O3 -DNDEBUG`; comparing it directly would confound optimization,
instrumentation and assertions. Native LLVM generated-code O0/O2 tests are a
separate coverage dimension and must both remain enabled.

Clang's [ASan usage guidance](https://clang.llvm.org/docs/AddressSanitizer.html#usage)
recommends `-O1` or higher for reasonable performance. That is a reason to measure,
not evidence of a Chibi speedup or permission to remove automatic sanitizer tests.

## Matched configurations

Use the same clean candidate commit, runner, compiler/toolchain, dependency pins,
job count and environment for both arms. Do not change package installations,
third-party archives, compiler pins, timeouts or test manifests for this experiment.
Do not borrow unfinished CI/cache changes. On Linux use the existing CMake route;
on macOS use the existing Graph route with a matching CMake reference.

Use new, separate build directories. Record whether dependency archives are
already extracted, ccache availability/configuration/stats, source-cache warmth,
runner image/hardware, compiler versions, commit SHA and the full commands. A
fresh build directory is **not** a cold compiler cache. Never clear a shared cache.
Run both arms without concurrent jobs from other experiments. Report cache
conditions rather than attributing cache hits to optimization.

The following commands are opt-in; they do not run in existing workflows. Run
once for each `level` (`0`, then `1`), retaining logs outside quiet.py's rolling
log directory. Start with directories that do not already exist; do not delete a
previous experiment's evidence.

```sh
level=0 # repeat with 1 and a fresh output directory
b="build-sanitizer-o$level"
evidence="$PWD/experiment-evidence/o$level"
mkdir -p "$evidence"
# These are shell quotes within a Make variable, preserved by the build recipe.
flags="-DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=ON \
-DCHIBI_SANITIZERS=address,undefined -DCHIBI_ENABLE_WARNINGS=ON \
-DCHIBI_ENABLE_CCACHE=ON -DCMAKE_C_FLAGS_DEBUG='-g -O$level' \
-DCMAKE_CXX_FLAGS_DEBUG='-g -O$level' -DCMAKE_OBJC_FLAGS_DEBUG='-g -O$level'"
python3 tools/quiet.py --log-dir "$evidence" build -- \
  make test-build BUILD_DIR="$b" BUILD_DEBUG_OPTIMIZATION="$level" \
  SANITIZER_CMAKE_FLAGS="$flags" LOCAL_CMAKE_FLAGS="$flags" VERBOSE=1
```

Check the exit status before proceeding. `quiet.py` measures **monotonic wall
seconds**, including child processes; pico_unit's CPU time is not the comparison.
Its outer `build` duration includes setup/configuration/build; retain the inner
phase durations separately. On macOS, also prepare the parity reference using
`make graph-reference BUILD_DIR="$b" SANITIZER_CMAKE_FLAGS="$flags"` outside the
timed test phase. On Linux the CMake reference is `$b`, on macOS `$b-cmake`.
The opt-in Graph option is `--debug-optimization 0|1`, valid only for Debug;
omitting it leaves all existing flags unchanged. Graph does not consume CFLAGS,
CXXFLAGS or arbitrary CMake flags. Setting only the CMake flags is insufficient
on macOS; setting only the Graph option leaves the reference/initial host mismatched.

## Verify flags before timing tests

Retain both compile databases, CMake caches, generated `export_compiler.h`, link
commands and verbose build logs. Graph's per-object records contain the actual
compiler command; compare them with the database, not only with a printed
configuration. For CMake retain actual verbose compiler argv (`VERBOSE=1` streams
quiet.py logs; `cmake --build <dir> --verbose` exposes compiler commands when needed).
Verify representative engine, SDK, test-driver, C++ shim and (macOS) Objective-C
compilations. For each arm require effective `-O0`/`-O1`, `-g`,
`-fsanitize=address,undefined`, `-fno-omit-frame-pointer`, no effective `NDEBUG`,
and the existing test `-UNDEBUG`. Check argument order, including later overrides.
Check the exported compiler arguments used by subprocess fixtures too. Prebuilt
third-party archive flags do not change and must be reported separately.

Run `build-graph`, `build-graph-programs`, and `build-graph-generated` through
`tools/test_policy.py tool` with the selected build paths below and
`CHIBI_GRAPH_REFERENCE` set to the platform's reference. The first compares
compile/link argv with CMake; the second checks unchanged default, explicit O0,
explicit O1 and invalid/non-Debug options; generated-header parity on macOS
also checks exported compiler arguments. These are correctness gates, not part
of selector timing. Do not claim actual compilation from commands-only tests.

## Four-selector comparison

After successful builds and flag checks, set all paths explicitly so no child
silently falls back to another build:

```sh
export CHIBI_TOOLS_BUILD_DIR="$b/test/tools"
export CHIBI_LANGUAGE_BUILD_DIR="$b/test/language"
export CHIBI_BUILD_HOST="$PWD/$b/sdk/chibi-build-host"
export CHIBI_COMPILER_HOST="$PWD/$b/sdk/chibi-compiler-host"
export CHIBI_CTL="$PWD/$b/bin/chibi_ctl"
export MallocNanoZone=0
for selector in language-package-compiler-chibi-scalar \
                language-package-compiler-chibi-behaviors-rest \
                language-asset-compiler-host compiler-bootstrap; do
  python3 tools/quiet.py --log-dir "$evidence" "$selector" -- \
    python3 tools/test_policy.py tool "$selector" || exit "$?"
done
```

Keep platform availability honest: asset-host coverage is unavailable on some
configurations; an unavailable selector is not a pass. `compiler-bootstrap`
uses the Debug tools/SDK selected above, **not** the separate Release
`language-package-compiler-chibi-bootstrap` route. Keep both generated native LLVM
O0/O2 paths enabled wherever the current manifest requires them.

Record a first run separately, then three paired warm repetitions, alternating
O0/O1 order. Keep per-selector monotonic wall time, exit status, observed coverage
and logs. Report medians and ranges, cold/configuration/build time separately,
and build-plus-first-selector-run total. Do not silently discard failures,
slow repetitions or missing coverage. Optimizing test runtime at the expense of
build time may be a loss for iteration; compare both.

A later, separately approved O1 Debug arm without sanitizers can isolate
instrumentation cost while retaining assertions, but it needs matching
unsanitized dependencies and must not be described as the matched O0/O1 result.
Do not use Release/NDEBUG as that control.

## Manual hosted first pass (historical O0/O1)

The first pass below describes the original O0/O1 runner. The current manual
workflow measures [O1/O2 with sanitizers on and off](optimization-instrumentation-experiment.md);
use that protocol for new measurements. Historical results remain unchanged.

`Debug optimization experiment (manual, not full CI)` is a separate Linux-only
`workflow_dispatch` workflow. It requires an exact reviewed SHA containing the
runner and the landed Ubuntu provisioning helper; the workflow must first be
registered on the default branch. It does not change or replace normal PR checks.

The runner configures both clean arms before building, shares only pinned
prebuilt dependencies, and disables ccache in both. It verifies representative
actual compiler argv against the compile database and exported compiler flags
before timing the four selectors as pairs with alternating arm order. This
initial run is **one pair per selector**, not the warm repetitions above or a
full-suite pass. Its supervised aggregate budget is 35 minutes within a
45-minute job. Failure, timeout or cancellation preserves logs and JSONL timing
evidence; missing measurements are not a successful comparison. Configure time
(including the first dependency download), build time and each selector's wall
time are separate, with a build-plus-four-selectors total only after both arms
finish. The artifact includes toolchain/hardware/cache metadata and verified
argv, but not compiled binaries or dependency archives.

## Decision gate

Do not change defaults based on the four selectors alone. If results are
promising, the coordinator must require the unchanged full native, material,
project, language and tooling manifests on **both** platforms, independent review
of the exact candidate and applicable server checks before any policy proposal.
Record every unrun selector and environment limitation. No speedup, native build
pass, or full-suite pass is asserted by this document.

### Parallel verbose build evidence

The Linux manual runner fixes the generator to Unix Makefiles and passes
`--output-sync=target` to Make for both arms. Without recipe-level output
buffering, parallel progress output can interrupt compiler commands longer than
`PIPE_BUF`, making strict actual-argv verification fail even after a successful
build. The verifier still requires exact argv (apart from dependency-output
flags); it does not reconstruct or normalize corrupted command lines. This option
is specific to the manual Make experiment, not a change to Ninja or normal CI.

### Linux CI candidate after the bounded pair

Run [40435330707](https://github.com/waldnercharles/chibi/actions/runs/40435330707)
measured exact `15bc4b78c4121d54c5c078bb9939f7d59b4dcbec`, with compiler cache
disabled, one ordered pair and four unchanged selectors. All 35 cases per arm
passed, and both arms passed strict actual-argv verification. Seconds:

| Phase | O0 | O1 |
| --- | ---: | ---: |
| Build | 272.289 | 412.088 |
| Scalar | 179.846 | 87.273 |
| Behaviors-rest | 176.011 | 85.531 |
| Asset compiler host | 195.373 | 79.341 |
| Bootstrap | 113.875 | 37.400 |
| Build plus tests | 937.394 | 701.632 |

Configuration (6.328 / 3.028 seconds) is excluded. The O1 total was 25.15%
lower in this single pair; this is not a general speedup claim. Compilation was
slower. The result motivates a Linux CI candidate, not a developer-default or
macOS policy change and not a replacement for full CI.

Only the Linux native CI job overrides `SANITIZER_CMAKE_FLAGS`: Debug retains
`-g`, ASan/UBSan, frame pointers, assertions, warnings and compiler caching,
with `-O1` for C and C++. Initial builds and later test-build invocations inherit
the same profile. The separate Release products are unchanged. Normal developer
Make defaults and macOS remain unchanged; an explicit command-line
`SANITIZER_CMAKE_FLAGS` (including `-g -O0` for both languages) still wins.
No selectors or deadlines are removed or extended.

The existing `build-graph-debug-optimization` CI gate now compares the actual
O1-configured CMake compile commands with Graph-emitted commands, and exercises
Graph's default, explicit O0, explicit O1 and rejected profiles. It retains its
Release planning helper and Debug sanitized test driver/reference. Linux's normal
build route remains CMake; this does not switch Linux to Graph or change its
outer planning host. Exact-candidate full CI remains required before landing.

A future macOS experiment should use a separately reviewed, manual-only
matched pair on one fixed macOS runner/toolchain/SDK, identical pinned archives,
cache off and fresh build trees. Keep configuration separate, strict actual-argv
checks (including Objective-C), the same selectors and one bounded aggregate
budget. Mac policy remains unchanged until that measurement and its independent
review. No macOS dispatch or automatic repeat is authorized by this proposal.
