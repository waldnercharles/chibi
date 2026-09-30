# Behavior execution: current gap and implementation direction

The implemented backend and validation commands are documented in
[Compiled behavior execution](../compiled-behaviors.md). The audit below records
the state before that migration.

Source audit, September 9, 2026. This describes current behavior and proposed
work; it does not claim that Arena behaviors have been compiled to native code.

## Current execution

Arena gameplay runs through the graph interpreter in both hosts. The native
SLJIT backend exists, but the gameplay host does not call it:

- [`engine.c`](../../src/runtime/engine.c), `continue_step`, dispatches
  input/events and each attachment's Tick, then pumps behavior activations.
- [`behavior.c`](../../src/runtime/behaviors/behavior.c), `dispatch_attachment`, calls `behavior_instance_dispatch`.
- [`instance.c`](../../src/graph/runtime/instance.c), `behavior_instance_enqueue`
  and `behavior_instance_dispatch`, create/reuse a `flow_run`.
  `scheduler_pump` calls `flow_run_tick_at`. Data dependencies are
  evaluated through [`eval.c`](../../src/graph/runtime/eval.c), `eval_node`.
- [`compile.c`](../../src/graph/compiler/compile.c),
  `behavior_asset_compile`, validates graphs and computes diagnostics
  and effects. It does not generate a behavior program.
- [`system.c`](../../src/runtime/systems/system.c) alone lowers graphs into
  `graph_program` and selects SLJIT/typed/generic execution. Its public creation
  and tick APIs have no callers elsewhere in `src`. The current
  [gameplay authoring guide](../gameplay-authoring-plan.md#verified-starting-point-before-this-slice)
  explicitly identifies MovementSystem as an editor/benchmark fixture outside
  the shipped Scene.

This is unfinished integration against the original requirement. [Issue #1](https://github.com/waldnercharles/c-blueprints/issues/1)
requires native compiled Behavior Assets and portable WASM bytecode.
[Issue #8](https://github.com/waldnercharles/c-blueprints/issues/8), previously
closed with unchecked acceptance and reopened after this audit, requires that path for movement, spawning,
projectiles, targeting and damage. Commit
[`526d2e4c`](https://github.com/waldnercharles/c-blueprints/commit/526d2e4c7fb03b261f48e9d954b223762a9aa201)
introduced the synchronous vector System backend; its historical
`docs/ecs-movement-system-plan.md` explicitly deferred branches, state and
continuations. Commit
[`1ad29eb0`](https://github.com/waldnercharles/c-blueprints/commit/1ad29eb09330df7d12a057a2b19d5efeddcbfa0e)
shared program storage and exposed continuation inspection, without implementing
compiled behavior execution.

## Existing compiler boundary

[`graph_ir.h`](../../src/graph/compiler/graph_ir.h), lines 11–20, contains only
constant, copy, component Get/Set, struct Break/Make and call instructions.
There are no basic blocks, branches, loops, variable accesses or yields.
System lowering accepts ordered component reads/writes; its function lowering
accepts generated MathExpression bodies only (`system.c:512–528`).

The SLJIT memory backend accepts fixed-size single POD values and direct
adapters with at most three inputs
([`graph_jit_sljit.c`](../../src/graph/compiler/graph_jit_sljit.c):1585–1645).
Its register/SIMD path specializes the supported F32/vector movement operations.
[`jit_abi.h`](../../src/graph/compiler/jit_abi.h):28–50 reserves a resume block,
live slots and wake time; the existing lowerer does not implement those
continuations. [#36](https://github.com/waldnercharles/c-blueprints/issues/36)
addresses incorrect arithmetic callback admission within this limited System
path; it does not connect behaviors to JIT.

The current Arena assets demonstrate the missing semantics:

| Authored behavior | Required compiler support beyond the existing IR |
| --- | --- |
| ArenaShipMotionBehavior / ArenaEnemyVisualBehavior | Persistent variables, ordered field setters, ordinary function calls, branches, F32 math and random samples |
| TwinStickWeaponBehavior | Begin/Tick entry points, cooldown state, WhileLoop, library calls and deferred Spawn Prototype completion |
| EnemyBehavior | Self, entity query native call, branches and ordinary pure function calls |
| ProjectileBehavior / ProjectileHitBehavior | Contact payloads, component presence, field mutation, event sending, destruction and particle host calls |
| ArenaWaveBehavior | Arrays from queries, ForEach/ForLoop/WhileLoop, explicit-entity component access, spawning and event ordering |
| ArenaDropBehavior / ArenaPickupBehavior | Nested function activation, source-pose snapshots, managed material values and collection state |

## Implement one shared behavior compiler

Keep Behavior Assets and their existing authoring model. Reuse `graph_program`,
the typed IR and SLJIT, extending them in small stages:

1. Add basic blocks with explicit jump, conditional branch, return and yield
   terminators, plus typed persistent-variable/event-argument access. Lower pure
   data dependencies into ordered instructions at compile time. Begin with the
   memory backend; register allocation and batching can follow measurement.
2. Emit real native control flow and data operations. Native helper calls are
   appropriate for queries, allocation and host effects. Generated code must
   not call `flow_run_continue` or recursively evaluate graph nodes to execute
   its body. Numeric adapters must match concrete signatures and retain defined
   integer error behavior. Ordinary functions and libraries need the same
   lowering, including explicit call-frame lifetime.
3. Compile one program per immutable asset generation. Instances retain owner,
   variables and services; each activation retains only its live slots, resume
   block and host-wait state. Connect event dispatch to compiled entry points
   and preserve existing FIFO ordering and initialize-before-Begin behavior.
4. Lower delays and deferred host operations to explicit yields. Save live
   values before yielding and resume at the generated continuation. Cancellation,
   entity destruction and reset must cancel the host token and release captured
   arrays/strings/structs exactly once. Keep current simulation boundaries.
5. Execute the same lowered blocks with a portable bytecode backend for WASM.
   Native SLJIT is deliberately disabled for Emscripten in
   [`CMakeLists.txt`](../../CMakeLists.txt):54–60. Portable bytecode here means a
   compiled instruction stream, not the current recursive graph walk.

Default native Play should require compiled execution. During migration, an
explicit development fallback can remain available with the backend and exact
unsupported asset/graph/node/signature visible. Do not silently label a
validated graph “compiled.” Shipping acceptance must fail when an Arena event
falls back. The interpreter can remain an explicit parity/debug oracle until
replacement coverage is complete. Designers should not have to choose between
Behavior and System to obtain the correct execution backend.

This is a substantial compiler/runtime feature. A first complete compiled
event is a bounded milestone; compiling only a math island or wrapping the
interpreter in a machine-code entry point does not satisfy it.

## Native engine responsibilities

Compilation preserves authored gameplay decisions. Native ECS/runtime helpers
should own reusable operations with data locality or lifecycle requirements:

| Native responsibility | Keep authored |
| --- | --- |
| Fixed-step integration, collision and broad-phase queries | Movement intent, target choice and reaction policy |
| Entity query/filter/reduction implementation | Encounter thresholds, formation layout, spawn timing and difficulty |
| Spawn/event command queues and safe completion | Prototype choice, initial values and event orchestration |
| Particle simulation, attachment interpolation, ribbon construction and rendering | Effect parameters and the event that triggers an effect |
| Reusable numeric/vector/spring helpers when shared | Ship motion tuning, XP trajectory, visual reactions and game rules |

Avoid translating individual Arena graphs into bespoke native systems merely
to hide interpreter cost. Existing native helpers should be callable from the
compiled graph with explicit effects and ownership.

## Threading and dependencies

Gameplay currently runs serially. No production host installs the available
parallel hook, and fresh event dispatch executes synchronously before the later
parallel-capable pump. The test hook reverses iteration on one thread. Box2D's
world creation also supplies no worker callbacks
([`physics.c`](../../src/runtime/scenes/physics.c):75–81).

First compile and measure the actual workload. If coarse entity batches then
justify workers, Cute already supplies a task pool with add-task and join
operations in `cute_multithreading.h:524–556`; use a small synchronous adapter
before adding another dependency.

[Brutal's header](https://github.com/waldnercharles/brutal/blob/main/include/brutal_tpool.h)
provides a C/POSIX bounded MPMC queue, inline execution when full and a caller
that helps drain work. Its wait drains the whole pool. Source inference:
calling that wait from one of its own active jobs would wait for that job's own
completion; use outer fork/join only.
[enkiTS's C API](https://github.com/dougbinks/enkiTS/blob/master/src/TaskScheduler_c.h)
provides partitioned task sets, dependencies, pinned tasks and per-task waits;
its [implementation](https://github.com/dougbinks/enkiTS/blob/master/README.md)
is C++ with a C interface. It fits a measured need for a richer task scheduler,
but adds no graph semantics or ECS safety by itself.

Before parallel execution: partition by entity, preserving order among its
attachments; prepare queries before dispatch; use per-worker activation scratch;
exclude structural changes and managed-string interning from workers; merge
deferred commands deterministically and join before physics, rendering or reload.
Missing effect declarations must force serial execution. The current runtime
test at `behavior.c:305–318` permits missing declarations while the compiler
effect pass treats them conservatively. ECS query state, command arrays and
string intern storage are shared mutable state. A “parallel” checkbox would
expose these hazards to authors rather than solve them.

## Acceptance and existing ticket

Reopened and updated **#8**, linked to #35 and #36, with this requirement:

> Current Arena Behavior Assets still execute through flow/eval in native and
> WASM hosts. The existing SLJIT System fixture does not satisfy this issue's
> gameplay acceptance. Implement shared control/data-flow lowering with native
> SLJIT as the default and portable bytecode for WASM. Preserve authored assets,
> lifecycle, errors, debugging and hot apply. Report backend failures at their
> source node; require zero interpreter fallback for the shipped Arena events.

Required gates:

- Native JIT and portable bytecode match interpreter snapshots and event order
  for movement, firing, targeting, contacts, damage, spawn/death/XP and restart.
  Exercise real attached Scene behaviors, ordinary functions and libraries.
- Test arithmetic widths/errors, component snapshots after writes, managed-value
  ownership, nested calls/loops, waits and cancellation; #36 remains a focused
  regression rather than proof of full coverage.
- Pause/step/breakpoints use source maps and generated safe points. Debugger
  resume must not repeat effects or advance simulation time unexpectedly.
- Invalid hot apply retains the last valid program. Compatible apply preserves
  defined state; incompatible state requires restart. Keep old code/assets alive
  until activations release them, and invalidate dependency/layout/native-module
  generations together.
- Instrument backend execution counts and assert no graph-walking calls during
  native compiled acceptance. Record representative editor/native/WASM timings
  separately from correctness; include code compilation outside frame timings.

The source audit was read-only; the parent task subsequently reopened and
updated #8 with these gaps and acceptance criteria.
