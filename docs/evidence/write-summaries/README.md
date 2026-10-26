# WriteSummaries indexed traversal

The two outer loops in `package_compiler.WriteSummaries` now index their
unchanged `Body[]` input. On the bounded imports workload this removes
4,567,036 selected payload-copy bytes per compilation (3.74% of the measured
total). This experiment does **not** establish a wall-time improvement.

## Ownership and scope

The input is a by-value parameter. Neither the function nor its integer-only
callee, `JoinWrite`, changes it. Its synchronous callers finish constructing
the bodies before calling and resume changes after return. Only the new
integer summaries array and scalar loop state change during traversal.

Stage-zero foreach lowering retains the whole source collection and then each
managed element (`src/language/lower.c`, `SCRIPT_STMT_FOREACH`,
`declare_symbol`, `store_symbol`). Checked indexing followed directly by a
switch avoids those two retention sites. Input lifetime still follows normal
call argument retention/sharing, and argument values remain runtime roots.
The switch still evaluates its subject once. Union extraction validates its
tag, field bounds and shape; payload bindings still retain their values.
The inner calls loop, result retention, bounds checks, inspection and borrowing
guards are unchanged. No runtime code changes are part of this optimization.

The accepted base is `c2ad1f31917829ddf2342ff53a3efdd32412dc1a`, including
#704's native and scratch changes. Those changes leave these source loops and
stage-zero snapshot lowering applicable. The earlier attribution at
`84f7ba15e05423bd4ab3c3d29f4f360ee83c19ba` motivated this experiment; it is
not used as its timing baseline.

The authored stage-one compiler already recognizes unchanging foreach inputs
and reads immutable payload bindings in place. These numbers concern a
**stage-zero-prepared compiler executing with SLJIT**. They do not establish
the same copy or timing savings for a compiler produced by stage one.

## Matched experiment

The connected Mac ran macOS 26.7.1, arm64, with Apple Clang 21.0.0
(`clang-2100.3.34.2`). Both variants used one sealed executable, built from the
accepted runtime with Release `-O3 -DNDEBUG`, SLJIT, sanitizers off,
`-Wno-error=string-concatenation`, and `-Wl,-dead_strip`. Compiler source texts
were selected only during fixture construction: the accepted base source or
the candidate source. All other compiler sources, runtime code, C build,
workload inputs and probes were identical. No installation or pin change
was involved.

Six processes ran sequentially in AB/BA/AB pairs. Each prepared one fixture,
then compiled the existing `imports` and `loops` samples. Each sample had one
warmup per collection mode, followed by three balanced measured rounds per
mode. There are nine warm observations per variant/sample/mode, nested in
three processes, and 96 total compiler invocations. `loops` always followed
`imports`; this is not a cold-cache experiment.

Counters-off runs supply timing. Counters-on runs use the existing selected
C-copy counters and attribute snapshot deltas only while WriteSummaries is
active. The local classes are Body[] snapshots, Body element snapshots, and
all remaining snapshots. Counters never nest. The inactive mode still contains
probe branches, outer clocks and backend counters, so it is not a pristine
executable comparison. Instrumentation was confined to a disposable checkout;
final candidate validation uses no measurement probes.

The timed interval is inclusive `behavior_function_run`, including compiler
serialization and literal-JSON work. It excludes fixture preparation, input
construction, result detachment, closure loading and expected-output checks.
Build and whole-process wall/user/system CPU data are separate. RSS is a
whole-process high-water mark, not a per-invocation allocation measure.

Every invocation succeeded without compiler bytecode, inspection or size
fallback. Closures were byte-identical across all repetitions, collection
modes and source variants for each sample. Every sample call produced the
expected output on both bytecode and SLJIT. Independently stage-zero-compiled
artifacts passed the same output checks. The executable and source identity
were checked before and after every process.

## Copy results

Selected bytes sum text, nominal, record and value-buffer counters. They are
selected C copy-operation spans, not total memory traffic or CPU percentages.
Every counted invocation of a variant/sample gave the same vector.

| Workload | Baseline bytes | Candidate bytes | Reduction |
|---|---:|---:|---:|
| imports | 122,203,276 | 117,636,240 | 4,567,036 (3.74%) |
| loops control | 1,104,161 | 1,066,665 | 37,496 (3.40%) |

For imports, the removed whole-array snapshots copied 2,283,518 bytes in two
events, and element snapshots copied another 2,283,518 bytes in 132 events.
Both classes are zero in the candidate. The other 1,687 WriteSummaries
snapshot events still copy exactly 2,290,646 bytes. Thus its local snapshot
traffic changes from 6,857,682 to 2,290,646 bytes; the total payload reduction
is fully accounted for by these sites. No assumption that all 6.86 MB could
be removed was used.

Header moves are a separate unit. Imports removes 39,604 total header moves;
the removed snapshot sites account for 39,738, with 134 additional moves
elsewhere. The control removes 350 total header moves versus 354 at those
sites. These differences are retained in the evidence, not added to bytes.

## Timing results

Warm counters-off medians, milliseconds (nine observations each):

| Workload | Baseline wall / CPU | Candidate wall / CPU |
|---|---:|---:|
| imports | 314.533 / 314.144 | 311.789 / 311.506 |
| loops control | 3.888 / 3.889 | 3.931 / 3.931 |

Within each process pair, candidate wall-time changes relative to the
baseline process median were **+0.47%, -1.43%, -0.20%** for imports, and
**-0.31%, +5.42%, +1.61%** for loops. The pooled imports median alone is not
evidence of a stable speedup. The small control's timing also varies; no
throughput guarantee or independent CPU percentage is inferred from bytes.
Counters-on timings and all observations remain in the data.

## Correctness validation

The focused pico_unit regression has a test-only public wrapper in the
compiler package; the production function and Body type remain private.
It checks multiround propagation, a clear recursive cycle, failed and unknown
bodies, write dominance, unused managed payloads, aliased input entries,
unchanged inputs, caller replacement between calls, mutation of detached
results, empty input and an invalid call edge. Both backends run with ordinary
and collectable producer arenas. Saved results are read after producer,
input and fixture teardown. The invalid-edge case checks failure category,
cleanup, real backend entry and zero fallback. The prepared private function
must contain no Body/Body[] snapshot intrinsic; payload snapshots are allowed.

Completed Release checks on the candidate without probes:

| Selector | Cases | Assertions |
|---|---:|---:|
| package-compiler-chibi-write-summaries | 1 | 781 |
| package-compiler-chibi-imports-compiled | 2 | 13,231 |
| package-compiler-chibi-collections-compiled | 1 | 4,371 |
| package-compiler-chibi-libraries | 13 | 17,407 |
| package-compiler-chibi-bootstrap | 1 | 23,630 |

The bootstrap check prepares stage one through stage zero and verifies the
subsequent compiler stages reproduce identical bytes. It is distinct from
the pinned-runtime, source-only recovery workflow. No new syntax, target ABI,
bootstrap minimum or artifact-format change is introduced. The initial claim
that regeneration was unnecessary missed the scalar harness's stricter source
and artifact identity gate. The follow-up [artifact repair](artifact-repair.md)
refreshes the reviewed compiler and manifest through the normal seed workflow;
the compiler source and measurements above are unchanged.

ASan/UBSan (`-fsanitize=address,undefined`, frame pointers,
`UBSAN_OPTIONS=halt_on_error=1`) passed the focused case (781 assertions),
27 borrowing cases (848 assertions), and 167 program/retention/capture/query
cases (12,450 assertions), with no sanitizer diagnostics. The original
foreach implementation fails the new Body snapshot assertion; restoring the
candidate makes it pass. No forced collection during a call is claimed.

Full native/material/project/tool suites, the complete language group, GPU
checks, other platforms and the pinned-runtime recovery workflow were not run.
The selected checks cover the changed traversal, managed inputs and results,
backend behavior, propagation, errors and self-reproduction; broader CI remains
applicable before landing.

The initial regression attempt stopped because it looked up a private
function as an export; the test-only wrapper corrected that setup. An initial
measurement build failed on a private-header include before execution. Both
were fixed before the final measurement seal. The first supervised imports
command could not inspect child processes in the sandbox and started no test;
the normal supervisor then ran successfully with process-inspection access.
The sanitizer build reached its initial 180-second bound and completed after
resuming with a 240-second bound. An unknown `language-retention` selector
started no test; the existing `language-program` selector ran retention cases
successfully. These setup failures are not counted as passing checks.

[validation.json](validation.json) records commands, results and tested source
hashes. [measurements.json](measurements.json) contains exact base/tree, compiler-source
and executable hashes, all process/resource records, per-invocation counters,
timings, site deltas, closure hashes and paired summaries. No unmeasured
historical executable is used as a performance baseline.
