# CUT61 iframe optimization and validation

Current regression coverage is recorded in [targeted tests](targeted/README.md):
three deterministic C cases plus the existing short checks. The duplicate long
capture entrypoint and measurement-only blank-document loop have been retired;
the measurements below retain their historical source identities.

Empty Studio iframe documents now use `Context::CreateDocument` directly. This
avoids parsing an empty RML body and evaluating unrelated dirty models during
document creation. In the matched 1000-frame fixture, creation-time model reads
fall from **1000 to zero**, while ordinary Context work remains one update per
frame. Fitted content also grows and shrinks within the same frame, including
when a later document dirties an earlier holder.

The implementation is complete for this scope. Final runtime acceptance was not
completed, and the PR remained draft at this checkpoint.
No overall Studio speedup or full verification is claimed.

## Historical frozen source and configuration

- Code and final focused evidence: `641424f550c8f78327e3e8ef232b904364688514`.
- Tree: `9bf51cc95d42a0ff13d462f088ce1f77f26b779b`.
- Integrated main: `0f629f8bcaefe9689ed0060e533835082897baa4` (PR186 policy).
- Original PR head: `0538d05c67f74fb3b177a534c5a0ed5d3f3b0c0d`.
- Native: macOS arm64, Clang 23.1.1, SDK 26.5, Debug, assertions,
  ASan+UBSan, four build jobs. RmlUi is pinned to
  `ba95ffe8bfb6370efb2cdcca927eaad4710c5413`; patch hashes and binary SHA-256
  values are in `environment.json`, `supervised/results.json`, and
  `micro/identity.json`. Immutable native binaries remain local rather than
  being committed.
- Final focused fixtures run CPU layout, callbacks and hit testing with no
  window or GPU render work. Historical lifecycle logs identify the actual
  Metal backend and Apple M4 Pro; capability discovery alone was not used as
  proof of hardware execution.
- Web: Emscripten 6.0.9, normal serial and four-worker Release builds,
  profiling configuration off. `web/provenance-final-641424f5.json` pins
  settings and all four JS/WASM hashes.

The following measurements describe this frozen historical source. The later
targeted-test migration changes test code and retires duplicate profiling paths;
its evidence is recorded separately. Neither record is current-head full-gate
evidence.

## Runtime change

`kit.c` creates blank layer/node documents directly, then applies the same
stylesheets, body classes, listeners, ID and visibility. Authored RML still uses
the existing loader. The document source URL is assigned before attachment and
plugin notification, so relative resources retain their document base.

RmlUi `LoadDocument` updates all data models with `Update(false)`. That boolean
preserves dirty bits; a dirty binding can therefore be evaluated again in the
ordinary `Context::Update`. Direct empty-document creation eliminates that
earlier evaluation and leaves the dirty binding for its normal update.

A stable fitted iframe refreshes styles and geometry before measuring content
height. This refresh skips `OnUpdate` only; animation, style, scroll, layout and
other existing callbacks still run. Existing full updates on changed boxes and
heights, the pre/Context/post sequence and all four settling passes remain.
Stable attachment IDs and observer resolution protect the changed settling path
when callbacks close or detach/reattach documents. This does not claim arbitrary
callback safety for every pre-existing holder traversal.

The new C fixtures check unrelated-model isolation, the subsequent normal
binding update, copied styles/classes, relative image resolution, same-frame
growth and repeated shrink, geometry/hit testing and callback-driven lifetime
changes. The graphics-free lifecycle selector checks used the existing C
pico_unit driver at the recorded revision; the duplicate selector is now retired. Python test launchers were removed. The analysis script and the original
profiling runner (`../run.py.txt`) were removed on 2026-10-20; Git keeps them.

## Historical focused validation

All test executions used the production `tools/test_policy.py` supervisor,
including cleanup. Configuration/build time is reported
separately. The only new native routes are the independently reviewed exact
`--iframe-updates` and `--kit-documents` tuples.

| Check at the frozen source | Result | Complete command time |
| --- | --- | --- |
| `--iframe-updates`, profile enabled | 12 checks; all 1000 profile frames | 3.586 s |
| Same fixture, explicit profile off | 12 checks; no profile output | 0.580 s |
| Same fixture, environment unset | 12 checks; no profile output | 0.555 s |
| Original `--kit-documents`, six alternating enabled/off processes plus default-off process | 10 checks and 49 frames in each; enabled breakdown reconciles | 1.029–1.567 s each |
| C tooling suite via production supervisor | 17 tests, 189 assertions | 21.406 s |
| C Studio/lifecycle selector driver | 4 tests, 84 assertions; no graphics | 5.070 s |
| `make check-format` | Passed | 5.1 s |
| `make web JOBS=4` at frozen source | Serial, threaded and paired package passed | Build only; no backend or browser execution |

`supervised/` retains complete compressed logs and command/source/binary
metadata. Profiling-on kit runs retain 49 Context updates, zero render captures,
zero render passes and zero geometry compiles. Probe counts are unchanged except
that two layouts move from the Context phase into pre-settle: total probe layouts
remain 38, and no extra `OnUpdate` is added by the stable fitted refresh. The
inner Context sum/residual retains the original **5 ns** rounding tolerance.
`analysis.json` records successful offline reconciliation.

Historical forms at the frozen source, after a separate build (the optional
blank-document profiling loop has since been retired):

```sh
make build JOBS=4 BUILD_TARGET=test_suite
CHIBI_IFRAME_PROFILE=1 make agent-test JOBS=4 AGENT_SELECTOR=--iframe-updates
CHIBI_IFRAME_PROFILE=1 make agent-test JOBS=4 AGENT_SELECTOR=--kit-documents
make test JOBS=4
make test-studio-selectors JOBS=4
make web JOBS=4
```

The analysis script that summarized these captures was removed on 2026-10-20;
Git history keeps it.

The captures invoke `python3 tools/test_policy.py native <frozen-binary>
<exact-selector>` directly, with `CHIBI_EXECUTION_PROFILE=iteration`,
`CHIBI_ALLOW_FULL_GPU=0`, and `MallocNanoZone=0`; the benchmark/profile workload
itself is C. Explicit-off and unset modes are recorded separately. `make test`
also passed the same 17-test tooling group before the final selector setup fix;
the later direct supervised run above uses the final C driver.

A first C selector migration run failed its unknown-selector expectation because
the preceding rejection check left GPU opt-in disabled. Restoring the original
validation-only setup fixed it; `selectors-integrated.log` preserves the failed
run and `selectors-final.log` the successful rerun. This was test setup, not a
production iframe defect. Separate build-retirement metadata emitted Ninja
generator warnings for Makefile web directories; compilation/package exit codes
were zero.

## Matched document-creation comparison

The before binary uses only `src/editor/rmlui/kit.c` from original PR head
`0538d05c`; the after binary uses the frozen implementation. All other source,
patched dependencies, flags, assertions and fixture frames are identical. The
reverse source delta and both binary hashes are retained in `micro/`. Source was
restored and rebuilt before the comparison.

Three before/after pairs ran serially in alternating order through the production
supervisor. All six complete processes consumed 10.936 seconds. Every sample includes all 1000 replacements
and Context updates; no warmup frame or outlier was removed. Before completes
10 passing checks and two expected semantic failures for the now-fixed
unrelated-model behavior; after passes all 12. The before diagnostic variant is
not a successful test result.

| Counter across 1000 frames | Before | After |
| --- | ---: | ---: |
| Documents opened | 1000 | 1000 |
| Creation-time unrelated model reads | 1000 | 0 |
| Total frame unrelated model reads | 2000 | 1000 |
| Context updates | 1000 | 1000 |
| Render captures / passes / geometry compiles | 0 / 0 / 0 | 0 / 0 / 0 |

Creation times across the three full samples were 744.040, 176.883 and 390.698 ms
before, and 260.697, 109.348 and 97.007 ms after. External native/dependency/release
builds were active; the short CPU selector validation also overlapped early
trials. These timings are **load-confounded**, and do not establish a latency
speedup. The deterministic operation counts establish elimination of the extra
model evaluations. Parsing was removed too; timing cannot assign cost separately
to parsing and binding evaluation. This Debug/sanitizer fixture is not a release
benchmark or a whole-Studio frame-time result.

## Earlier acceptance and rejected diagnostics

`historical/` preserves every included capture with its original identity.
Browser acceptance at `cac4acb9` completed in both normal web modes in headless
Chrome 154: real canvas typing/filtering, divider resize, nested menu focus and
dismissal, and Outliner→Assets→Outliner replacement. Serial used actual
`ArrayBuffer`; threaded used `SharedArrayBuffer` on the worker script path with
no fallback. Screenshots and console/error logs remain in
`historical/browser-current/`. Page-error logs were empty; an informational
oversized menu-shadow texture message remains in the console record. This is
historical acceptance, not acceptance of the current integrated clock/policy tree.

The original 2016-frame lifecycle case and its assertion remained intact:

| Historical revision / capture | Full frames / original assertion | Wall time | Diagnostic result |
| --- | --- | ---: | --- |
| Original `0538d05c`, baseline 1 / 2 / 3 | 2016 / pass in each | 130.827 / 138.910 / 136.189 s | Offline validator passed |
| Intermediate `47bdcf9f`, candidate 1 | 2016 / pass | 141.958 s | Offline validator passed |
| `cac4acb9`, current 1 | 2016 / pass | 137.740 s | Offline validator passed |
| `cac4acb9`, current 2 | 2016 / pass | 150.149 s | **Rejected:** 42 Context rows exceeded their enclosing span |

The last rejected capture’s maximum excess was 88.708 microseconds. The RmlUi
inner timer used `std::chrono::steady_clock`, which Apple libc++ implements with
`CLOCK_MONOTONIC_RAW`; outer C spans used `CLOCK_MONOTONIC`. See the
[libc++ implementation](https://raw.githubusercontent.com/llvm/llvm-project/main/libcxx/src/chrono.cpp)
and [Apple clock implementation](https://raw.githubusercontent.com/apple-oss-distributions/libc/main/gen/clock_gettime.c).
The mixed clock domains can invalidate nesting comparisons; no specific clock
adjustment event is inferred. The patch now uses the same `CLOCK_MONOTONIC`
conversion as the enclosing timers. Fresh short captures reconcile without
loosening tolerance. The long case was not rerun after this clock fix.

Lifecycle timing does not demonstrate an overall improvement. The final
pre-clock candidate’s fitted correctness work increased measured host updates
from 1100 to 1197, all-context update calls from 2855 to 2992, and pre-phase holder
updates from 2200 to 2394. Post-phase changed boxes increased from 34 to 149.
These costs remain visible in the raw records. Some older runs overlapped other
builds; each full run still kept all frames and its original 180-second budget.
No historical result certifies the final clock fix or the current integration.

## Remaining gates and publication status

CUT61’s historical 2016-frame capture, broad native/render/Studio suites,
backend/reload/no-SLJIT/compiler-parity execution, and web backend execution
were not run for the final candidate. No fresh browser runtime was launched.
An old Make web run was interrupted before the worker backend check; `interrupted-web/` preserves its exit130 and partial logs. Normal
web builds after integration used only `chibi` and package targets.

Independent agent source review of `641424f5` against the integrated base found
no blocker. This is not a GitHub APPROVED review. Required CI, approval, CUT62 integration ownership and
any PR/ticket holds remain in force. A future combined candidate must re-pin
fresh main and source identities, and obtain unavailable required verification
before landing. Focused test evidence supplies no merge
authority. The branch is updated, retained and draft; main is not changed.
Owned build/test/browser/server processes were stopped or reaped. No authored
assets, assertion body, reference image, sanitizer setting or system permission
was changed to produce a pass.
