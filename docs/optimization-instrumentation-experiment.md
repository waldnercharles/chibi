# Opt-in O1/O2 × sanitizer experiment

The manual `debug-optimization-experiment.yml` workflow measures four Debug arms:
O1 and O2, each with ASan+UBSan on and off. This supersedes the runner's original
O0/O1 comparison; it changes neither ordinary CI nor developer/macOS defaults.
It does not remove sanitizer coverage or substitute for full-suite validation.

Dispatch with an exact reviewed source SHA. Two independent Ubuntu 24.04 matrix
jobs each run one sequential O1/O2 pair with a fixed instrumentation profile.
Each retains the existing 45-minute job ceiling and 35-minute supervised aggregate
budget, including configuration. Failure or timeout preserves partial logs but
produces no completed comparison. Do not raise these limits to fit four builds.

Both jobs use the same source SHA, dependency source manifest, Clang 18 and pinned
CMake requirements, four build jobs, no ccache, fresh separate build directories,
and the same four selectors. Within each pair the exact pinned dependency binary
archive is shared. Across sanitizer profiles the corresponding `address-undefined`
and `release` archives are intentionally different: this is a realistic whole-build
instrumentation comparison, **not** an isolated first-party instrumentation cost.
Their exact archive hashes and manifest hash are retained in metadata.

All arms retain assertions, `-g`, and `-fno-omit-frame-pointer`. Representative
engine, SDK, driver and C++ shim compiler argv must match the actual verbose build
log and compile database. Generated `export_compiler.h` fixture arguments are
checked too. Sanitized arms require ASan+UBSan; unsanitized arms reject sanitizer
flags. This verifies executed compilation, not merely requested CMake options.

Configure both arms before either build, then build O1 followed by O2. Test each
selector in alternating O1/O2, O2/O1 order. Record build, four-selector execution,
and their sum separately; configuration/dependency downloads are separate phase
records. GitHub job timing supplies queue/provisioning costs; `JOB_STARTED_UTC`,
runner image, machine specifications, toolchain versions, command argv and phase
wall seconds are retained for interpretation. Setup/queue cost is not a compiler
or execution speedup and must not be folded into those comparisons.

## Interpretation limits

Optimization comparisons are matched **within** a runner; instrumentation
comparisons are **across** separate runners. The same runner image is not the same
physical machine. Before comparing, require equal source/manifest hashes,
compiler/CMake versions, image version, job count and selector list. Report any
hardware differences and do not attribute them to instrumentation. Package setup
may also differ across runners; retain workflow setup logs. There is no shared
object cache or overlapping builds on one runner, but OS/file-cache warmth and
fixed O1-before-O2 build order remain confounds. One pair per profile is exploratory
measurement, not a statistically established speedup or a CI policy decision.
