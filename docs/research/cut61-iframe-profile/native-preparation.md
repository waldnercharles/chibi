# Native lifecycle capture prepared (not run in cloud)

The strict selector `--studio-panel-lifecycle` calls exactly
`area_panels_take_turns(false)` through a new test-side wrapper. The original
function body and `studio_documents_panel_checks()` call list are byte-for-byte
unchanged. All three rounds, 2,016 explicit frames, assertions, ordinary rendering,
and `SETTLE_PASSES = 4` remain intact. The CPU-only result in `README.md` and its
original measured source/logs remain unchanged: 0.21–0.34% for the small kit
fixture, not a representative Studio speedup.

## Build and selector validation

Use the native verifier's existing toolchain activation, then:

```sh
make build JOBS=4 BUILD_TARGET=test_suite
build/test_suite --studio-panel-lifecycle validate
python3 test/studio_panel_lifecycle_test.py build/test_suite
```

Exact `validate` returns the selected function and expected frame count before
preference isolation, registry, app creation, or graphics initialization. Unknown
positional arguments, extra arguments, and competing flags return 2 before app
creation. Native execution still requires the existing full/hardware opt-in;
this selector was not added to the CPU allowlist. Existing aggregate selectors
and all their calls are unchanged.

Cloud verification: build passed (Debug, ASan+UBSan, four jobs); all 11 selector
validation/refusal checks passed without graphics. The original headless kit
fixture passed with profiling both enabled and disabled (10 checks each).
Enabled counts remain identical to the initial 49-frame profile; disabled mode
emitted no diagnostic rows. See `selector-validation.log`, `native-prep-build.log`,
and `native-prep-kit-profile-*.log.gz`. The native case itself remains unexecuted.

## Exact one-case measurement command

Only after the parent admits the Fedora hardware slot, from the fetched worktree:

```sh
timeout --signal=KILL 180s env CHIBI_EXECUTION_PROFILE=hardware CHIBI_ALLOW_FULL_GPU=1 CHIBI_IFRAME_PROFILE=1 SDL_LOGGING=gpu=info build/test_suite --studio-panel-lifecycle > cut61-lifecycle.log 2>&1
```

Use a fresh output file and record the shell exit status immediately. This is a
hard 180-second deadline, with no grace period or automatic retry. Timeout (GNU
`timeout` with KILL normally returns 137), any other nonzero exit, a missing
`STUDIO_CASE` footer, a frame count other than 2,016, or a failed original assertion
means incomplete/failed evidence. The footer's `app_result` is app setup/run
status, not a replacement for the final process exit status or assertions.

Record `git rev-parse HEAD`, build mode/sanitizers/compiler, actual backend/device
from startup logs, and process elapsed time with the capture. Do not select a
software fallback or change resolution/settles to get a passing capture. This
command does not run the Designer variant or remaining panel checks. No native
measurement, optimization, RmlUi internal patch, PR, or main merge is included.

## What the instrumentation reports

Profiling remains opt-in via `CHIBI_IFRAME_PROFILE=1` on this exact native selector
(or on the original headless kit fixture). Other Studio selectors do not enable
it. Each `rml_backend_update` contributes three `STUDIO_IFRAME` records containing
its unique copied `Chibi Editor N` context name, phase start timestamp, duration,
holder updates/time, changed boxes/heights, and settle-pass count. Names survive
teardown in the diagnostic records and distinguish host/graph/other contexts;
records do not retain document or context pointers.

`STUDIO_FRAME` brackets the original `frame_delta` body, including input, scene
work, Studio frame, and presentation. It includes total measured phase time and
`outside_phase_seconds`, plus host backend deltas for context updates, retained
frames, render captures, and render passes. Host stats describe only
`f->studio->backend`; phase records may include several contexts. A retained frame
can have no phase records, and must still be counted.

`STUDIO_CASE` includes app initialization and teardown, the completed application
frame count, and the sum of outer-frame times. The phase/outer-frame output is
written after the outer-frame timer stops. Therefore its I/O cost is included in
case/process time, but not in the outer-frame sums. Case time also contains setup,
teardown, inter-frame actions, and diagnostic resets. Recording is reset at each
application frame; backend updates between frames are not attributed to that
frame. They remain part of case time.

Context remains inclusive of models, hooks, style, layout, and release. No finer
RmlUi instrumentation was added. `kit_begin` direct restyle updates and the later
presence/popover/autofocus helpers are outside the three phase timers, as are
host emit/sync, rendering and presentation. Thus the residual is **unattributed
work**, not a GPU-time measurement. Holder time is already included in settle
time; do not add it to phase totals. Per-frame phase sums across contexts can be
compared with the outer-frame time to choose the next area to profile.

## Overhead expectations and limits

Disabled: no profile clocks, record allocation, context-name copies, or profile
output. The diagnostic build still has the existing six phase-helper calls and
counter branches per backend update, plus two test-side frame guards and local
stack setup. Disabled overhead is not claimed to be zero or measured away.

Enabled: two monotonic clock reads per phase, two per holder update, record/name
copies, and record-array growth as needed (capacity reused across frames), plus
the outer/case clocks and per-frame output. Record assembly is after each phase's
stop timestamp, so that bookkeeping is in outer-frame residual time. No per-element
logging or DOM probes were added to the native case. Timings include the
instrumentation that executes inside their boundaries; no overhead subtraction,
release-build extrapolation, or before/after speedup is claimed. The existing
single-threaded profiler rejects reentrant phase starts; current graph and host
updates are sequential.

A read-only source review confirmed exact dispatch, unchanged lifecycle and
aggregate code, all 2,016 frames, unchanged settling, and no double-counting of
holder subsets in frame totals. Native rendering behavior and the new native
reporting path still require the scheduled Fedora run.
