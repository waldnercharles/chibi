# ADR 0008: The editor owns one flow_run, and preview evaluation suspends during a run

Status: Accepted
Date: 2026-08-18

## Context

`eval_view_refresh()` (editor/eval_view.c:32-46) runs once per frame from editor/editor.c:1338. It calls `graph_eval_reset(g)`, resets its arena, then calls `graph_eval_output()` for every output pin whose `kind` is `PIN_DATA`. `graph_eval_output()` writes the computed value into `n->outputs[pin]`, so the preview's side effect is the node's output storage, and `eval_view_output()` reads it back for display.

A live `flow_run` writes to the same arrays. core/flow.c drives node evaluation through `flow_context_eval_data()`, which lands values in the same `pin_value *outputs` allocated by `graph_add_node()`. Nothing separates the two writers. Continuous preview during execution overwrites whatever the run computed, so pin displays show a value derived from current literals rather than the value that flowed through the node when it executed. For a debugger that is wrong by definition: a watch answers what the program did.

Error reporting in both layers is file-static. core/flow.c holds `static flow_error g_flow_error;` returned by `graph_last_flow_error()`, and core/eval.c holds `static eval_status g_status;` returned by `graph_last_eval_status()`. Two concurrent runs, or one run plus one preview pass, overwrite each other's diagnostics. The flow runtime is also not yet reachable from the UI: no `flow_run` is constructed anywhere in editor/ or app/, so this decides how it gets wired before the wiring exists.

## Decision

The editor holds at most one `flow_run` at a time. While that run's `flow_run_status()` is anything other than `FLOW_IDLE`, `eval_view` preview is suspended: `eval_view_refresh()` is not called, and pin displays read the values the run recorded. Watch values are captured from execution, matching Blueprint debugger behavior, so a watched pin shows the value the node produced on its last activation.

The static error globals go away. `g_flow_error` in core/flow.c is replaced by per-run error state (the `error` field a `flow_run` already carries) plus an explicit dispatch-failure out-parameter for the `graph_run_event()` case that returns NULL. `g_status` in core/eval.c is replaced by error state on a per-evaluation context. A future second run, or a headless run in test/self_check.c, then does not interfere.

## Consequences

Watch values become trustworthy: what the editor shows during execution is what the run computed, so stepping and breakpoints have something correct to display. Preview cost also drops: evaluating every DATA output pin of every node every frame is the most expensive thing the editor does, and it stops while a run is active.

Removing the globals also makes the evaluators reentrant enough for a test to run two graphs in one process and read each one's error.

The work: `graph_last_flow_error()` and `graph_last_eval_status()` change shape, so every caller in core/flow.c, editor/eval_view.c, and test/self_check.c is touched. `eval_view` needs a suspended state and a value source that is the run rather than `n->outputs`, which means recording per-pin values at activation time. And the editor needs a visible transition back: when the run reaches `FLOW_COMPLETED` or `FLOW_FAILED` and is freed with `flow_run_free()`, preview resumes.

## Alternatives rejected

### Allow multiple concurrent runs now

Nothing in the design needs two simultaneous runs of one graph, and allowing them costs immediately. `pin_value *outputs` on `struct node` is single-instance storage, so concurrent runs need per-run output storage: a value table keyed by node and pin, or a copy of the graph's value arrays. The debugger question has no good answer either: with two runs live, a watched pin has two values and the UI must say which it means.

### Keep preview running during execution and display preview values as watches

This is the current behavior extended, and it is wrong in the way that matters. A watch would show the value a pin would have if evaluated now from current literals, not the value the node produced when the exec wire reached it, and the two differ precisely when a node has side effects. It would also keep both writers pointed at `n->outputs[pin]`, so which value survives depends on frame timing.
