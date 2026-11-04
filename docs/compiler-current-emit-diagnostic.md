# Current compiler: source-to-LLVM-bundle diagnostic

2026-11-04 UTC. One independently checked diagnostic completed in
**16.409267155 seconds**, producing **92,759,932 bytes** of LLVM text in
**61 pieces**. This is not a runnable build, exclusive frontend timing, a vNext
speed result or a direct Jai comparison.

## Exact workload

Source commit `02c86287430def964445ec90185961e20319536d`, tree
`780ba87ea9f29308f2f20a94381cdce8cc5d26ca`; prebuilt admitted compiler SHA-256
`7b17b5bfdaed144e0949afd065db723bb45c8bd55b356cb70910d6df42e6d266`.
The compiler has the earlier raw stage3/stage4 equality and 573-test evidence.
This observation did not run its generated output or repeat that suite.

The same source closure, explicit Linux x86-64 target, level 2, jobs 2 and CPU
affinity `[0,1]` were used as the [full O2 baseline](compiler-current-o2-baseline.md).
A fresh process read original source and wrote a new `program.ll` through the
raw `--emit-llvm` route. No previously prepared LLVM, AST, IR or application
object was supplied. Installed tools/runtime remained declared prebuilt inputs.
The command shape is descriptive, not an armed or portable specification:

```text
<accepted-builder> --compiler <source>/tools/compiler --core <source>/packages/core --output <fresh-output>/program.ll --llvm <llvm22-bin> --target x86_64-unknown-linux-gnu --level 2 --jobs 2 --emit-llvm
```

The compiler command had a 120-second cap plus one-second owned cleanup. Its
outer worker had a separate 180-second cap plus one-second cleanup. Outer
admission and final receipt writing are outside the owned worker interval.
A new quiet observation passed after the other native work had terminated.
Filesystem pages were warmed by verification; this was not cold storage or an
exclusive-host experiment. Host/CPU limits remain those of the O2 baseline.

The compiler timer starts immediately before process creation and ends at
observed leader exit. It includes startup, source-to-text work, publication and
the compiler's cleanup. Source/tool evidence hashing, structural output checks,
owned descendant cleanup and receipt writing are outside that timer.

## Result and verified scope

- Compiler elapsed wall time: **16.409267155 seconds**, exit 0, no timeout.
- Inner cleanup: **0.001445263 seconds**, ECHILD and restored subreaper.
- Outer worker: **20.984326996 seconds**, plus **0.001086641 seconds** cleanup;
  exit 0, no timeout, surviving child or cleanup error.
- Output SHA-256:
  `95d7009f54683c1c6e879de8307f3697d18f22502bd01352d3974611fa4289aa`.
- The framing inventory has **2,356 source-function comments** and **9,028
  textual definitions**. These are textual counts, not independently measured
  typed functions, specializations, machine instructions or emitted code bytes.
- Final independent readback matched the exact bytes and full framing inventory,
  all 4,054 source entries, 160 inherited pins, 128 runtime pins, nine loader
  search snapshots, exact configuration/command/environment and clean ownership.

The checker recognizes source-invariant target headers, piece delimiters,
definitions, attributes tails and contiguous source-function names. It does
**not** prove LLVM validity or semantic completeness: well-framed invalid LLVM
or an omitted final piece can pass that limited checker. No final expected
bundle count/hash was invented, and no artifact was run or reused for the
headline full-rebuild samples. This is one observation, not a median or tail.

The prospective runtime closure includes the metadata-only `uname` helper
omitted from the earlier O2 campaign. That omission and its limited retrospective
scope remain disclosed in the baseline note; this new pin does not repair old
receipts. The initial rejected proposal and its data-only repair are retained.

## What this boundary means

For this nonshared Linux invocation, `native_driver/Main.chibi` calls the same
Gather and `ModuleWith` route before either result. That work includes semantic
collection, lowering and textual LLVM generation. The emit route then joins
pieces, serializes the bundle and publishes it. It skips three LLVM tool-version
probes and the native continuation's separate start-module generation,
optimization, object generation and link/finalization work. Serialization and
process/I/O patterns also differ.

Therefore **do not subtract 16.409 seconds from the O2 full-build result and call
the difference exclusive LLVM/backend cost**, or call this observation an exact
frontend lower bound. It does show a substantial source-to-text path and a large
intermediate representation worth profiling. Exclusive phase counters and a
separately admitted usable O0 full build remain useful next measurements.

The [derived result record](../tools/experiments/vnext/evidence/current-emit-20261104.json)
keeps exact values, input/receipt hashes and limitations. It is a selected public
summary, not raw receipts or a runnable replay bundle; local workspace paths and
process details are omitted. Original hashes identify retained original bytes,
not this derived record. No production compiler, default, bootstrap pin, CI gate or release
changed to obtain this diagnostic.
