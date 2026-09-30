# Behavior JIT helper audit

Audited September 11, 2026 against `6dd0b0fd` on
`cwaldner/compiled-behavior-paths`. This is an implementation audit and a proposed
performance contract. It does not claim measured speedups or implement the next
compiler stage.

Follow-up: the first numeric-region implementation and release measurements are
recorded in [behavior-numeric-performance.md](behavior-numeric-performance.md).
The audit below remains the assessment of `6dd0b0fd`.

## Decision

Compile ordinary behavior computation and control flow into native instructions.
Retain calls where optimized native C would also reasonably call a substantial
runtime service or library routine. A helper being written in C, already tested,
or shared with bytecode is not evidence that calling it is faster.

The current implementation removes recursive data-edge discovery, but remains
dominated structurally by generic value helpers and node-level C scheduling.
`SLJIT` identifies the entry backend; it does not establish native-quality code.
Zero oracle calls is a correctness/routing check, not a performance acceptance
criterion. See [native emission](../../src/graph/compiler/behavior_jit.c),
[data helpers](../../src/graph/runtime/behavior_data.c), and
[activation scheduling](../../src/graph/runtime/flow.c).

## Disposition by operation

| Operation | Decision | Required implementation / reason |
| --- | --- | --- |
| Constants, reroutes, copies, scalar defaults | Lower directly or eliminate | Fold constants and aliases; do not call a node evaluator or copy tagged values for every edge. |
| Integer/float arithmetic, comparisons, Boolean/bit operations, select, min/max/clamp, ordinary conversions | Lower directly | Preserve exact width, signedness, overflow/error rules, NaNs, signed zero and rounding. Lower division checks where semantics require them. |
| Vector arithmetic, dot products, fixed-size transforms | Lower directly | Scalarize first, then use SIMD where it helps. Aggregate type is not a reason to call C. |
| Fixed-layout struct make/break and field projections | Lower directly | Compile field offsets, keep fields in registers or fixed frame slots; avoid constructing temporary heap objects. |
| Persistent scalar/fixed-layout variables, event arguments, delta time, Self | Lower directly | Resolve variable indices and layouts at publication; use typed loads/stores and argument slots. Keep values observable across effects and reentrant calls. |
| Branch, Sequence, ForLoop, WhileLoop, Break | Lower directly | Labels and jumps within a compiled function; loop backedges carry budget/cancellation checks. No scheduler return per ordinary node. |
| Local/library graph functions and macros | Lower directly | Resolve callees at publication; inline small profitable callees or call generated code with a defined internal ABI. Use reusable typed frames. Preserve call-depth semantics. |
| Memo state | Usually eliminate; otherwise inline | Static scheduling should handle ordinary dependencies. Preserve pull-evaluation epochs and effectful-read order; do not hoist random/component reads across writes. |
| Array length, emptiness, last index, valid index, scalar Get | Lower directly | Inline descriptor loads, bounds checks and element loads, preserving current empty/invalid behavior. Managed element ownership may require a separate slow path. |
| Array searches/equality on fixed scalar elements | Lower directly by default | Generate typed loops. Benchmark large bulk searches against specialized native routines; generic per-element tagged dispatch is not justified. |
| Array construction/mutation and managed struct/string values | Split | Generate known layout operations; retain allocation, growth and deep-copy slow paths. Prove ownership before removing clones or snapshot copies. |
| Component/entity access | Split | Resolve layout and component IDs ahead of execution; acquire/validate entity/component access through a narrow service when needed, then generate field loads/stores. Preserve write tracking and access rules. |
| RNG | Split, with a semantic constraint | Tiny explicit-state PRNG updates are suitable for direct lowering. Current `rand()` state/order must be preserved until all backends migrate together. Random Range also uses `long double`; preserve that behavior or explicitly change the numeric contract before specializing it. |
| Delays, yielding calls, host waits | Split | Generated code saves live values and a resume PC, then calls the scheduler to register the wait. Generate resumed computation. Ordinary loop iterations should not become host waits. |
| Cancellation and step budgets | Split | Inline hot checks at required safe points; retain cancellation propagation, request detachment and destruction as runtime work. Preserve node-based budgets even when nodes are fused. |
| Breakpoints, stepping, watches, traversal heat | Split | Emit checks/records only in instrumented code. Materialize inspectable values at safe points. Keep debugger formatting/storage out of release computation. |
| Spawn/despawn, structural ECS changes, event queues, physics, rendering, input/OS services | Retain service calls | These operations own shared engine state or invoke substantial native libraries. Compile argument preparation and continuation; avoid generic node wrappers. Batching may reduce cost if ordering permits. |
| Allocation growth, deallocation, deep managed cleanup, error formatting | Retain slow-path calls | Centralize uncommon or substantial runtime work and keep generated code compact. Small common allocation fast paths can still be inlined after measurement. |
| Transcendental math and large memory operations | Retain specialized native calls provisionally | Candidate benefit is mature optimized implementation and smaller code. Use typed calls to the routine, not tagged node dispatch. Compare with semantics-equivalent emitted code if these dominate profiles. |

The last three rows are justified runtime/service seams or performance
candidates, not measurements proving C helpers win. None of the current
behavior helpers has a measured helper-versus-inline performance justification
in this audit.

## Concrete problems in the current implementation

1. `behavior_jit_data()` emits calls to `behavior_data_instruction()` for binding,
   slot reads and memo checks. Even its four directly emitted float operators
   call `behavior_data_scalar()` to locate slots. The latter does runtime-node
   lookup, returns input/output pointers and sets the memo flag. The arithmetic
   operation therefore does not establish an efficient arithmetic chain.
2. `behavior_jit_control()` emits a host callback for every control record except
   Branch. Branch itself calls C for its condition and continuation. In
   `flow_run_activate()`, loop state, frame pushes/pops and per-node memo reset
   remain C work. Replacing only the arithmetic helper will leave this cost.
3. `behavior_instance_get_variable()` and `behavior_instance_set_variable()` scan
   asset variables by ID. Publication already has enough information to resolve
   an index. See [instance.c](../../src/graph/runtime/instance.c).
4. `behavior_data_call()` finds a graph and creates/frees a runtime store for a
   pure graph call. Flow calls create child runs through `flow_context_call()`.
   Synchronous calls should not require a full scheduler activation; yielding
   calls need persistent frames, but can still use pooled contiguous storage.
5. `eval_prepared_node()` dispatches on node kind, and the reflected call path
   still uses generic `pin_value` arguments. Compile-time types are not being
   used to remove these decisions. See [eval.c](../../src/graph/runtime/eval.c).
6. Array helpers mix cheap access and expensive ownership operations. Array Get
   copies to an arena; Array Add builds temporary items then clones the result.
   Optimizing ownership/allocation can matter more than inlining the wrapper.
   See [array_nodes.c](../../src/graph/nodes/array_nodes.c) and
   [value_array.c](../../src/graph/runtime/value_array.c).

The separate [System emitter](../../src/graph/compiler/graph_jit_sljit.c) already
contains fixed-layout component/struct and vector emission worth reusing where
semantics agree. Its bulk query assumptions must not be applied blindly to
entity-oriented, yielding Behavior Assets.

## Compiler direction

Use a typed control-flow program for a whole event/function, with explicit
loads/stores, calls, effects, branches, returns and suspension points. Lower both
native code and bytecode from it. Give temporaries registers or fixed frame
offsets; box only at APIs that need dynamic values. Begin with simple local
register allocation, constant folding and copy elimination. Preserve alias and
effect ordering before adding cross-block optimizations.

Compile synchronous regions to run until completion, suspension, a required
effect barrier, or a debugger/budget/cancellation safe point. Host calls can
return directly into generated code when they do not suspend. Spill live values
before suspension or calls that can invalidate assumptions. Never retain raw
component pointers across structural mutation, yield or reentrancy without a
validity guarantee. Keep FIFO event delivery and Begin-before-Tick ordering in
the scheduler.

SLJIT supplies registers, memory operands, branches, SIMD and calls. It also has
a lightweight register-argument calling convention for generated-to-generated
calls. Those are tools for this implementation; merely routing execution
through SLJIT does not provide the engine's typed lowering or allocation policy.
See the [official feature list](https://github.com/zherczeg/sljit) and
[primary API documentation](https://github.com/zherczeg/sljit/blob/master/sljit_src/sljitLir.h),
especially `SLJIT_ENTER_REG_ARG` and `SLJIT_CALL_REG_ARG`. Confirm each used
operation against the dependency version and target capabilities.

## Performance acceptance

The existing [activation benchmark](../../test/bench_activation.c) covers one
eight-node Branch/Get/Set/Select graph and compares pooled/unpooled activations.
It does not currently compare all backends or handwritten C. The available
`build` configuration is Debug with sanitizers enabled. Neither is sufficient
to approve a helper on performance grounds. No new timings were taken here;
historical benchmark numbers describe a different implementation.

Use optimized builds without sanitizers or inspection, warm generated code and
pools, exclude publication from steady-state timings, and report compilation
latency separately. Measure repeated runs with backend order varied. Compare:

- Current helper-based SLJIT, portable bytecode, and explicit oracle.
- Direct lowering of the same operation/region.
- Optimized handwritten C with identical inputs, ownership, effects and outputs.

Measure both isolated kernel cost and full dispatch cost so pooling/scheduling
cannot hide the emitted-code result. Use dynamic inputs and observable output
checksums. Cover scalar/vector chains, branches/loops, nested calls, scalar and
managed variables, array traversal/mutation, component reads/writes, yielding
calls and spawn workloads. For a retained service, compare direct typed calls
against the current generic wrapper, not against omitting the service.

Record ns/operation or activation, helper calls by category, allocations/bytes,
generated-code size and compile time. Then measure Arena and a second scene's
simulation frame-time distribution separately from rendering/presentation.
Keep semantic parity tests for RNG, ownership, effects, waits and debugging.

A retained performance helper needs reproducible evidence of better throughput,
latency or code-size/cache behavior, or a demonstrated absence of meaningful
regression with a stated code-size/compile-time benefit. A runtime service needs
an explicit service contract and minimal typed call overhead. Generic evaluator,
slot-lookup and per-node scheduler helpers should not qualify as the final
implementation for ordinary computation.

Implement in this order: establish the comparable benchmark; replace node-level
control dispatch and generic storage with typed compiled regions; lower scalar,
vector, fixed-layout and function operations; split container/ECS/ownership fast
and slow paths; use profiles to evaluate the remaining service calls. This is
substantial remaining compiler work, not a tuning pass over four arithmetic ops.
