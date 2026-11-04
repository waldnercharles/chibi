# vNext baseline preparation

This is measurement plumbing, not a compiler benchmark or a speed claim. No
accepted current Chibi builder is available for this experiment yet. Do not
retry an uncertain bootstrap or run compilation without CPU coordination.

Python 3.11+ and Git are the only dependencies. No packages need installing.
Command measurements additionally require Linux `waitid(WNOWAIT)`, pidfds,
process-local subreaper support and readable procfs; unavailable capabilities
are rejected before launch without a fallback or privilege change.

## Static source census

From the repository root, choose a new output filename outside the checkout.
The pins below identify the earlier plan checkpoint, not a later commit carrying
this harness. Replace them with the independently verified intended source
commit and tree before running:

```sh
python3 tools/experiments/vnext/baseline.py census \
  --source-root "$PWD" \
  --expected-head caffb12bce7ecb92044c21ce0ee214589b4d76f7 \
  --expected-tree 0fc0e51cc4c3e78fc14aafbdab7d16f97adc951b \
  --output /tmp/chibi-vnext-census-001.json
```

Use new full commit and tree identities whenever HEAD changes, even for a
documentation-only commit, and regenerate the census.
The command refuses to overwrite a receipt. It checks the repository root, HEAD,
tree, compiler/core source status, and each source's committed blob identity.
Uncommitted edits to documentation or this harness do not fail the compiler/core
source guard; they do not waive the HEAD/tree checks.

The census follows the existing native_driver Gather's `project:` and `core:`
imports, with the entry package `native_driver` and `Main.chibi` first. It counts
physical lines, including comments/blanks, both target branches and even
import-looking block-comment lines that Gather's line scanner sees. Files ending
in `Tests.chibi` are omitted. This is not active-target source, a full parser,
token counts, generated-source accounting or a compilation. Unsupported import
collections are not included; this fixed workload uses project/core only.
Collection-root imports (`project:.` and `core:.`) are supported; output paths
cannot name `.`. Traversal outside a collection and symlinked source paths are
rejected. Limits
are 4,096 packages, 16,384 files, 16 MiB per file and 256 MiB total source.

## Harness regression tests

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/experiments/vnext/baseline_tests.py -v
```

These use the checkout's current clean compiler/core source closure plus tiny
Python child processes in temporary directories. They do not compile Chibi or
C, invoke an existing builder, modify source, or create commits. Tests cover
source/tool mismatches, closure tampering, deadline termination, surviving children, unsuccessful
processes, preserved logs, fresh output isolation, missing/empty/wrong/symlinked
outputs, post-run identity changes and honest measurement labeling. Regression
controls also cover kill-before-reap ordering, interrupted Popen assignment,
timer construction/start failure, procfs read failure, receipt write failure,
unsupported platforms and the separate cleanup timing boundary.

## One candidate run, when an accepted builder exists

Create a JSON specification with this shape. All placeholder identities below
must be replaced with verified values. The first tool must exactly match
`argv[0]`; pin every transitive compiler, linker, runtime binary and wrapper or
build script in `tools` too. Supply flags for all intended target, optimization,
thread-count and cache controls in `argv`. Do not put secrets in the environment
or specification: the complete specification is preserved in the receipt.

```json
{
  "schema": 1,
  "classification": "chibi_candidate",
  "workload": "native_driver full development AOT, exact target and jobs",
  "source": {
    "root": "/absolute/source/root",
    "head": "FULL_APPROVED_COMMIT",
    "tree": "FULL_APPROVED_TREE",
    "census": "/absolute/census.json",
    "census_sha256": "SHA256_OF_CENSUS_RECEIPT"
  },
  "tools": [{"path": "/absolute/prebuilt/chibi", "sha256": "VERIFIED_SHA256"}],
  "argv": ["/absolute/prebuilt/chibi", "EXPLICIT_BUILD_ARGUMENTS", "{output_dir}/program"],
  "environment": {"PATH": "/explicit/tool/directories", "LC_ALL": "C"},
  "timeout_seconds": 120,
  "expected_outputs": [{"path": "program"}]
}
```

```sh
python3 tools/experiments/vnext/baseline.py run \
  --spec /absolute/run-spec.json --run-dir /absolute/new-run-directory
```

The run directory must not exist, including as an empty directory or symlink.
Its parent must exist. Expected outputs must be nonempty regular files beneath
its fresh `output/` directory. Each output may also have an expected `sha256`.
A zero exit plus file existence is only an artifact check, not semantic/runtime
correctness. Validate the resulting program separately before accepting a
baseline; include any finalization required to make it usable inside the timed
command. There is no hard-coded compiler invocation because no current one has
been accepted. Use `harness_self_test` for dummy commands, never `chibi_candidate`.

The command runs directly, without a shell, from the fresh output directory.
Only the explicitly specified environment is inherited by the child, plus fresh
`HOME`, `TMPDIR` and `XDG_CACHE_HOME`. Placeholders `{source_root}`, `{output_dir}`,
`{cache_dir}` and `{run_dir}` are expanded in arguments. Direct compiler flags
must point every intermediate/cache to this run or explicitly disable reuse.
These directories cannot prevent an arbitrary command from using an external
cache or compiler daemon. The caller must inspect that contract. The shared
`owned_process.py` operation temporarily makes its one-purpose caller a Linux
child subreaper. It rejects pre-existing children without reaping or signalling
them. The deadline first kills the newly started leader's group. The leader
PID remains reserved with `waitid(WNOWAIT)` until callbacks stop and the final
group signal finishes. Cleanup then signals only pidfds bound to actual kernel
direct/adopted children and repeatedly reaps them until `waitpid` reports
`ECHILD`. This covers workers in separate process groups and orphaned nested
descendants without claiming that a procfs scan proves absence. No group is
signalled after leader reap. The prior subreaper state is restored afterwards.
Commands must wait for all required work and must not deliberately daemonize or
detach work. Any adopted descendant rejects a successful leader's result.

`result.json`, `stdout.log`, `stderr.log` and partial outputs survive failures.
SIGINT or SIGTERM during owned process launch/setup/wait terminates and reaps
the command, records its exit status and signal, and produces a failed-run
receipt. Timer startup failures use the same ownership cleanup. This requires
running the harness on its single main Python thread, with no pre-existing
children, the default SIGCHLD disposition and no other child reaper. Before
launch it explicitly establishes kernel SIGCHLD default/no-auto-reap semantics,
including clearing native flags that Python's cached disposition cannot reveal.
SIGKILL, an abrupt interpreter
crash or host failure cannot be handled: they may leave children or omit the
receipt. An external supervisor is needed for guarantees across those failures.
Nonfinite JSON
numbers, including overflowed exponents and unused fields, are rejected before
launch. Receipts are fully serialized before a new file is opened.
Exit status is 0 for passed command/output checks, 1 for a recorded failed run,
and 2 for invalid inputs or a harness/environment error. Identity preflight
failures do not launch the command or create the run directory.

## Measurement boundary and limitations

Elapsed monotonic wall time starts immediately before process creation and ends
when leader exit is observed. It includes process startup, all compiler work and
children the command waits for, and finalization/linking.
Leader observation polls every 5 ms, adding up to roughly 5 ms of observation
delay plus scheduler delay to fast commands; these are not precise
submillisecond measurements. Watchdog shutdown,
procfs child inspection, descendant termination/reaping and subreaper restoration are
reported separately as `cleanup_seconds`; they do not turn an already completed
command into a timeout. Cleanup has an explicit one-second allowance in addition
to `timeout_seconds`; the configured overall ownership budget is therefore
`timeout_seconds + 1`. An outer phase with a fixed total budget must reserve
that second before choosing the execution limit. Receipts preserve
`cleanup_limit_seconds`, `overall_limit_seconds`, `cleanup_echild` and
`subreaper_restored`. Cleanup errors, unavailable ownership capability or failure
to reach `ECHILD` cannot be accepted as success. Blocking OS operations and
controller termination still require an external supervisor for hard wall-clock
guarantees. On setup/wait failures, leader-exit observation may
include termination cleanup, and that failed run is not a timing sample. Source/tool identity
hashes and static census verification happen before and after that interval;
output validation/hashing and receipt writing happen afterwards. These are
harness checks, never compiler precomputation. Any compiler hashing, parsing,
IR preparation, code generation or input-dependent work belongs inside the
measured command. Only installed prebuilt tool/runtime dependencies may be
prepared in advance; regenerating them is a separately reported build.

Preflight reads warm source/tool filesystem pages, so this cannot establish a
cold-cache result. Receipts label the filesystem state as uncontrolled/warmed.
Keep repeated warm and properly controlled cold experiments separate. Record
hardware model, OS, CPU/thread/affinity policy and concurrent-load conditions
alongside accepted results; the receipt only discovers OS, architecture and CPU
count. Source/tool postflight checks detect persistent changes, not temporary
changes restored before verification. Keep inputs immutable during each run.

No phase breakdown, allocation count, peak RSS or LOC/s is invented. Those fields
remain unmeasured. Static Gather lines are not yet an accepted full-compilation
throughput denominator. A successful candidate still needs independent evidence
of eager full compilation, empty effective compilation caches, correct source
closure and runtime correctness before comparison with the 100,000 LOC/s goal.
