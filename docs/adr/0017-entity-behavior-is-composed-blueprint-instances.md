# ADR 0017: Entity behavior is composed Blueprint instances

Status: Accepted
Date: 2026-08-22

## Context

ADR 0011 established that a `blueprint_instance` is persistent runtime behavior
state for one shared definition, and deliberately left open what a consumer
attaches an instance to. `docs/spec.md` lists an ECS binding as a deferred,
optional module layered on the node descriptor and reflection APIs.

A game is now the consumer. It needs a rule for what owns behavior, what owns
data, and how a designer composes an enemy out of parts. Two prior attempts
(`Neon-Swarm`, `GeometryWars`) put all behavior in hand-written C systems over
an ECS, which performs well and requires editing C for every behavior change.

The alternative reading of this runtime is that a graph is a system: one
instance per behavior kind, iterating a query in its body. That model is
cheaper per frame, and it cannot express a per-entity latent node, because one
activation owns the whole batch and suspending it suspends every entity.
Per-entity suspension is the capability that distinguishes this runtime from a
visual ECS, so it decides the model.

## Decision

An entity MAY own several Blueprint instances, one per graph asset its
prototype attaches. Instances on one entity are independent runtime state
sharing only that entity's components.

Behavior that lives in time uses per-entity instances. Uniform per-frame
arithmetic over many entities stays a C system or a single batch graph; the
dividing line is whether the behavior needs a continuation.

Per-entity durable state is a component. A Blueprint instance's variables are
private to that instance and transient: they hold behavior-local state such as
a phase index or a burst counter. State that a second graph, a C system, the
renderer, or serialization must observe is a component. The editor offers
promoting an instance variable to a component, mirroring `VAR-10`'s promotion
of a pin to a variable.

The game lives in-tree as modules with external-consumer discipline: `ecs/` for
entity storage, `engine/` for rendering, input, and host surfaces, and `game/`
for annotated components, node packs, and prototype data. No file under `core/`
includes a header from any of them, per `SCOPE-1`. `ecs/` is testable headlessly
with no window, per `SCOPE-2`. The game's reflection pack is composed exactly
the way an external consumer's would be, so extraction to a separate repository
later is a directory move plus CMake export work.

## Consequences

An entity's behavior is a list of graph assets in its prototype, so a designer
assembles a new enemy by composing reusable behaviors rather than by editing one
graph per enemy type.

Instance count is entities times attached graphs rather than entities, so
instance creation and destruction become hot paths on spawn and on apply.
Pooled instance storage is load-bearing rather than an optimization.

Dispatch is per event handler, not per instance. A graph with no `Tick` event
costs nothing on a Tick pump, so most composed behaviors are reactive and free
between events. A suspended latent activation likewise costs nothing until its
wake time, which makes latent authoring cheaper than a polling `Tick`.

Two graphs on one entity communicate through that entity's components. Neither
can read the other's variables, which keeps `RUN-18`'s isolation guarantee
intact and keeps every shared value nameable by the effect declarations in
ADR 0019.

Recreating an instance re-runs its `Begin`, which is a defined recovery path.
This is what makes the per-definition teardown in ADR 0020 tolerable, and it
depends on instance variables genuinely being transient.

The debugger keeps its meaning. A breakpoint pauses one entity's activation and
the call stack, watches, and traversal records all describe that entity.

## Alternatives rejected

### Graph as system, one instance per behavior kind

One activation per behavior per frame is markedly cheaper and iterates
contiguous component arrays. It cannot suspend one entity without suspending the
batch, so `Delay`, retriggerable delays, and any windup-then-act sequence revert
to a component timer plus a hand-rolled state enum. It also degrades the
debugger, since a breakpoint in the loop body pauses the batch at an arbitrary
iteration, and leaves the delegate work in `EVT-13` through `EVT-18` unused,
since there are no per-entity instances to bind. This model remains available
for uniform per-frame passes, where it is the better choice.

### One instance per entity

Simplest scheduler and simplest debugging, at the cost of a monolithic graph per
enemy type and no reuse below the graph level. It also interacts badly with
ADR 0020: any authored edit resets that entity's entire behavior state rather
than one behavior's.

### Instance variables shared across the graphs on an entity

This removes the need to declare a component for shared state, and creates a
second per-entity typed store with worse properties than the first. C systems
cannot see it, so the renderer and physics cannot read values the graphs treat
as entity state. It breaks `RUN-18`, which guarantees two instances share no
variable state, and that guarantee is what makes ADR 0019's conflict analysis
sound. It has no id space, so a declared effect cannot name it. And it creates
attach-order dependencies, since a graph reading a variable another graph
declares no longer compiles standalone.
