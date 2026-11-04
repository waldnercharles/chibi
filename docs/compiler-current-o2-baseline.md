# Current compiler: first full-rebuild O2 baseline

Date: 2026-11-04 UTC. **Five-sample exploratory baseline independently checked.**
Current-default O2 median: **96.516 seconds**, or **793 original parsed LOC/s**.
This is not a development-O0 result or a claim that the target has been met.

## Workload and input identities

Rebuild the real `native_driver` compiler from original Chibi source to a usable
Linux x86-64 ELF. This is the existing compiler's **current-default O2** route,
not development O0 or a vNext backend. Source is commit
`02c86287430def964445ec90185961e20319536d`, tree
`780ba87ea9f29308f2f20a94381cdce8cc5d26ca`. Documentation and harness checkpoints
are separate identities; moving this document does not change the workload.

The independently admitted stage4 input compiler has SHA-256
`7b17b5bfdaed144e0949afd065db723bb45c8bd55b356cb70910d6df42e6d266`
and file size 11,576,992 bytes. Prior stage3/stage4 outputs were raw-identical and
the exact 573-test compiler inventory passed. The frozen baseline harness is
identified by checkpoint `b92c073b01e77033e9d3aaea928bc59fbb25105b` and file hashes;
the serial controller and its configuration require their own identities.

The command shape below describes the workload, not a ready-to-run specification.
Names in angle brackets stand for individually pinned absolute paths:

```text
<accepted-builder> --compiler <source>/tools/compiler --core <source>/packages/core --output <fresh-output>/chibi --llvm <llvm22-bin> --target x86_64-unknown-linux-gnu --level 2 --jobs 2
```

Each run uses a fresh process, output directory, HOME, TMPDIR and cache locations.
The raw route bypasses `CachedBuild`; no AST, semantic state, IR, object or
application artifact is reused. Installed compiler, LLVM 22, loader, shared
libraries, CRT and libc link inputs are declared prebuilt dependencies. Runtime
and start IR are regenerated inside the measured build. Rebuilding installed
tools is a separate workload.

## Denominator and timing boundary

- Original source read: 121 files, 31 packages, 3,830,672 bytes and **76,592
  physical lines**, including comments/blanks and both target import branches.
- Original source passed unchanged to parsing: **76,569 physical lines** and
  3,829,984 bytes. This is the conservative throughput denominator: subtract the
  replaced `core:target/Target.chibi` template's 23 lines and 688 bytes.
- Generated target text: 16 lines and 235 bytes, regenerated during every build
  and reported separately. It is not added to original LOC. Count each original
  line once, never once per specialization or pass. This is not active-target-only
  source; conditional-import source is parsed even when not semantically active.

At 100,000 original parsed LOC/s, this workload's budget is **0.76569 seconds**.
The separate all-original-lines-read budget is 0.76592 seconds. Neither is a
measured runtime, an acceptance result or a directly comparable Jai benchmark.

Monotonic command time starts immediately before process creation and ends when
leader exit is observed. It includes startup, source loading, semantic work,
lowering, LLVM version probes, LLVM generation and opt/llc work, runtime/start
generation, linking, publication and the compiler's own cleanup. Hashing the
compiler itself performs stays inside that time. Evidence hashes, input guards,
output validation, harness cleanup and receipt writing are outside it.

Preflight warms filesystem pages. These are warmed, uncontrolled-page-cache
runs, not cold-storage measurements. Preparation identified an AMD EPYC 9V74
virtualized host with nine visible logical CPUs, Debian 13.6 and Linux 6.18.57.
The campaign uses affinity `[0,1]` and `--jobs 2`, not a promise of exactly two
OS threads. A coordinated quiet start does not establish an exclusive host.
Cgroup quota and memory-limit files were unavailable; keep that limitation.

## Results

One fresh warmup precedes five serial fresh measured samples, without selection
or replacement. The warmup is preserved and excluded from the aggregate.

- Warmup: **98.268114317 seconds**, exit 0, no timeout or cleanup errors,
  `ECHILD` reached and prior subreaper state restored. The output is raw-identical
  to the admitted compiler and has the expected SHA-256 and executable mode.
- Samples 1–5, in order: **96.515532821, 92.915245091, 100.060811674,
  90.235333957 and 97.549658843 seconds**. Every sample passed.
- Median: **96.515532821 seconds**. Minimum: **90.235333957 seconds**.
  Maximum and nearest-rank p95: **100.060811674 seconds**. With five samples,
  nearest-rank p95 is the maximum, not a precise tail estimate.
- Rate: **793.3334434573 original parsed LOC per median second**.
- Outer owned worker: **593.428963741 seconds**, plus **0.001098789 seconds**
  cleanup. No missing or replacement sample, timeout, surviving child, input
  mismatch or nonempty stdout/stderr was observed.

The first controller launch failed Python identity preflight before campaign
creation or any compiler execution. Its preserved failure receipt is part of
the record, not a compiler timing sample. Keep every later failed, interrupted,
contaminated or unrun slot visible; do not substitute another run.

Each compiler has a 120-second limit plus a separate one-second owned-cleanup
allowance. The owned campaign worker has a 900-second limit plus its separate
one-second cleanup allowance. Outer admission and final postflight/receipt writes
are outside that worker interval. These limits are not completed-run timings.

Every sample passed process cleanup, unchanged input checks, static ELF checks
and exact raw comparison to the admitted compiler. Independent final readback
verified all 4,054 source entries, 160 pinned inputs, nine loader-search
snapshots, exact configurations, serial monotonic order and the final receipts.
Raw equality with the same runtime closure transfers the earlier compiler-test
evidence; it does **not** mean 573 tests were rerun on each output. The known
competing native lane remained paused throughout; shared-host uncertainty remains.

## Interpretation and remaining measurements

This is exploratory O2 characterization, not the twenty-build acceptance series,
the best estimate of development-mode speed, a backend winner or a release gate.
No exclusive phase wall/CPU times, peak RSS, allocation/clone/RC counts, token or
specialization counts, typed/emitted function counts or produced-program runtime
budgets have been measured by this campaign. Original noncomment LOC is also
unmeasured. Pre/post memory observations are not peak compiler memory.

A separately admitted full-rebuild O0 series is the next useful development
diagnostic; it needs its own correctness oracle and output hash. O0 still runs
LLVM opt/llc and links. An O2/O0 whole-command delta is a configuration comparison,
not exclusive LLVM cost. A later `--emit-llvm` run produces a serialized bundle,
not a usable program, and has different surrounding work. **Do not subtract its
time from full-build time and label the difference exclusive backend cost.**

Freeze the acceptance statistic, failure policy and numerical produced-program
regression budgets before selecting a backend. No merge, release, platform
acceptance or default-toolchain switch follows from this baseline.

## Later metadata-dependency qualification

A subsequent diagnostic review found an omitted controller dependency:
Python's `platform.platform()` can execute `/usr/bin/uname -p` to populate the
receipt's host string. This occurs before the compiler timer starts. That string
is receipt metadata only; it is not used in the compiler arguments, environment
or source inputs. The helper was **not pinned by this campaign**, so its exact
historical bytes cannot be certified from these receipts. The declared source,
compiler/backend and runtime pins and raw-output checks still passed.

Keep the observed timings and original receipts unchanged, with this gap visible.
Do not backdate a newly measured helper hash or replace the samples. Future
diagnostics must include this helper and its already-covered loader/libc
dependencies in their prospective controller closure before launch. Independent review
found no other newly discovered unpinned input path affecting compiler inputs
or timing within its scope. This qualification limits reproducibility evidence;
it does not turn the metadata helper into measured compiler work.

## Evidence and reproduction limits

The [derived result record](../tools/experiments/vnext/evidence/current-o2-20261104.json)
contains exact timings, source/compiler identities and hashes of retained original
receipts, configurations, source/tool manifests, controller and controls. It is
a selected public summary, not the raw receipts or a runnable configuration.
Machine-local paths and host/process details are omitted. Original hashes name
original bytes, not this derived record; the record alone is not a full replay
bundle. The [baseline harness](../tools/experiments/vnext/README.md) describes
how to prepare a new pinned run. New paths or tool bytes require fresh admission.
The separate serial controller adds outer ownership, source/tool/mode/resolver
checks, exact-output checks and stop-on-failure ordering. It is identified in the
record, not installed as a new public entry point by this documentation change.

The pinned-interpreter harness controls passed all 27 tests and the serial
controller controls passed all 12 tests. A successful source hash or exit alone
is insufficient to admit another workload. No compiler implementation, default,
bootstrap pin, CI gate or release changed in this checkpoint.

A later [single source-to-LLVM-bundle diagnostic](compiler-current-emit-diagnostic.md)
records 16.409 seconds to emit 92.76 MB of text. It is a different output and
measurement boundary, not an exclusive phase breakdown of these full builds.
