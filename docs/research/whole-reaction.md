# Whole visual reactions

Trigger Visual Reaction now expands into the shared behavior IR. SLJIT emits
finite checks, checked entity/component access and five float stores. The
ordinary body has no runtime helper calls, allocation or node dispatch.
Bytecode and generated C execute the same instructions. The oracle callback
remains available for explicit parity/debug execution.

This adds ArenaEnemyVisualBehavior.Contact Begin, Damaged and its Bump function.
The [catalog audit](whole-reaction-audit.txt) now reports 31/33 events, 27/31
effectful local functions, 39/39 pure local functions and 4/4 library functions
across 19 behavior assets. Arena's end-of-run live snapshot rises from 20/22 to
22/22 entries. That snapshot omits retired and unused assets and does not prove
full Scene or language coverage.

## Compiler and scheduling contract

`node_desc.lower` is a publication-only expansion hook. Runtime capabilities can
emit the compiler's IR while core stays independent of their component types.
The hook is never called by behavior execution. Synchronous flow expansions
emit their effects and continue through Then; data expansions return a typed
slot. Unsupported signatures produce source diagnostics. The visual reaction
expansion lives beside the runtime capability in `visual_motion_lower.c`.

The operation evaluates Rotation, Lift and Target in their original order.
Validation then checks a present rotation and finite lift, requires the world,
resolves the live target component, and validates rotation fields and reaction
duration. Only after every check succeeds does it store rotation, lift and zero
reaction age. The new argument-failure classification preserves FLOW_ERR_ARGUMENT
alongside the existing node/state errors, source node, pin and call stack.
Component access retains the compiler's generation, presence and layout checks.

The descriptor now declares its GameVisualMotion read and write. Target can name
another instance's entity, so the old entity-scoped flag was not a sufficient
promise of independent writes. Scheduling remains serial unless analysis proves
disjoint targets. The lowered program also records the component write so query
reuse cannot pass across it incorrectly. Custom expansions receive instrumented
variants for breakpoints, stepping, watches and source traversal. Ordinary code
removes those callbacks.

## Measurements

[Five optimized trials](whole-reaction-results.json) rotate three backends over
500,000 invocations after 10,000 warmup calls. The fixture invokes the operation
through a nested behavior function, then writes a completion variable. Timing
excludes compilation and activation setup. Generated C uses Clang 23 with
`-O3 -ffp-contract=off` and the same context ABI.

| Component access | SLJIT ns/call | Generated C ns/call | Bytecode ns/call | SLJIT/C |
| --- | ---: | ---: | ---: | ---: |
| Resolve column at execution | 11.786 | 8.438 | 251.092 | 1.397 |
| Supply a resolved column | 10.994 | 7.928 | 251.056 | 1.387 |

Both meet the advisory 1.5x native/C target. SLJIT is 21.3–22.8x faster than
bytecode on these small bodies. Warm execution allocates no VM scratch; the
body contains no allocating operations. Supplied columns still require live
entity generation, component presence and layout checks. This is an equivalent
generated-C comparison, not a comparison with a whole handwritten Scene.

[Seven paired Arena runs](whole-reaction-scene-results.json) alternate saved
executables with profiling disabled. They exercise 180 fixed steps with firing
and restart and discard 30 warmup frames. The table reports medians of each
run's percentile measurements in milliseconds.

| Backend | Behavior p50 before/after | Behavior p95 before/after | Simulation p50 before/after | Simulation p95 before/after |
| --- | --- | --- | --- | --- |
| SLJIT | 0.008 / 0.008 | 0.022 / 0.022 | 0.014 / 0.013 | 0.030 / 0.028 |
| Bytecode | 0.018 / 0.018 | 0.034 / 0.031 | 0.024 / 0.024 | 0.041 / 0.038 |

There is no measured median behavior improvement in this small workload. The
simulation and tail differences are small and affected by microsecond timer
resolution. Do not treat them as a large Scene speedup or large-crowd acceptance.

A [separate profiling pass](whole-reaction-profile.json) recorded six
ArenaEnemyVisual.Contact Begin dispatches in both executables. Its native
execution contribution fell from 0.307 to 0.060 microseconds per measured frame;
bytecode fell from 0.113 to 0.047. This single instrumented sample helps attribute
work but is weaker timing evidence than the uninstrumented pairs.

## Validation

Oracle, bytecode, SLJIT and emitted-C tests compare component state and complete
flow snapshots across explicit and default-Self targets, null rotation, NaNs,
infinities, finite extremes, signed zero, invalid durations, missing worlds and
components, stale/zero handles and overlapping failure conditions. Waiting calls
retain caller arguments while reading current component state on resume.
Tests replace duration or remove the component during a wait. They also cover
budgets, cancellation, breakpoints, stepping, input/output watches, traversals,
call stacks, untouched non-target components and scheduler effect diagnostics.
Compiled cases require whole execution, zero oracle calls and an IR without
runtime calls.

`runtime_backend_smoke` adds a separate test of actual runtime capabilities,
linked against the runtime component pack. `check-behavior-backend` runs it along
with the existing core smoke on native and Emscripten builds. This avoids
substituting a synthetic component for the operation's portable acceptance.

Native `make test`, `make build`, ASan/UBSan Arena execution and emitted-C parity
pass. `make web` builds the app and executes the actual reaction through BYTECODE
with SLJIT absent. A native no-JIT build rejects both core and runtime-capability
dispatch with a compile diagnostic and zero oracle calls. Emscripten exposed a
private helper name collision with its C library's `finite`; the lowering helper
is now named `require_finite`. Formatting, Python compilation and
`git diff --check` pass.

```sh
build/test_suite --whole-reaction
build/test_suite --emit-reaction-c /tmp/reaction.c
clang -std=c23 -O3 -ffp-contract=off -shared -fPIC -Isrc /tmp/reaction.c -o /tmp/reaction.so
build/test_suite --whole-reaction /tmp/reaction.so
python3 tools/bench_reaction.py --build-dir build-performance --output /tmp/reaction.json
```

The remaining catalog event exclusions are ArenaDrop.Begin and ArenaPickup.Tick.
Their first blockers include Primitive Part, Transform Visual Offset, Contains
and Scene Rhythm. Managed containers, remaining language operations, full
inspection coverage, scheduler/state locality, large-crowd gameplay acceptance
and runnable Scene AOT exports remain open in the
[completion criteria](behavior-completion.md).
