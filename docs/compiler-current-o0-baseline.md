# Current compiler: full-rebuild O0 baseline

2026-11-04 UTC. Independently checked. Five fresh samples have a **47.001-second median**, or
**1,629 original parsed LOC/s**. Every output matches a separately tested O0
compiler byte for byte. This remains about 61 times the 0.76569-second budget
for the 100,000 LOC/s target. No compiler default or backend has changed.

## Workload

Rebuild the real compiler from original source to a usable Linux x86-64 ELF,
using the same admitted **O2 input compiler**, source closure, LLVM 22 tools,
explicit jobs 2 and affinity `[0,1]` as the
[historical O2 campaign](compiler-current-o2-baseline.md). The output compiler
is built at level 0. This is a whole nonincremental build, not emit-only output,
an incremental cache hit, or a vNext implementation measurement.

- Source commit: `02c86287430def964445ec90185961e20319536d`.
- Source tree: `780ba87ea9f29308f2f20a94381cdce8cc5d26ca`.
- Prebuilt O2 builder SHA-256:
  `7b17b5bfdaed144e0949afd065db723bb45c8bd55b356cb70910d6df42e6d266`.
- Exact O0 output SHA-256:
  `d92500a3e1b8f9cc4066362de3ef609c4270c838f10e4d8a64afa3d7483884f2`.
- Output size: **59,624,208 bytes**, versus the O2 output's 11,576,992 bytes.
  File size is not peak memory use, runtime performance or machine-code size.

The descriptive command shape is not an armed or portable replay specification:

```text
<accepted-O2-builder> --compiler <source>/tools/compiler --core <source>/packages/core --output <fresh-output>/chibi --llvm <llvm22-bin> --target x86_64-unknown-linux-gnu --level 0 --jobs 2
```

Every invocation gets a new process, output directory, HOME, TMPDIR and cache
locations. The raw build route bypasses `CachedBuild`; no prior application
AST, IR, object or output is supplied. Installed compiler, tools, loader,
libraries and CRT/libc link inputs are declared prebuilt dependencies. Runtime
and start IR are regenerated inside the measured command. Level 0 still runs
`opt default<O0>`, `llc -O=0` and final linking; it does not remove LLVM.

## Correctness admission

Before the series, the O0 oracle passed static ELF admission and the exact
**573-test compiler inventory**. A second fresh build by the same original O2
builder reproduced the oracle's raw bytes. Those two admission builds are
preserved separately and are not inserted into this sample series.

This is **not an O0 selfhost fixed point**. The unchanged compiler-test route
compiled test programs at its default O2 and retained its `Jobs([])`/`getconf`
object-worker selection, rather than the campaign's explicit jobs 2. The suite
includes a filesystem-effectful test, so it is not entirely pure. No produced
compiler from the six-build campaign was executed, and the suite was not rerun
per sample: exact raw equality transfers the already admitted oracle evidence.

## Measurement and results

The denominator is unchanged: **76,569 original physical lines passed unchanged
to parsing**, including comments, blanks and both target-import branches, over
121 original files and 31 packages. The compiler reads 76,592 original lines;
the 23-line target template replaced by 16 generated lines is excluded from the
unchanged-original denominator. Original parsed bytes are 3,829,984. Generated
text is reported separately, not counted as original LOC or per specialization.

Monotonic command time runs from immediately before process creation through
observed leader exit. It includes startup, source work, compiler-internal
hashing, LLVM tools, runtime/start generation, linking, publication and compiler
cleanup. Evidence hashing, source/tool guards, host metadata, output validation,
owned descendant cleanup and receipt writing are outside that compiler timer.

- Excluded warmup: **48.462327433 seconds**.
- Samples 1–5 in order: **47.001361032, 46.443293692, 46.110402721,
  47.984012118 and 49.690535001 seconds**.
- Median: **47.001361032 seconds**. Minimum: **46.110402721 seconds**.
- Maximum and nearest-rank p95: **49.690535001 seconds**. With only five samples,
  that p95 is simply the maximum, not a precise tail estimate.
- Rate: **1,629.0804844538 original parsed LOC/s**.
- Outer owned worker: **305.02206692 seconds**, plus **0.00124416 seconds** cleanup.

Independent terminal readback verified all 4,054 source entries, 177 configuration
pins, nine loader-search snapshots, exact launch/argv/environment/specifications
and six direct raw comparisons. All six slots passed, in order, without replacement: exit 0, empty compiler
streams, exact output hash and raw equality, unchanged input fences, clean
ECHILD, restored subreaper, no timeout, surviving child or cleanup error. Each
compiler had a 120-second cap plus one-second cleanup; the outer worker had a
900-second cap plus one-second cleanup. Outer admission and final receipt
writing are outside that owned interval. No limit was extended.

Verification warmed filesystem pages. These are warmed, shared virtualized-host
runs, not cold-storage or exclusive-host measurements. The known competing
native lane remained paused. The host/CPU limitations in the O2 note still
apply, including unavailable cgroup quota/memory-limit files. Jobs and affinity
are not a guarantee of exactly two total OS threads.

## Interpretation

The historical O2 median was 96.515532821 seconds. O0's elapsed median is **51.30%
lower**, a **2.053× throughput ratio**, for this workload/configuration comparison.
The campaigns were sequential, not paired or randomized. The earlier O2
campaign also omitted the identity pin for the metadata-only `uname` helper;
its [original qualification](compiler-current-o2-baseline.md#later-metadata-dependency-qualification)
still applies. This new campaign prospectively pins that helper and does not
repair or rewrite the historical evidence.

This supports treating optimization level as a substantial development-build
latency tradeoff. It does not establish exclusive LLVM time, a backend winner,
or acceptable generated-program runtime performance. Do not subtract either
full-build result from the [16.409-second emit diagnostic](compiler-current-emit-diagnostic.md)
and label the difference exclusive backend cost: those output paths differ.
All O0 outputs here were built by the original O2 compiler. Repeated builds
using the produced O0 compiler are a different, unmeasured workload.

This is exploratory characterization, not the twenty-build acceptance series,
a direct Jai comparison, a platform/release gate or 100k success. Exclusive
phase wall/CPU times, peak RSS, allocation/clone/RC counts, noncomment LOC,
token/specialization counts and produced-program regression budgets remain
unmeasured. The backend-neutral typed-IR work can continue independently; this
result alone does not justify abandoning LLVM or switching defaults.

## Evidence

The [derived result record](../tools/experiments/vnext/evidence/current-o0-20261104.json)
contains exact values, identities and hashes of retained original artifacts.
It is a selected public summary, not raw receipts or a runnable replay bundle;
machine-local paths and process details are omitted. Hashes identify original
bytes, not this derived record. New tools or paths require fresh admission.

The six-stage controller differs from the earlier campaign by the reviewed
`-B` worker argument. The baseline/ownership harness is unchanged; all 12
focused controller controls passed. Explicit armed fields and independent
readback govern launch. Retained preparation descriptions still say unarmed;
those frozen historical labels were not rewritten to imply a different state.
No production source, CI gate, bootstrap pin, release or default changed.
