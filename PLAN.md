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
