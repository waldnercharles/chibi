# ADR 0019: Parallel activation through declared node effects

Status: Accepted
Date: 2026-08-22

## Context

ADR 0017 gives each behaving entity its own Blueprint instances, so a frame may
dispatch thousands of independent activations. Those activations are already
isolated from one another by construction: ADR 0011 puts variables and node
scratch on the instance and everything else on the activation, and `RUN-18`
guarantees two instances share no runtime state. That isolation is most of what
parallel execution needs.

The remaining conflicts do not appear in graph structure. An activation can
write another instance's activation queue through
`blueprint_instance_dispatch_delegate` or `blueprint_instance_broadcast_event`.
A reflected node can write another entity's components, which entity-local
reasoning cannot see. `flow_context_service()` hands out a borrowed host pointer
whose thread-safety core cannot know. Graph topology says nothing about any of
these, so conflicts cannot be inferred from the graph and must be declared.

The available thread pool (`brutal_tpool`) is fork-join: enqueue and wait. That
suits bulk-synchronous phases with an explicit sync point, which is also what
ADR 0018's command buffer already requires.

## Decision

A `node_desc` carries an access declaration: the component types it reads, the
component types it writes, and flags for structural change, world reads such as
spatial queries, host sink output, and purity. Reflection carries the
declaration from the annotation, so a game module opts in without editing
`core/`. The declaration is written by the author of a node pack, not by a
designer, so `UI-1` does not apply to it.

An undeclared node is treated conservatively as writing everything and being
unsafe to run in parallel. Parallelism is opt-in per node.

The compile pass unions per-node effects into a per-entry effect mask, cached
against the topology revision that `blueprint_instance` already tracks. Two
activations are eligible to run concurrently when their masks do not conflict.
Activations on the same instance are never concurrent.

Writes are entity-scoped by default: a per-entity graph writing a component
writes its own entity's component. Two activations of one definition on
different entities, whose writes are all entity-scoped, are parallel-safe with
no further analysis. This is the common case and it covers most behavior.

Every effect that is not entity-scoped is recorded rather than performed: writes
to another entity, creates, destroys, component add and remove, delegate
dispatch, and calls to any service not declared thread-safe. Records carry a
sort key of dispatch sequence and activation ordinal and are applied
single-threaded at the sync point in key order. This is the same command buffer
ADR 0018 requires for structural change.

Core does not include a thread pool. It takes a `parallel_for` style callback
and an opaque user pointer in its options, and the host supplies the pool. This
preserves `SCOPE-1` and keeps `self_check` single-threaded.

Any enabled breakpoint forces single-threaded execution for that world.

Identical output at any thread count is an acceptance requirement, not an
aspiration. The check runs a seeded world for a fixed number of frames at one
thread and at many, and compares a per-frame world hash.

## Consequences

Parallel execution becomes a property the compile pass can report, so a designer
can be told which behaviors serialize the frame and why, naming the node and the
effect responsible.

Determinism does not depend on thread count or on completion order, because
every cross-entity effect passes through a sorted buffer rather than through
whichever worker got there first. Determinism does depend on the sort key being
stable, which makes dispatch sequence part of the runtime contract.

A node that misdeclares its writes is a data race the compile pass will approve.
The conservative default limits the blast radius to nodes that opted in, and
makes the declaration the reviewed surface.

Effects are declared per node and consumed per entry point, so adding a node to
a graph can serialize behavior that previously ran in parallel. That change is
visible at compile time rather than as a frame-time regression.

Debugging and parallelism are mutually exclusive by construction. This is
acceptable because a paused world is not a performance-sensitive one, and it
removes any need to reason about pausing eight workers at unrelated points.

Rollout is staged deliberately. The conservative default means a world runs
single-threaded until node packs are annotated, so the first playable version is
single-threaded even after the machinery exists. The machinery is built first
anyway, because the command buffer and the deferred sync point are required by
ADR 0018 for structural change regardless of threading, and because annotating a
catalog after the fact is far more work than annotating it as it is written.
Parallel dispatch is expected work, not a hypothetical: the sequence is
measure single-threaded activation cost, annotate the node packs, then enable
the pool behind the determinism check.

## Alternatives rejected

### Infer conflicts from graph structure

Structure is silent about exactly the operations that conflict: cross-instance
dispatch, host functions writing arbitrary entities, and borrowed services. An
analysis over structure alone would either be unsound or would classify almost
everything as conflicting.

### Lock components

Per-component or per-entity locks would allow immediate cross-entity writes
without a command buffer, at the cost of nondeterministic ordering between
activations and lock traffic on the hottest path. Determinism is worth more here
than immediacy, and the command buffer is required for structural change
regardless.

### A system scheduler with dependency groups

`brutal_ecs` ships one, and it would be a second scheduler competing with the
activation scheduler in ADR 0011 for ordering authority. One scheduler that
understands activations is simpler than two that must agree.

### No parallelism

Defensible until measured. The declaration and command-buffer machinery is
required by ADR 0018 for structural change in any case, so the incremental cost
of making it parallel-capable is small, and retrofitting effect declarations
onto an existing node catalog later is not.
