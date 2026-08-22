# ADR 0016: execution state belongs to activations

Status: Accepted
Date: 2026-08-22

## Context

`node` stores the authored definition of a graph node: its descriptor, identity,
pins, defaults, edges, and editor metadata. Evaluation also needs temporary pin
values, memoization, cycle detection, and diagnostics. Those values were stored
on the shared node, so two activations of one Blueprint could overwrite each
other and stopping one activation could reset another.

`flow_run` already owns continuation frames, parameters, watches, scratch, and
errors. `eval_context` already owns pull-evaluation status and data-edge
observation. These are the existing seams for execution-local state.

## Decision

Runtime pin values, computed and visiting flags, and evaluation diagnostics live
in a runtime store owned by one `flow_run` or one pull `eval_context`. The graph
and its nodes retain authored data only. Nested pure-function graph evaluation
gets a separate store while sharing the parent instance for variables and owner
handles.

The first store implementation is a simple per-node sidecar. Evaluator and flow
code access it through internal runtime accessors. A later dense layout may
replace the sidecar backend without changing node descriptors, graph JSON, or
flow callback identity.

Runtime stores are not serialized and do not change authored topology revisions.
Unconnected flow inputs read `pin_spec.default_value`; callers set authored
defaults through graph mutation APIs.

## Consequences

Two activations can evaluate the same graph node with different values. Memo and
cycle state no longer crosses activation or pull-evaluation contexts. Watches
continue to expose copied activation values, and the editor's pure preview reads
the preview context instead of graph nodes.

Runtime slots borrow pointer-bearing values from their execution owner. A flow
run keeps its arena for the run lifetime and retains completed nested function
runs until the parent stops or resets, so returned values cannot point into a
freed callee. Watches copy strings, names, arrays, and structs into run-owned
watch storage. External code that accessed the removed runtime fields on `node`
must use flow or evaluation accessors.

`eval_context_reset_runtime()` resets one context's store. The old
`graph_eval_reset()` symbol remains only as a deprecated compatibility symbol,
since a graph cannot identify which context should be reset.

The store currently uses a straightforward lookup representation. Dense offsets
are deferred until a measurement shows that the lookup costs matter.

## Alternatives rejected

### Per-activation shadow graphs

Cloning nodes would preserve direct field writes in callbacks, but would create a
second node identity for every activation. Sink callbacks, watches, editor
selection, and nested graph references would need mapping between authored and
runtime nodes.

### Dense runtime layout immediately

A dense layout may reduce lookup cost, but it would combine the ownership fix
with offset construction and invalidation rules. The sidecar establishes the
behavioral contract first.

### Lua or another scripting VM

The runtime still executes compiled C node callbacks and retains typed pins,
graph editing, serialization, and headless validation. A scripting VM would add
a language bridge and lifecycle surface without removing the need for
activation-local state.
