---
name: event-graph
description: >-
  Author or extend gameplay in this project's Event Graph and reflected Component model. Use for Behavior Assets, Prototypes, nodes, pins, or events.
---

# Event Graph

Use the canonical terms in `CONTEXT.md`.

## Model

- A **Prototype** defines Entity composition and Component defaults.
- A **Behavior Asset** defines reusable Graphs and authored variable defaults.
- Each attachment creates an independent **Behavior Instance**; its variables are private runtime state.
- **Components** hold durable, inspectable state shared across behaviors, systems, rendering, and persistence.
- Event parameters exist only for one activation.

## Semantics

- Execution wires order mutations and side effects; data wires supply values.
- `Get_<Component>` and `Has_<Component>` are pure, demand-driven reads. Each consuming execution step sees current state; store a variable to preserve an earlier snapshot.
- `Set_<Component>`, `Add_<Component>`, and `Remove_<Component>` mutate state and have execution pins.
- Pins include scalar/container type and, where applicable, nominal type. Use registered conversions between representations only when necessary.
- An Event runs only when a real host or graph dispatch path supplies its name and parameter shape.
- The working asset and runtime snapshot are separate; apply and restart explicitly to exercise authored changes.

## Boundary

Keep gameplay policy in Behavior Assets. C should expose small, composable mechanisms rather than feature-sized nodes. Search the registry before adding a primitive, and author through the Behavior Editor or `editor_command_apply()` instead of treating serialized JSON as an untyped graph language.

When the existing vocabulary is insufficient, read
[Extending the graph](references/extending-the-graph.md).
