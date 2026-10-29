# Tooling case wall timings

`CHIBI_TEST_TIMINGS` privately opts the native `test_tools` driver into a separate
TSV file in an existing directory. CI sets it for tooling and Linux project
selectors and keeps the files with the existing seven-day native-profile evidence.
It is off by default; pico's output, assertions, hooks and CPU-time reporting are unchanged.
The vendor header is unchanged. Both the CMake and authored Graph source lists
include the test-only wrapper.

Each process creates a unique mode-0600 file with a version header and columns
`event`, `ordinal`, `case`, `wall_ns`. Match a `begin` to an `end` within the same
file by ordinal, not case name: names may repeat. Begin records are flushed before
the original test runner is entered, so an unmatched begin identifies an
interrupted case, not a completed zero-duration case. The begin duration is a
placeholder zero; only end rows contain measurements. End durations use
`CLOCK_MONOTONIC` around the original call, including setup, test, teardown and
waiting for children. The wrapper calls pico exactly once on every path.

Files stop at 4,096 cases, case identifiers at 128 bytes, and total output stays
below 2 MiB per process. Truncation is marked; sink errors warn on stderr and
stop measurement without changing test results. No arguments, environment
values, working directories or child logs are recorded. Private files are not
inherited across exec. Normal child process ownership and deadlines are unchanged.

Sort completed end rows by wall duration to find expensive cases. Do not sum
across concurrently running drivers, interpret wall time as CPU time, or infer
case starts from buffered GitHub `Running:` output. Suite setup/cleanup outside
cases, driver startup, build time, and trace I/O are **unmeasured residual**, not
attributed to a case. This first pass does not identify CPU versus I/O cost or
individual child spans. Measure before optimizing repeated publishing setup.

`test_tools --case-timings` runs the C/pico regression suite in the existing
dispatcher. It invokes only its private `--case-timing-probe` selector to check
output/result parity, duplicate-name ordinals, setup/teardown and child waits,
private unique sinks, bounded output, sink errors, and interrupted cases.

Local validation uses strict GCC and the actual wrapper, regression and fixture
sources in one dead-code-stripped scratch dispatcher. The full current native
build and macOS execution require hosted CI; a focused local pass is not evidence
that those larger suites passed.
