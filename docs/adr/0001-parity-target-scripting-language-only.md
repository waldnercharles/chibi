# ADR 0001: Parity target is the scripting language, not the engine

Status: Accepted
Date: 2026-08-18

## Context

The repository today is a graph runtime plus an editor and nothing else. `core/` holds `pin_value` and `pin_spec` (core/pin.h), `node_desc` and `node` (core/node.h), graph storage and edges (core/graph.h), data evaluation (core/eval.c), an imperative flow runtime (core/flow.c), the six built-in descriptors registered through `FLOW_NODES` in core/flow_nodes.c, and JSON persistence (core/serialize.c). Reflected `Make_Vec3`, `Break_Vec3`, and `add_floats` descriptors arrive from generated/registry.c. Nothing in the tree models an actor, a class, an instance, or a component. `node_desc` plus `graph` is the entire object model.

Unreal Blueprints is two things wearing one name. One is a scripting language: named and custom events, variables, user-defined functions, macros, and a flow-control catalog. The other is an object model welded to UObject: Blueprint classes with inheritance, Blueprint interfaces, class default values, the Construction Script, component hierarchies, and event dispatchers bound to particular object instances. The second half is the larger of the two and the half that cannot be written without an engine underneath it, because its semantics are the engine's semantics.

The scripting half needs no engine and no window. test/self_check.c already builds graphs in code, runs `graph_eval_output()` and `graph_run_event()`, and reports results through `check()`. Every scripting feature below can be verified the same way, headless, before it is wired into editor/editor.c. ECS work was already deferred, and the agnostic boundary is now stated as `SCOPE-1` in docs/spec.md: core never includes cute.h or any ECS header.

## Decision

Target parity with the Blueprint scripting language. In scope: named events and Custom Events with parameters, variables including array, set, and map containers, user-defined functions, macros, the full flow-control catalog beyond the current Event, Branch, Sequence, Sequence4, ForLoop, and Delay in core/flow_nodes.c, a compile and validate pass over the graph, and a debugger with breakpoints and watched pins.

Out of scope for now: actors, Blueprint classes, class inheritance, Blueprint interfaces, class default values, the Construction Script, and event dispatchers bound to object instances.

ECS binding, meaning arch_ecs entities and components with Get, Set, Add, Remove, and Query nodes, lives in a separate optional module built on the public `node_desc` API, not in core/. Core gains no dependency on it.

## Consequences

Every in-scope feature is testable in test/self_check.c without a renderer, so the runtime can be finished and verified ahead of the UI. The scope has an edge: a request that needs a class, an instance, or an actor is answered by pointing at this ADR rather than by partially building an object model.

Two structural costs follow. Variables and functions need somewhere to live that is not a class, so `struct graph` in core/graph.h grows owned variable and function tables next to `nodes`, `edges`, and `strings`. And a graph that calls a user-defined function in another graph needs a reference that survives editing, which is what ADR 0003 provides.

## Alternatives rejected

### Add a Blueprint class and object model now

The object model is the engine-coupled part. Building it first means inventing a UObject substitute (identity, reflection over class members, default value inheritance, a component hierarchy, garbage collection semantics) before one scripting feature is complete, and ECS integration would want to revisit every one of those decisions. It also makes headless testing much harder, because class defaults and Construction Script behavior are only meaningful against an instantiated world.

### Bind to arch_ecs now

Binding now inverts the dependency stated by `SCOPE-1`: core would need arch_ecs entity and component types to express pin types, and the runtime would stop being engine-agnostic. It also spends effort on the plumbing between arena-owned struct values and pointer-stable ECS storage while the language those nodes are scripted from still lacks variables and functions. The optional module can be built at any time on top of `node_desc` without core changing.
