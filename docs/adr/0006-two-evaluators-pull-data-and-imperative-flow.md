# ADR 0006: Two evaluators, pull-based data and imperative flow

Status: Accepted
Date: 2026-08-18

## Context

A blueprint graph carries two kinds of edge, named in core/graph.h: `GRAPH_EDGE_DATA` and `GRAPH_EDGE_EXEC`. `pin_spec.kind` marks each pin `PIN_DATA` or `PIN_EXEC` (core/pin.h), and `graph_connect_data()` and `graph_connect_exec()` are separate entry points, the second taking an explicit `exec_order`. A DATA edge says where a value comes from; an EXEC edge says what happens next.

Data evaluation lives in core/eval.c. `graph_eval_output()` recurses into a node's inputs, and the two bits on `struct node`, `computed` and `visiting` (core/node.h), give memoization and a cycle guard: a node computes at most once per activation, and re-entering a node still on the stack raises `"cycle detected"`. Only requested outputs get computed, which is what lets `eval_view_refresh()` walk every DATA output pin every frame.

Imperative execution lives in core/flow.c. A `flow_run` holds `flow_frame *frames`, an explicit continuation stack whose entries are `FLOW_FRAME_NODE` or `FLOW_FRAME_LOOP`. Because the continuation is data rather than C stack, a node can yield: `flow_delay()` in core/flow_nodes.c calls `flow_context_pause_until()`, the run reports `FLOW_PAUSED`, and the host resumes it through `flow_run_tick_at()` against the clock supplied as `flow_options.now`. Mutating the graph while paused is rejected: `graph_flow_pause()` raises `graph->flow_pause_count`, and a topology change on resume returns `FLOW_ERR_TOPOLOGY`.

Blueprints draws the same line, between pure nodes that carry no exec pins and are re-derived wherever their value is needed, and impure nodes that sit on the exec wire and run once, in order.

## Decision

Keep two evaluators.

Pull-based data evaluation (core/eval.c) memoizes per activation with a cycle guard and computes only the outputs asked for, exposing the last structured `eval_status` (code, node, message).

A separate imperative evaluator (core/flow.c) walks EXEC edges with an explicit continuation stack of `flow_frame` values, resets data memoization between node activations, supports pausing for latent nodes against a host monotonic clock, and rejects graph mutation while paused via `graph->flow_pause_count`. `graph_run_event()` dispatches one named Event node, failing on unknown and duplicate names. Hand-authored imperative nodes reach the runtime only through the `flow_context` helpers in core/flow.h.

DATA edges never participate in execution order, and EXEC edges never participate in data dependency evaluation.

## Consequences

Each evaluator gets the guarantees its edge kind needs. Pure data is re-derivable and order-independent, which is what allows memoization, partial evaluation, cycle detection as an error rather than a hang, and the editor's per-frame preview. Imperative flow is ordered and side-effecting, which is what allows `Sequence` to fan out in a defined order, `ForLoop` to iterate through a `FLOW_FRAME_LOOP` frame, and `Delay` to suspend across frames without a thread. The split also makes latent nodes cheap: pausing costs a state save.

The costs: two code paths that both know about pins, two error reporting mechanisms, and a rule to enforce at every connection site about which evaluator an edge belongs to. `flow_context_eval_data()` is the seam, and it has to reset memoization at the right granularity, per node activation, or an impure node reads a value derived before an earlier side effect.

## Alternatives rejected

### A single push-based evaluator

Pushing values forward from sources computes every output whether anyone reads it or not, which makes partial evaluation impossible. It also has no natural place for ordering: a push evaluator invents an order for side-effecting nodes, and the order it invents is the traversal order rather than the one the author drew with `exec_order`. Cycles stop being a detectable error and become an infinite propagation loop.

### Compile the graph to a bytecode VM

A VM is a performance choice, not a correctness one, and dirty tracking is itself parked until profiling shows full recompute is the bottleneck. Compiling now adds a step between every edit and every run, and it makes the debugger harder because breakpoints and watched pins have to map back from bytecode to nodes. This stays open as a later optimization behind the same two-evaluator semantics.
