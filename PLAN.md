# Performance and automatic parallel execution

Implement the previously assessed compiler and runtime opportunities, prioritizing
measured costs. Preserve authored ordering, effects, numeric semantics, ownership,
errors, debugging, hot apply, and native/web backend policy. Work starts at
5a74676f in an isolated worktree; the dirty primary checkout stays separate.

## Context and ownership

The engine already lowers the catalog into typed bodies, fuses query reductions,
embeds repeated Tick bodies, reuses managed arenas, and proves parallel spans.
Native Auto workers exist. Web threads and numeric Wasm are optional. Existing
reports identify managed copying, loop/spring code quality, runtime planning,
and material work as remaining opportunities. Re-measure before crediting gains.

Immutable program generations remain the compiler's source of truth. Runtime
bindings own per-instance state; the shared task pool owns workers. Compiled
optimizations must retain exact failure/partial effects. Unknown effects remain
ordered. Authors should receive safe optimizations without graph rewrites.

## Implementation

1. Build a clean Release baseline and capture representative kernel, managed
   Tick, parallel, and scene measurements. Keep benchmark runs sequential.
2. Improve the existing register allocator and control-flow optimization in
   src/graph/compiler/behavior_function_opt.c and native emitters. Extend safe
   invariant/redundancy removal; retain exact failure and partial-state behavior.
3. Remove avoidable deep managed copies and allocations in compiled snapshots
   and persistent storage. Retain independent ownership across waits and writes.
4. Extend query and behavior fusion/dense native loops where dependencies allow;
   consider SIMD only with exact semantics and a profitable measured workload.
5. Make supported web multithreading and profitable graph Wasm execution the
   ordinary launch path, with capability-based serial fallback. Use existing
   packaging/service-worker mechanisms; do not enable workflows or deploy.
6. Profile larger scenes and optimize additional dominant shared-runtime costs,
   including material work and scheduling, without changing visual/game rules.
7. Integrate, review, and validate all changes; record timings, limitations, and
   exact source/build/backend states in docs/research/performance-defaults.md.

## Verification and done

- Regression tests compare oracle, native, bytecode, generated C, state, budgets,
  errors, ownership, waits, hot apply, and serial/parallel snapshots.
- Focused tests after each change, then make test, native backend rejection,
  fresh web build and browser launch, and formatting/diff checks.
- Compare baseline/candidate on the same machine, assets, input, worker budget,
  and flags. Report publication/cold start separately from warmed kernels and
  complete frame distributions. Keep native and web evidence separate.
- Exercise a second independent fixture or scene for shared changes.
- Retain only optimizations supported by semantic correctness and measured
  benefit or a necessary default/capability change. Report remaining acceptance
  gaps rather than treating microkernel results as frame-rate proof.

If blocked, record evidence, attempts, alternatives, and required external input,
and complete the independent techniques first.

## Coordinated continuation

Reconcile the completed performance defaults with streamed field evaluation,
scenario/replay parity, and material border coalescing before starting the next
compiler and general runtime implementations. The common baseline includes local
main's committed Studio work; its active uncommitted work remains separate.

Root owns integration, shared build/controller/test policy, and measurement
windows. The "Implement frame-time roadmap" chat owns physics, materials,
rendering, and complete scene profiling. This chat owns behavior JIT/compiler,
query generation, managed graph execution, and numeric Wasm graph boundaries.

1. Integrate field commit 164112bb and prepared runtime/renderer commit 25ae04e7
   onto main's committed baseline 48f62f97 in the isolated coordination checkout.
2. Keep automatic worker/SIMD selection and scalar recovery. The compatibility
   app/controller contain zero SIMD. CHIBI_FIELD_SIMD=OFF disables only the field
   translation unit's SIMD in the worker build. Preserve selected sanitizer
   instrumentation and truthful backend/coverage reporting.
3. Run native sanitizer checks, field/material/backend checks across scalar and
   worker Wasm, replay/profile parity, disassembly and browser startup/fallback.
   Then integrate the validated common baseline into local main while preserving
   its uncommitted work, and give both implementation owners that exact commit.
4. Implement query predicate-chain branch fusion in a separate compiler worktree.
   Preserve single-use/entry guards, NaN unordered inversion, observable partial
   effects, source diagnostics and exact budgets. Compare native, bytecode,
   oracle and generated C; measure body and activation costs separately.
5. Begin the contact-collection implementation in the general-performance chat,
   preserving shapes, compound colliders, sensors, sweeps and event ordering.
   Require matched scene gains and a second fixture before accepting it.
6. Integrate accepted first-round changes, rerun relevant combined checks, and
   record source commits, delivery state and measurement limits. Source changes
   and builds may proceed independently; accepted timings use reserved quiet
   windows with identical assets, quality, worker budget and state parity.

Done means the common baseline is reconciled and validated, the first compiler
and general-performance slices have real implementations with scoped evidence,
and existing local work is preserved. No deployment or remote push is part of
this continuation. A blocked slice records evidence, attempts, options and the
decision required while its independent counterpart continues.
