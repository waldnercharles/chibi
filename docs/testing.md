# Test iteration

Use `make agent-list` and the capability-aware commands in [agent execution](agent-execution.md)
while iterating. Full/mixed GPU suites now require explicit hardware opt-in;
CI sets it explicitly and retains complete coverage. A focused selector may contain
GPU work: the old `test-fast` name alone does not establish CPU-only coverage.

## Commands and coverage

| Command | Coverage |
| --- | --- |
| `make test-fast TEST_ARGS=--graph-optimize` | Builds or reuses the native `test_suite` in `BUILD_DIR`, then runs only that selector. It does not run tooling scripts or the full suite. Coverage depends on the selector; GPU/mixed and unclassified selectors require the hardware profile. |
| `make test-list` | Builds or reuses `test_suite` and prints common focused selectors. |
| `make test-tools` | Runs the isolated Python and Node tooling tests, with `TEST_JOBS` workers (default 4). |
| `make test-slow` | Runs the full native `all_checks` suite and the separate whole-function audit. |
| `make test-compiler-parity` | Builds the behavior-backend check and runs the focused native behavior parity selector. |
| `make test-render` | Runs native GPU previews and hidden-window checks. It remains separate from `make test`. |
| `make test` or `make test-full` | Runs the complete tooling and native regression gate, including generated/build checks, `all_checks`, and the whole-function audit. |

`TEST_ARGS` is only accepted by `make test-fast` and is passed through to
`test_suite`. An empty selector is an error. Unknown selectors exit with status
2 instead of falling through to the complete suite. A second `--...` selector
or option is rejected with status 2 rather than silently ignored. With no
selector, `test_suite` runs the full `all_checks` suite. When a selector accepts
positional arguments, pass them after the selector as ordinary Make arguments.

The common focused selector list includes asset/support checks such as
`--asset-documents`, `--rcss`, and `--tree-watch`; authoring and graph checks such as
`--automation-authoring`, `--graph-editing`, and `--graph-optimize`;
runtime/compiler checks such as `--runtime-execution`, `--behavior-parity`,
and `--simulation`; Studio checks such as `--editor-graph`,
`--studio-documents`, and `--rmlui-kit`; modeling/material checks such as
`--modeling` and `--materials`; and game checks such as `--arena-combat`
and `--arena-encounter`. Run `make test-list` for the current common list.

The timing sweep found several costly calls inside the native full suite.
Final per-call readings were: `test_compiled_behaviors` 49.0s,
`test_automation_profile_scenario_inputs` 18.0s, `test_automation_search`
17.2s, `test_refactor` 16.8s, `test_rmlui_kit` 13.7s,
`run_editor_command_checks` 12.3s, `test_encounter` 11.9s, and
`test_materials` 10.7s. Their coverage remains in `make test`; focused
selectors expose these checks individually (`--compiled-behaviors`,
`--automation-profile-scenario-inputs`, `--automation-search`, `--refactor`,
`--rmlui-kit`, `--editor-commands`, `--arena-encounter`, `--material-thermal`,
and `--material-mpm`). The thermal and MPM selectors avoid running the larger
materials group when one is the relevant change. These timings describe one
debug/sanitizer build on one Mac and are guides, not performance guarantees.

The costly cases exercise distinct behavior, so I kept them in full coverage.
`test_compiled_behaviors` compares compiled behavior execution across the
bytecode, oracle, and SLJIT backends. Automation search checks thirteen search
contracts, including 38 user-intent queries that must rank their intended node
in the first three results. Refactor coverage uses separate Arena, general
content, and Studio projects to check event, asset, value, function, member,
refusal, and delete cases; its repeated project loads keep mutations isolated
and references realistic. Profile-scenario coverage includes HRC fluid work,
sparse input playback through warmup and measured frames, and named scenarios.
None of those repetitions was a proven duplicate check. The time savings come
from selecting only the closest check during iteration and parallelizing
isolated tool-test processes rather than deleting assertions.

The full gate is unchanged for CI: CI can continue to call `make test`.
Focused results are deliberately narrower coverage; they are useful feedback
for the changed code path and do not replace the full gate.

## Test timings

Set `TEST_TIMINGS=1` on `make test`, `make test-fast`, or
`make test-slow` to add per-call elapsed timings to the native suite log.
`tools/quiet.py` prints the log path for each stage. Timing is opt-in so normal
test output stays compact.

## Baseline

On 2026-10-04, a clean isolated worktree at `96322c7191565c543e8375cde20e07d4fd2ab45a`
ran the original serial `make test JOBS=8` in 340.9 seconds. The full native
`all_checks` stage took 221.4 seconds, the tooling scripts took about 76
seconds, configure plus build checks took 37.8 seconds, and the separate
whole-function audit took 1.3 seconds. A focused
`build/test_suite --graph-optimize` invocation took 0.47 seconds on that
already-built sanitized binary. The focused invocation runs much less coverage;
it is not an equivalent-speed comparison with the full suite.

The first full gate after parallelizing tools completed in 277.8 seconds
(4m38s), with a 32.8-second build and 218.8-second native suite. A separate
`make test-tools TEST_JOBS=4` run took 24.4 seconds wall time. Against the
serial baseline, the complete gate was 63.1 seconds (18.5%) faster, and the
tooling stage dropped from about 76 seconds to 24.4 seconds. After the final
selector and argument-validation edits, the full gate passed again in 267.2
seconds (4m27s) with an incremental 26.0-second build, a 215.2-second native
suite, and a 1.3-second audit. These full runs still include all 21 tooling
tests and native checks; the 267.2-second rerun benefited from the current
build and should not be compared to the clean baseline as a cold-build result.

A separate warm
`make test-fast TEST_ARGS=--graph-optimize` completed in 2.83 seconds total;
the selector itself took 0.5 seconds, with configure at 1.0 second and an
incremental build at 0.1 second.

The newly split selectors were all run individually after the final argument
validation change: compiled behaviors 47.2s, automation search 17.2s,
automation refactor 5.0s, profile-scenario inputs 15.8s, editor commands
12.5s, material thermal 4.8s, and material MPM 3.6s. These are selector-stage
times from the quiet runner and exclude each wrapper's configure/build steps.
An unknown selector and a combined `--graph-optimize --materials` invocation
both exited 2 quickly; neither ran the full suite or silently ignored the
second flag.

The largest routine cost is running every native test in `all_checks` for
each small code change. Prefer the closest focused selector during iteration
and retain `make test` as the complete gate. Tool tests now run concurrently
in isolated processes with a four-worker default; raising `TEST_JOBS`
increases CPU and memory pressure.

## Studio documents and shell iteration (CUT-50)

List subgroup names without opening a window:

```sh
build/test_suite --studio-documents list
build/test_suite --studio-shell list
```

Select one complete group or shell case. Quote Make's `TEST_ARGS` when passing a
positional name:

```sh
CHIBI_EXECUTION_PROFILE=hardware CHIBI_ALLOW_FULL_GPU=1 \
  make test-fast TEST_ARGS='--studio-documents tabs' TEST_TIMINGS=1
CHIBI_EXECUTION_PROFILE=hardware CHIBI_ALLOW_FULL_GPU=1 \
  make test-fast TEST_ARGS='--studio-shell readout_follows_camera' TEST_TIMINGS=1
```

Listing and invalid arguments are handled on CPU before execution-policy checks
or graphics initialization. Running a valid Studio group/case requires the explicit
full-execution opt-in above; these mixed selectors are not added to the bounded
CPU/GPU allowlists. `make test` and `make test-slow` also run the CPU-only
`test/studio_selectors_test.py` subprocess regression checks against their built
binary, covering discovery, strict errors and profile enforcement.

These selectors keep each case's complete fixture, input sequence, settling frames,
and assertions. An unknown group/case or extra positional argument exits 2 before
creating the graphics app. The `list` output is the authoritative set of names.
Existing surface selectors remain supported. `--studio-documents properties`
runs the complete Properties group; the older `--studio-properties` runs only its
field checks.

With no positional argument, `--studio-documents` still runs all 19 documents
groups in their original order, then graph-inputs and thumbnails. Those last two
have their existing `--studio-graph-inputs` and `--studio-thumbnails` selectors.
`--studio-shell` still runs all 19 shell cases in their original order. Full gates
and capture mode are unchanged; an explicit shell case always runs assertions,
even if `CHIBI_SHELL_CAPTURE_DIR` is set. A focused pass does not certify the
aggregate or replace `make test-render`.

`CHIBI_TEST_TIMINGS=1` (or `TEST_TIMINGS=1` through Make) now prints flushed
`STUDIO_BEGIN` markers and `STUDIO_TIMING` records for documents groups, individual
shell cases, and app setup/teardown. Wall times use SDL's monotonic performance
counter. Shared-fixture records include fixture count, setup and teardown time,
frame count, and frame time split into event pumping, scene/graph tick, Studio,
and presentation. `batch_frames`/`batches` measure `frames()` calls, including
settling and intentional multi-frame measurements; these overlap the frame stage
totals and must not be added to them. Nested shell records likewise overlap the
parent shell record. Groups with their own fixture/frame helpers (for example
models and code intelligence) have complete wall time but incomplete shared-helper
counters. Timings are diagnostic measurements, not performance assertions.

For bounded cloud iteration, build first, then run one selector under an explicit
bound (software Vulkan requires a display such as Xvfb):

```sh
timeout --kill-after=10s 90s env CHIBI_TEST_TIMINGS=1 \
  CHIBI_EXECUTION_PROFILE=hardware CHIBI_ALLOW_FULL_GPU=1 \
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  xvfb-run -a build/test_suite --studio-shell readout_follows_camera
```

Use the repository's render leak policy and strict UBSan; do not interpret a
bound hit as a pass. Do not repeatedly run the full documents aggregate on
software Vulkan or launch concurrent heavy tests on the hardware machine.
The 60-frame cadence/recording loops and long motion/iframe settles are retained:
static inspection does not establish that those frames are redundant.

### CUT-50 measurement checkpoint (2026-10-05)

Implementation source: `196f09c74a2d68e6e3338b6a33cd56c8237aaeed`, rebased onto
merged main `de73cb85652f43694957377dfc3607d2e15da386`. The rebase changed no
files relative to the measured pre-rebase commit `decc5fbc`; main's tree also
matches the original PR151 baseline `be4d5c0d6d6a38f0747ec5d1c7f66b618c0c9b8a`.

Machine: Linux 6.18.44 x86_64, Intel Xeon Platinum 8573C, five exposed CPUs with
a four-CPU quota and 16 GiB memory limit. Build: Clang 19, Debug,
`CHIBI_ENABLE_SANITIZERS=ON`, `CHIBI_SANITIZERS=address,undefined`;
`make build BUILD_TARGET=test_suite JOBS=4`. Graphics: Xvfb and software Vulkan,
Mesa lavapipe package `25.0.7-2+deb13u1`, explicitly selected using
`VK_DRIVER_FILES=/workspace/.cloud-tools/root/usr/share/vulkan/icd.d/lvp_icd.json`.
These are not hardware-GPU timings.

All measurements ran sequentially after compilation, from the repository root,
with `ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1`, and a 90-second
bound plus 10-second termination grace. The shell command shape was the bounded
example above; elapsed times include `timeout`/Xvfb/process startup and shutdown,
measured with Python's monotonic clock, but exclude configure/build. Candidate
runs:

| Selector | Wall seconds | Result |
| --- | --- | --- |
| `--studio-shell readout_follows_camera`, timing on, first run | 3.883 | Exit 0; camera/readout assertion passed |
| Same, second run | 2.534 | Exit 0; same assertion passed |
| Same, timing off, capture environment set | 2.544 | Exit 0; assertion still ran, no timing output or capture |
| `--studio-documents tabs`, timing on | 14.908 | Exit 0; 35 assertions passed |
| `--studio-shell transport_menus`, timing on | 12.864 | Exit 0; transport assertion passed |

Readout's repeat range was 2.534–3.883 seconds; do not treat the first-run versus
repeat difference as an optimization. Its timed case body took 2.960 then 2.141
seconds: setup 0.280/0.229, teardown 0.070/0.070, six frames 2.605/1.838, with
Studio work 2.499/1.752 and presentation 0.103/0.084 seconds. Transport's 24 frames
took 11.449 seconds, including 7.689 in Studio and 3.721 in presentation; setup
was 0.224 and teardown 0.569 seconds. This localizes most readout cost to Studio
frame work, rather than fixture loading or presentation. It does not separately
profile RmlUi layout versus other Studio work. The issue's earlier hardware CPU
stack samples identify style/layout and iframe settling; this change makes those
cases independently repeatable without removing their settling coverage.

Additional checks passed: clang-format 22.1.0 on changed C/headers, `git diff
--check`, both 19-entry subgroup lists without a display, and unknown/empty names,
extra positional arguments and extra flags returning 2 without app creation.
A static comparison against `be4d5c0d` verified all 19 documents and 19 shell calls
in their original order and every shell case body byte-for-byte unchanged.
An independent read-only review found no missing coverage or selector dispatch
problem.

For the before comparison, the original four modified test sources were restored
from `be4d5c0d`, rebuilt with the same configuration, and the baseline binary was
saved separately; candidate sources/binary were then restored. The old smallest
selector containing camera readout, `--studio-shell`, hit the unchanged 90-second
experimental bound at **90.004 seconds, exit 124**. It logged 29 passing assertions,
through camera readout, but did not complete the shell suite. Its terminal XIO
message followed timeout termination. This is an incomplete baseline, not a pass
or an observed full-suite duration. Against that lower bound, a complete focused
readout run now takes 2.534–3.883 seconds. The improvement is less unrelated work
per iteration, not faster execution of equivalent full coverage. No assertion,
settle, hard case, or gate was removed to obtain it.

Remaining validation: no complete candidate `--studio-shell` or
`--studio-documents` aggregate was run in cloud, and no full `make test`,
`make test-render`, whole-function audit, web build, or hardware GPU validation was
repeated for this test-only change. Other new subgroups were listed and reviewed,
not individually executed. The existing hardware pass at `262d96db` (16m47.33,
RTX 4080 SUPER/Mesa NVK, Debug ASan+UBSan) is historical evidence, not a candidate
pass. CUT-50 must remain open until a complete relevant aggregate passes on the
final candidate and comparable full-suite timing is recorded. The parent can
arrange that hardware run; do not run concurrent heavy jobs on that machine.
