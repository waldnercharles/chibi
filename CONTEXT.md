# c-blueprints domain glossary

This glossary defines the product's canonical language. It describes concepts and
relationships, not implementation details.

## Assets

**Behavior Asset** — A reusable authored definition of executable behavior. It
contains one or more named Graphs and their authored members, such as variables,
functions, and macros.

**Prototype** — An authored definition of an entity's composition and default
state. It names component types, their default values, and the Behavior Assets
attached when an entity is created.

**Graph** — A named executable behavior within a Behavior Asset. A Behavior Asset
may contain an entry graph and additional function or macro graphs.

**Component** — Durable, inspectable state owned by an entity. Components are the
shared boundary between behaviors, systems, rendering, and persistence.

Component Get/Has nodes are read-only data nodes: they have no execution pins.
Their values are read on demand for the consuming execution step, not frozen for
an entire event. Set/Add/Remove operations mutate state and retain execution pins.

## Runtime

**Entity** — A live object in a runtime world. It has a stable identity for its
lifetime and may hold components and Behavior Instances.

**Behavior Instance** — Runtime state created when a Behavior Asset is attached to
an Entity. Each instance is independent, even when multiple entities use the
same asset or one entity has multiple attached assets.

**Runtime World** — A live set of Entities, component state, Behavior Instances,
and execution state. Editor preview and game runtime are separate worlds.

**Runtime Target** — The world currently controlled by editor runtime commands,
such as Prototype Preview or Scene Runtime.

## Authoring

**Prototype Editor** — The editor surface for composing a Prototype, editing its
component defaults, and managing attached Behavior Assets.

**Behavior Editor** — The editor surface for authoring a Behavior Asset and its
Graphs.

**Inspector** — The context-sensitive editor surface for viewing and editing the
currently selected authored object or runtime entity.

**Graph Canvas** — The visual surface for placing nodes, editing authored values,
and connecting executable and data relationships in a Graph.

**Automation Command** — A deterministic authoring or runtime operation exposed
to both the human editor and test/automation clients. It must use the same
mutation and validation rules as the UI.

## Boundaries

The core graph language is independent of the ECS, game runtime, renderer, and
editor host. Reflection supplies data and operation descriptions so authored
assets and the Inspector can remain generic. Native compiled execution is the
primary performance path; portable bytecode is the WebAssembly path. The graph
model remains the shared semantic boundary between those execution backends.

“Blueprint” is legacy terminology and is not part of the current product
vocabulary.
