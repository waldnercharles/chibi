# Native CI build planning, 2026-10-24

Historical record. The Makefile was retired in e4aa7b4e2 and the C
`--test-runner` regression in 84ca9d25c. CI now runs the groups with
`chibi build test` (`tools/build.sh test`), which builds nothing itself.

The Linux and macOS native jobs then invoked the four existing test-group targets
in one sequential Make process. Their shared `test-build` prerequisite runs once
instead of four times. The project group's Release build still runs, as do the
initial sanitizer build, product build, Release product build and tooling tests.
`-j1` orders the groups; the build recipes retain their existing compiler job
limit. Linux retains `UBSAN_OPTIONS=halt_on_error=1`, now for all four groups.

No selector, dependency identity, persistent cache or cross-run artifact policy
changes. Every invocation still checks the normal build dependencies. This uses
the same shared-prerequisite behavior the Makefile's `test-full` target used.

## Hosted baseline

Source baseline: `0f45ed129742965122f7474c53301a1f829f1743`.
Times below come from timestamped job logs, excluding queue time.

| Run / job | Observed duration | Finding |
| --- | ---: | --- |
| [Post-merge macOS](https://github.com/waldnercharles/chibi/actions/runs/40042140030/job/120715437656) | 23m00s | Initial test build 8m21s; four later test-build entries total about 6m25s |
| [Shader macOS](https://github.com/waldnercharles/chibi/actions/runs/40039480502/job/120707002723) | 23m39s | Same repeated planning; initial test build 8m08s |
| [Post-merge Linux](https://github.com/waldnercharles/chibi/actions/runs/40042140030/job/120715437424) | 7m17s | Repeated native-toolchain entries also cost time |
| [Post-merge graph SDK](https://github.com/waldnercharles/chibi/actions/runs/40042140030/job/120715437744) | 3m18s | Finishes well before macOS |
| [Post-merge Studio](https://github.com/waldnercharles/chibi/actions/runs/40042140030/job/120715437615) | 2m01s | Finishes well before macOS |
| [Assert self-build macOS](https://github.com/waldnercharles/chibi/actions/runs/40042832661/job/120717621197) | 1m18s | Compiler self-build is not the native-job tail |

The assert-update native workflow, run `40042832584`, was still running during
the initial inspection. It is not included as a completed timing sample.

In the post-merge macOS run, native/material/project/language test commands took
35.843 / 40.050 / 44.031 / 40.508 seconds. The language step's apparent tail
included 113.8 seconds of third-party and graph planning before its tests began.
The initial build is the largest individual cost. This change addresses the
repeated planning separately without changing the graph or compiler.

The three removed test-build entries cost 285.4 seconds in the post-merge macOS
run and 296.6 seconds in the shader macOS run. Expected savings are about
4m45s–4m57s per macOS job, about 21% of these job durations. Linux's corresponding
entries cost 37.4 seconds. These are estimates from existing runs, not measured
whole-workflow improvements. Runner load, changed source and any work previously
performed by those later builds can affect the result. Fresh CI must confirm it.

## Local incremental measurement

On the development Mac, the real Release graph built `chibi_support` and its
normal build-host dependency with `JOBS=2`. After priming that isolated output,
four sequential Make invocations took 14.881 seconds. One Make invocation with
four goals sharing the same prerequisite took 3.600 seconds, saving 11.281
seconds. Every measured graph invocation reported `compiled 0 sources`; SHA-256
hashes of all 150 object, archive and build-host files stayed identical.

This is a bounded measurement of graph re-entry, not a full test-driver build or
a prediction based on local machine speed. Its reproduction recipe used a
scratch Makefile beside the retired root Makefile and no longer runs.

## Regression and validation

The `--test-runner` C dispatcher read both native jobs' group commands
from the workflow and executed them through the actual Makefile. Recording
children replaced compilation and test execution. Assertions required one build,
all four groups in order, the intervening Release build, and nonzero Make exit
with no later work after failure at each of those six boundaries.

The regression failed on the original workflow. After batching, the dispatcher
passed all 18 cases and 187 assertions, including process-cleanup checks.
`make check-format` passed with clang-format 22. The workflow parses as YAML;
actionlint reports the same six pre-existing diagnostics as the baseline,
covering existing shell style and disabled Windows jobs.

Local full native/material/project/language suites were not rerun for this
workflow-only behavior change. Their inventory, drivers, sanitizer builds and
platform jobs remain in CI. The C regression and real incremental measurement
cover the changed scheduling contract; the draft PR supplies full hosted checks.
