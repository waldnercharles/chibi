# Test iteration

Use a focused native selector while iterating on one feature, then run the full
gate before a merge-ready handoff.

## Commands and coverage

| Command | Coverage |
| --- | --- |
| `make test-fast TEST_ARGS=--graph-optimize` | Builds or reuses the native `test_suite` in `BUILD_DIR`, then runs only that selector. It does not run tooling scripts, the full `all_checks` suite, the whole-function audit, or GPU tests. |
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
