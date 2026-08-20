# ADR 0011: Persistent Blueprint instances own behavior state

Status: Accepted  
Date: 2026-08-19

## Context

The current runtime treats one `flow_run` as the complete lifetime of behavior.
`VAR-8` places variable storage on the run, and `EXEC-13` plans to place
DoOnce, Gate, MultiGate, and FlipFlop scratch there as well. Starting a later
event therefore resets both kinds of state.

That model can execute isolated graphs, but it cannot model persistent Event
Graph behavior. A Tick event cannot observe a value written by Begin unless the
host stores it elsewhere. DoOnce fires again for every dispatched event. A
Delay also occupies the only continuation owner, so a second event has no
instance-level scheduler to enter while the first activation is suspended.

The runtime must remain consumer-agnostic. A consumer may associate behavior
with an ECS entity, game session, quest, UI screen, or test fixture. Core must
not include or require any of those object models.

## Decision

An authored Blueprint definition is shared by persistent
`blueprint_instance` values.

A Blueprint instance owns:

- Its private variable storage, initialized from authored defaults.
- Instance-persistent per-node scratch used by stateful flow nodes.
- An optional owner value whose nominal `PIN_HANDLE` type is declared by the
  definition and exposed through Self.
- Borrowed runtime service bindings that event activations snapshot at
  dispatch.
- The ready and suspended event activations that currently belong to it.

An event activation owns:

- Its continuation and call stacks.
- Its temporary arena, data memo, and cycle guard.
- Its event parameters, error, step count, watch values, and latent state.

Several activations may coexist on one instance. The instance scheduler
executes one activation at a time in deterministic order. A suspended
activation does not prevent another event from starting. Activations share
instance variables and node scratch but do not share temporary values,
continuations, errors, or step counts.

Core assigns no lifecycle meaning to event names. Hosts dispatch generic named
events with typed parameters. Begin, Tick, and Input are demo conventions, not
core entry types.

Every execution belongs to an instance. Core does not keep a second stateless
run mode. Headless tests and editor preview create instances explicitly.

Freeing an instance cancels every activation and releases all instance and
activation storage. The authored definition becomes mutable after its last
attached instance is freed or detached. Runtime state is never serialized.

## Consequences

Persistent behavior no longer requires a host to mirror private graph state.
Begin, Tick, Input, custom events, and latent continuations operate over one
coherent variable and stateful-node store.

Two instances of the same definition remain independent. They may use distinct
owner handles and service instances, and a failure in one activation does not
become another activation's error.

The authored graph is shared while instances exist. Structural authoring edits
must stop or detach every affected instance before replacing node or pin
identities. The editor may still present one preview instance, but it no longer
owns exactly one `flow_run` as its full runtime model.

The spec reconciliation is complete. `VAR-8`, `EXEC-13`, `RUN-11`, and the
other one-run ownership and mutation requirements remain marked WITHDRAWN under
ADR 0007, with append-only replacements. ADRs 0001, 0006, and 0008 identify the
parts superseded by this decision.

The decision does not add Blueprint classes, inheritance, components, prefabs,
or a UObject substitute. A Blueprint instance is runtime state for one shared
definition. Mapping it to a host object remains consumer policy.

## Alternatives rejected

### One independent run per event

This preserves the current API but resets variables and stateful nodes between
events. The host then has to move behavior state into its own object model,
which makes graph variables misleading and prevents consumer-independent
semantics.

### One active run per instance

This preserves state but lets a Delay block every later event for the same
instance. It does not match event-driven behavior where separate latent
activations coexist.

### Put instance semantics in an ECS adapter

That would make persistence depend on one consumer. The same language problem
exists for a UI screen, quest, or headless test, so the instance belongs in
core while host identity remains opaque.
