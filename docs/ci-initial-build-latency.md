# Initial native CI build requests, 2026-10-24

The Linux and macOS native jobs request the products and sanitizer test drivers
in one `make build` invocation. The target list is the union of the previous
`make test-build` and default `make build` requests:

```
chibi chibi-compile chibi-player test_tools test_language
```

The Makefile still owns build policy. Its sanitizer profile, compiler flags,
output directory, job limit, build-host recovery and graph/native routes are
unchanged. The graph route still adds its build host. Release products, tooling
checks and all four sequential native groups remain separate and unchanged,
including the groups' shared `test-build` prerequisite and the project group's
Release prerequisite. A failed initial build still fails the job before tests.

## Measurement

Measurement baseline is main `4bc9faf664f98ebfde55f59297db8cdde5e1887d`.
The candidate is based on `9ec077565476f137d6e2cd003502528963f5d07b`, which
adds the installed macOS export check without changing build policy. On the
measurement baseline, the
existing graph's `--commands-only on --all-commands on` mode planned the separate
requests and their union. The build host used Debug with address and undefined
sanitizers, matching the CI host configuration. The graph target profile was
`sanitizer`, with two jobs, installed LLVM 23.1.1 and the existing pinned
third-party archive. No engine source was compiled or linked during these runs.

| Trial | Separate requests | Combined request | Difference |
| --- | ---: | ---: | ---: |
| 1 | 71.296 s | 35.998 s | 35.298 s |
| 2, combined first | 70.997 s | 36.002 s | 34.995 s |
| 3 | 71.010 s | 36.044 s | 34.965 s |

All 975 compile-command entries were identical across the requests; the combined
request's 10 link-command entries exactly matched the separate requests' union.
The paths, flags and output names were compared, not only counts. A preliminary
Release-host probe was excluded because CI's host is sanitized Debug.

This isolates planning from compilation and linking. It does not measure the
full incremental dependency/staging pass, a cold engine build or hosted runner
speed. The separate product step took 98–128 seconds in recent hosted runs;
some of that work compiles and links products that the driver request does not
build. Those seconds cannot all be claimed as savings. Compare the combined
step against the sum of both former steps when assessing fresh CI.

To repeat the planning comparison, build only `chibi-build-host` from a Debug
ASan/UBSan configuration with two jobs. Run it against `tools/toolchain/Graph.chibi`
with the normal compiler/core inputs, `--collection graph=tools/graph`, entry
`Libraries`, and these graph arguments:

```
--build /absolute/scratch/plan --profile sanitizer --jobs 2
--commands-only on --all-commands on
```

Append `--target NAME` for each request: the driver set is `test_tools
test_language chibi-build-host`; the product set is `chibi chibi-compile
chibi-player chibi-build-host`; the combined set is their union.
Keep the same source/output paths and environment for all requests. Retain the
compile/link JSON after each invocation, and alternate the run order.

## Regression

The existing C `--test-runner` dispatcher reads the initial commands from both
workflow jobs and runs an unchanged copy of the Makefile in a disposable
directory with recording build children. It covers cold build-host recovery and
existing-host routes. It
requires one request with the complete target set, the original sanitizer
profile/flags and the supplied job count. Failure is injected at the native
build, configuration, build-host bootstrap, third-party preparation and graph
boundaries; later work cannot run after
a failed boundary. The original four-group ordering/failure regression remains.

The new case fails on the baseline's two requests. Full hosted native suites
remain responsible for actual product/driver compilation and runtime coverage;
the recording fixture does not substitute for those checks.

On candidate base `9ec077565476f137d6e2cd003502528963f5d07b`, the tooling
dispatcher passed 19 cases and 205 assertions. Clang-format 22 and diff checks
passed. Actionlint reports the same six existing diagnostics as baseline. Full
engine builds and native suites are delegated to the fresh hosted CI run.
