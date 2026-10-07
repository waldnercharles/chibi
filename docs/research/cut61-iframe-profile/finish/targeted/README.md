# Targeted iframe regression coverage

Validated source: `944994536a8686d32f698dcc729753c1656141a1`.
Tree: `4f9a4773cfab600065066d14639764d1bbff502a`.
The worktree was clean when the native and tooling binaries were frozen.
`results.json` records their SHA-256 values, exact supervised commands, timings,
configuration, and serial/threaded JS/WASM hashes. Later evidence-only commits
do not change this tested code or satisfy current-head server checks.

The primary regression command is now `make agent-test AGENT_SELECTOR=--iframe-updates`.
It retains the 12 existing checks and adds three C pico_unit cases:

| Case | Contract |
| --- | --- |
| Bound document replacement | Sixteen distinct whole-document replacements share one surviving model. Conditional and list values update, each retired observer expires, document counts return to baseline, exact label reads remain one or two, and repeated topology reads do not accumulate. Unmount and destruction leave no bound reads or live documents. |
| Layout refresh | Parent and child `OnUpdate` counts remain unchanged through style/layout refreshes. Width and height resolve, opacity progresses and completes under synthetic time, and a normal Context update invokes each callback exactly once. |
| Removal during settling | One hosted callback removes both an already-visited attachment and an unvisited later attachment. The remaining document is not skipped, callback counts are exact, survivor geometry and subsequent resize resolve, and teardown releases every document and observer. |

Assertions inspect runtime behavior and ownership. They use no wall-clock pass
thresholds, sleeps, window, browser, or GPU rendering. Setup and teardown run even
when a pico assertion fails; case failures reach the focused and aggregate exit
status. The binding fixture also requires zero new RmlUi warnings.

## Safe retirement

Removed the measurement-only 1,000-frame blank-document loop from the regression
command. Its previous measurements remain historical evidence in the parent
report. Removed the duplicate `--studio-panel-lifecycle` selector, its special
capture wrapper and per-context record storage/APIs. The selector driver now
checks that the retired option is rejected and absent from discovery without
starting graphics.

The original `area_panels_take_turns(false)` and `(true)` assertions remain in
the broad Studio panel suite. Its three source files match integrated base
`0f629f8b` after wrapper removal. Unique authored-panel, pixel, retained-render,
input and backend assertions were preserved. The 49-frame kit diagnostic keeps
its cumulative phase profiling; its final run records 49 Context updates and
zero render captures, render passes and geometry compiles.

## Verification

All runtime commands used `tools/test_policy.py` with a 60-second deadline,
including fixture setup and process cleanup. Builds ran separately.

| Check | Result | Complete runtime |
| --- | --- | --- |
| `--iframe-updates` | 3 pico cases, 385 assertions, plus 12 existing checks | 2.163 s |
| `--kit-documents`, diagnostic enabled | 10 checks, all 49 frames | 0.871 s |
| C Studio selector driver | 4 cases, 52 assertions | 1.480 s |
| `make check-format` | Passed | 5.2 s; formatting, not runtime coverage |
| `make web JOBS=4` | Serial, threaded and paired package passed | Build only |

Native configuration is macOS arm64, Clang 23.1.1, Debug, assertions and
ASan+UBSan. Web builds use Emscripten 6.0.9, Release and profiling disabled.
Build retirement snapshot warnings concern Ninja introspection of Makefile web
directories; the configure, compile and package commands all exited zero.

Three intermediate failures were fixture issues: initial show callbacks were
included in the measured count, an empty list template emitted warnings, and
inline opacity changes bypassed the intended CSS transition. The final fixture
counts after warmup, uses nonempty list markup and changes a CSS class. Synthetic
animation time advances in frame-sized steps. Compressed iteration logs preserve
these failures; they are not acceptance passes. No production behavior fix was
needed for these cases.

Independent source review found no blocker in the contracts, failure propagation,
retirement or profiling API cleanup. It is not a GitHub approval. No new overall
Studio speedup is claimed. Broad native/render/Studio/browser/backend gates remain
quarantined and unverified, with existing PR holds and required CI still binding.
The duplicate capture's retirement does not clear those requirements.
