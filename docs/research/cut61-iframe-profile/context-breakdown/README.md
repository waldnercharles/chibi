# Context boundary diagnostic preparation

Prepared and CPU-validated at **dfacf12ea8c8e8732b5e16948f4590a8b224e530**, on top
of the existing native profiling branch. This is diagnostic preparation for
independent review, not an optimization. No GPU capture or broad gate ran, and
this addition has not been pushed or published.

## Preserved native negative result

The parent supplied the completed Fedora result for
`dba9625c7c87e6cfe9817c2d39e1ab61c8d923d3`: exit 0, the original assertion and
footer, all 2,016 frames, 102.32 seconds wall time. Pre-settle was 6.517 s (6.66%),
Context 41.924 s (42.81%), post-settle 0.266 s (0.27%), and outer-frame residual
49.221 s (50.26%). Nested holder time totaled 0.261 s; post-holder time was only
0.0136 s. Post-settle propagated 34 box changes. Frame median/p95/p99/max were
9.50/189.18/449.08/1990.53 ms. This does not justify removing post-holder work.
The earlier small headless fixture's 0.21–0.34% result is also unchanged.

The parent's offline mapping attributes 91.75% of pair-loop Context time to shows
after the first requested show; requested Outliner and Assets account for 52.71%
of repeated-pair Context. Requested labels do not establish exclusive ownership:
incoming binding creation and outgoing cleanup may both contribute. The native
raw capture is owned by the parent/Fedora verifier; it was not rerun here.

## Narrow patch and record schema

`cmake/patches/rmlui-context-update-profile.patch` applies to pinned RmlUi
`ba95ffe8bfb6370efb2cdcca927eaad4710c5413`, touching only `Context.h` and
`Context.cpp`. `Context::Update` accepts an optional pointer to four caller-owned
doubles. A null pointer is the default for every existing unprofiled call.
The patched body preserves every original operation and its order; stripping
only diagnostic lines reproduces the pinned body (verified in `source-check.json`).

The four disjoint `steady_clock` durations, each with its own start/stop, are:

| JSON field | Exact timed work |
|---|---|
| `model_update_seconds` | Entire existing `for (data_models)` / `Update(true)` loop. |
| `root_update_seconds` | The existing `root->Update(...)` call. |
| `layout_position_seconds` | Entire existing document loop calling `UpdateLayout()` and `UpdatePosition()`. |
| `release_documents_seconds` | The existing `ReleaseUnloadedDocuments()` call. |

`src/ui/rml.cpp` adds only a five-line forwarding wrapper. C code in the backend
chooses that wrapper only during an enabled Context phase. The profiler aggregates
four durations plus `context_calls` into existing samples and per-context/frame
records; no element-level hook or log is added. The original Context `seconds`
field remains the inclusive total. New details appear on Context records only.

`other_context_seconds` is labeled **other Context work**: total minus the four
subtotals, without clamping or double counting. It includes scroll/hover handling,
dirty-flag reset and any remaining Context work, plus surrounding wrapper,
aggregation and clock overhead. It is not a fifth pure RmlUi internal span.

These are execution-boundary labels, not exclusive subsystems. Model updates may
build or remove views/trees; root callbacks may trigger layout; layout callbacks
may do other work. Release timing does not include all possible cleanup (e.g.
deferred DataViews removal during model updates). Initial loading/SetInnerRML
outside measured Context is still outside this breakdown. Direct restyle Context
calls in `kit_begin` retain their existing unprofiled route and remain in the
outer-frame residual.

The original native lifecycle body, aggregate panel calls, selectors, 2,016-frame
sequence, assertions, and settling are untouched. CMake registers the dependency
patch using the existing patch mechanism; dependency-package hashing includes the
patch set. Rebuild normally: the optional-argument C++ signature changes the
compiled RmlUi symbol, so stale headers/libraries must not be mixed.

## Focused CPU evidence and reproduction

Using the existing saved environment and activation:

```sh
source /workspace/.cloud-tools/activate.sh
make build JOBS=4 BUILD_TARGET=test_suite
python3 test/context_update_profile_test.py build/test_suite /tmp/cut61-context-new-evidence
```

The output directory must not exist. The validation script runs exactly six fresh
`--kit-documents` processes, interleaving `CHIBI_IFRAME_PROFILE=1` and `=0` three
times, each under the unchanged 180-second limit and explicit selector opt-in.
The fixture is the existing graphics-free 49-frame test; it is not the native
Studio lifecycle. Build is Debug with ASan/UBSan and assertions, four jobs, using
the same activation/settings as the initial experiment.

All six runs exited 0 and passed all 10 original checks. Enabled runs each retained
49 frames and the original pre/context/post holder, callback, layout, box, height,
and settle-pass counts. Every Context record has one breakdown call, four finite
nonnegative durations, a subtotal no greater than Context time, and a reconciling
other-Context residual (5 ns tolerance for independent JSON rounding). Renderer
counters remain zero. Disabled runs emitted no profile records. An initial
validator failure caught a split-literal `root_seconds`/`root_update_seconds`
reporting mismatch; it was corrected before these final six runs. No assertion,
frame, or timeout was relaxed.

All durations below are milliseconds, summed across each 49-frame enabled run:

| Run | Context total | Models | Root update | Layout/position | Release | Other Context work |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 132.135 | 0.043 | 78.721 | 46.184 | 3.099 | 4.089 |
| 3 | 120.117 | 0.047 | 70.745 | 43.217 | 1.903 | 4.205 |
| 5 | 142.010 | 0.047 | 83.778 | 51.282 | 1.970 | 4.934 |

These values validate instrumentation on the small fixture; they do not locate
the native panel bottleneck. Enabled wall times were 0.508/0.492/0.608 s; disabled
were 0.484/0.556/0.547 s. Startup, logging, sanitizers, and run variation are
included. This overlap is not a calibrated overhead bound or a speedup result.
`results.json` records the code commit and binary SHA-256; `run-*.log.gz` retains
all rows/assertions, and `build.log` records the successful bounded builds.

## Overhead and review boundary

The added disabled path is constant work at Context boundaries: a backend phase
query/branch and eight null checks in Context, outside all loops. It performs no
new clocks, duration writes, allocation, logging, per-document, or per-element
diagnostic work. Existing disabled outer profiling guards remain. Disabled
overhead is not claimed to be literally zero or experimentally isolated.

Enabled breakdown adds eight monotonic clock reads per Context call, four duration
writes/accumulations and copies in the existing record, and additional fields in
the existing frame-level output. It adds no global callback registry or new
lifetime references. Timings include any overhead inside their boundaries; no
subtraction, warmup exclusion, or release-mode extrapolation was used.

Read-only source sanity review found no blocking issue and confirmed disjoint
spans, enum/array mapping, per-record deltas, preserved original call order, and
unchanged lifecycle coverage. Independent parent review and any later native
capture remain pending. Do not infer production ownership or a speedup from this
CPU validation, and do not remove post-holder updates.
