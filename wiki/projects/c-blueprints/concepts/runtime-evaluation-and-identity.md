---
title: >-
  c-blueprints runtime evaluation and identity
category: concepts
tags: [c, runtime, visual-scripting, architecture]
sources: [projects/c-blueprints]
summary: >-
  c-blueprints keeps authored graph data, persistent instance state, and activation-local execution state in separate owners.
provenance:
  extracted: 0.82
  inferred: 0.15
  ambiguous: 0.03
base_confidence: 0.59
lifecycle: draft
lifecycle_changed: 2026-08-27
created: 2026-08-27T05:10:02Z
updated: 2026-08-27T05:23:15Z
---

# c-blueprints runtime evaluation and identity

The runtime has two evaluators. Data edges are pull-based: a requested output recursively evaluates its inputs, memoizes within the current evaluation context, and rejects cycles. Exec edges are imperative: a `flow_run` walks an explicit continuation stack, preserves ordering, and can suspend against a host monotonic clock. The edge kinds never participate in each other's ordering or dependency rules. (docs/adr/0006-two-evaluators-pull-data-and-imperative-flow.md:11-35; core/eval.h:16-42)

## Three ownership levels

- A Blueprint and its nodes hold authored definition data: descriptors, stable IDs, pins, defaults, edges, and editor metadata.
- A `blueprint_instance` holds private variables, instance-persistent node scratch, an optional owner handle, service bindings, and the queue of event activations.
- A `flow_run` activation holds continuation and call frames, temporary values, data memoization, cycle state, parameters, watches, errors, and latent state. Multiple activations can coexist on one instance, but the scheduler executes them one at a time in deterministic order. (docs/adr/0011-persistent-blueprint-instances.md:23-59)

Runtime pin values, computed flags, and evaluation diagnostics live in a store owned by a flow activation or pull context. The graph no longer owns this mutable state. Nested pure-function evaluation gets a separate store while sharing the parent instance for variables and the owner handle. (docs/adr/0016-activation-owned-runtime-state.md:18-33; core/runtime.h:1-31)

## Execution consequences

`Delay`, loops, and other latent behavior save a continuation as data instead of relying on a live C call stack. A suspended activation can coexist with another event on the same instance. `flow_run_activate()` resets the activation's pull memo before each synchronous node activation, while the run keeps its runtime store and arena for the run lifetime. (core/flow.c:489-598; docs/adr/0006-two-evaluators-pull-data-and-imperative-flow.md:25-35)

Stable graph, node, pin, and variable IDs are the identity boundary for serialization, editor selection, breakpoints, watches, and applied-definition replacement. Runtime stores are not serialized and do not change authored topology revisions. (docs/adr/0016-activation-owned-runtime-state.md:31-54; docs/adr/0020-editing-a-live-world-through-applied-snapshots.md:40-43)

## Design constraint

Do not put new execution state on shared nodes or graphs. A dense runtime store may replace the current sidecar representation after measurement, but it must preserve the activation-local ownership contract and the public descriptor, JSON, and callback identities. (docs/adr/0016-activation-owned-runtime-state.md:26-29,53-69)

## Related

- [[projects/c-blueprints/concepts/ecs-prototypes-and-behavior-composition]]: maps instances onto entities and components.
- [[projects/c-blueprints/concepts/live-authoring-and-runtime-targets]]: applies immutable snapshots to live behavior worlds.
- [[projects/c-blueprints/references/architecture-decisions]]: records the decisions behind the evaluator split.

## Sources

- `docs/adr/0006-two-evaluators-pull-data-and-imperative-flow.md`
- `docs/adr/0011-persistent-blueprint-instances.md`
- `docs/adr/0016-activation-owned-runtime-state.md`
- `core/eval.h`, `core/flow.c`, `core/instance.h`, `core/runtime.h`
