# ADR 0020: Editing a live world through applied snapshots

Status: Accepted
Date: 2026-08-22

## Context

`EXEC-19` rejects structural or authored-value mutation of a Blueprint
definition while any runtime instance is attached to it. `RUN-19` and `UI-56`
implement that by routing every authored mutation through one editor choke point
that destroys the affected preview instances first.

That is sound when the editor holds one preview instance. ADR 0017 puts several
instances on every behaving entity in a live world, so the same rule would tear
down thousands of instances on every keystroke in the details panel, and would
discard their state each time. The requirement is right and its current
implementation does not survive the scale.

The underlying assumption is that authored edits mutate the definition the world
is running. Removing that assumption removes the problem.

## Decision

A definition exists in two forms. The authored working copy is what the editor
mutates; it never has instances attached, so it is freely mutable and no guard
is consulted. The applied snapshot is what a live world runs; it is never
mutated while instances are attached, so `EXEC-19` is satisfied by construction
rather than by careful sequencing.

Apply is an explicit operation, and Compile is where it happens. `UI-6` already
places Compile on the toolbar and `CMPL-1` already defines compile as an explicit
operation over one blueprint that never modifies the authored graph. When a world
is live, Compile additionally destroys the instances of each changed definition,
swaps in the new snapshot, and recreates them.

Apply is scoped per definition. Editing one behavior graph tears down instances
of that graph only. An entity's components and its other attached instances are
untouched.

The snapshot is produced by the existing serialization round trip, which
`FMT-12` already guarantees is semantically identical, and entity identities are
unaffected. Node IDs are stable across save and load under `ID-1`, so
breakpoints, watches, and diagnostics map across an apply.

Instance migration, meaning carrying instance state across an apply when node
identities still match, is an optimization inside Apply. It changes no
user-facing semantics and may land whenever it is worth doing.

## Consequences

Typing in the details panel costs nothing and involves no teardown, because the
working copy has no instances. The cost of an edit is paid once, when the user
asks for it.

The bar on instance creation drops from keeping up with typing to not stuttering
on a button press, which is a far easier target. It remains a real bar, so the
pooled instance storage in ADR 0017 is load-bearing here as well as at spawn.

Applying a changed behavior loses that behavior's private instance variables and
re-runs its `Begin`. This is tolerable precisely because ADR 0017 confines
instance variables to transient behavior-local state and puts durable state in
components, which survive apply untouched. The two decisions depend on each
other: if instance variables were durable, per-definition apply would be
destructive.

A live world can drift from the authored graphs until Compile is pressed. The
editor must show that a definition has unapplied edits, or the difference between
what is displayed and what is running becomes invisible.

`EXEC-19` and `RUN-19` are retained unchanged. `UI-56`'s choke point still
exists; it now guards the applied snapshot rather than every keystroke.

C hot-reloading composes with this. A reloaded game module invalidates the
function pointers in every `node_desc`, so a reload re-resolves descriptors by
name, which graphs already record under `FMT-5`. Execution state survives,
because `EXEC-2` keeps continuations in an explicit stack rather than the C call
stack and ADR 0016 moved runtime values off nodes, so a suspended activation is
data with no live C frame to unwind.

## Alternatives rejected

### Play and Stop only

Creating the world on Play and destroying it on Stop makes the choke point
trivial, since nothing is attached while editing. It also removes live
iteration, which is most of the reason to put a viewport next to the graph
editor at all.

### Live mutation with per-keystroke teardown

The current model extended to a live world. Every authored edit destroys and
recreates thousands of instances, discarding their state, at typing latency. It
is correct and unusable.

### Hot-swap the definition in place

Patching the running definition and migrating instances is the most powerful
option and contradicts `EXEC-19` directly, so it would require withdrawing that
requirement and replacing it with a mutation-safety argument covering
mid-activation topology changes. The working-copy model gets the same practical
benefit as an optimization inside Apply, without reopening the requirement.
