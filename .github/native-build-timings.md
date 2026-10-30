# Native build timing evidence

The two native CI jobs set `CHIBI_BUILD_TIMINGS` only while building their
Release helpers and selected-profile products. The directory is included in
the existing `native-profile-*` artifact ("Preserve build logs and timings"), including successful builds. Tests
and developer builds do not enable tracing by default. The existing test driver's
`build-timings` selector explicitly exercises the private sink, including authored
child lifecycle cases, on both native jobs without rebuilding the drivers.

Each private build-host process claims one exclusive numbered TSV file in
that directory. Nested hosts inherit the directory but never overwrite or
interleave a parent's records. There are at most 64 files, and each invocation
has at most 4096 child steps plus six phase/identity records. Exhaustion or IO
failure prints a diagnostic; it does not change the build result. Missing a
terminal `invocation` record means that trace is incomplete, not a zero-cost
phase. No argv values, paths, environment values or source text are recorded.

Columns are category, step ID, monotonic start seconds, host-observed completion
seconds, status and error. A zero-step `graph-host`, `build-host`,
`generator-host` or `other-host` row identifies the invocation. `startup` covers
source/path setup; `compiler-load` loads the compiler generation; `prepare`
compiles/links/binds the build program; `execute` runs its authored entry and
cleans up outstanding children. `invocation` includes final host cleanup.
Phase error is zero on success and one on failure. Child error is the existing
`chibi_process_error` enum; child status is an exit code or negative signal and
is meaningful only when its error is zero. `abandoned` means a child was stopped
during host cleanup without an authored wait (including failure cleanup).

Child categories distinguish compilation/cache lookup, archive/ranlib, link,
reflection, asset generation, nested Graph/build hosts, compiler hosts, probes
and other processes. Classification uses command shape/name, not inspection of
the command's work; `compile` cannot distinguish a cache hit from a real compile
by itself. Use the independent ccache statistics too.

Completion is when the host observes it, not an exact kernel child-exit
timestamp. Async records retain the first observation while servicing any
child, rather than delaying their timestamp until their particular wait.
Synchronous intervals include process supervision and cleanup. Parallel and
nested intervals overlap: **do not sum them to claim end-to-end wall time**.
Use interval unions within a host's execution window to estimate time with
observed children active; remaining time is unattributed authored/host work,
not proof of a particular planner or filesystem bottleneck. Header staging,
dependency scanning and compile-database generation are not separately timed.

This trace bypasses buffered/truncated child stdout. Existing deadlines,
instruction/storage budgets, process cleanup, authored build outputs/results and build/test commands
remain unchanged. The opt-in trace overhead itself is included in those wall
deadlines; no performance improvement is claimed by adding measurement.
