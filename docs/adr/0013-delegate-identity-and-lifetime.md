# ADR 0013: delegate identity and lifetime

Status: accepted

## Decision

A delegate binding is identified by a monotonically allocated binding ID owned
by the receiving Blueprint instance. Its callback target is a weak
`blueprint_instance *` plus the target event ID. The delegate never owns or
retains the target. Destroying a target removes its bindings before releasing
its instance storage.

Removing a binding compares the binding ID, not function pointers or captured
payload addresses. Duplicate registration of the same delegate, target
instance, and event ID returns the existing binding. Invocation snapshots the
binding IDs in registration order before calling anything. A callback may add
or remove bindings, but those changes affect the next dispatch only. A target
that is destroyed or missing when the snapshot is consumed is skipped and the
binding is removed.

Event signatures are versioned by the target event's authored parameter pin
IDs and types. A binding whose signature no longer matches is retained as a
broken authored binding and reports one structured diagnostic; it is not
silently coerced. Delegate bindings are runtime state and are not serialized,
undoable, or shared between Blueprint instances.

## Consequences

- Dispatch is deterministic and reentrancy-safe without taking ownership of a
  target instance.
- Destroying an instance cannot leave a callback into freed memory.
- Signature edits remain visible to the compiler and editor.
- The broad `EVT-12` requirement is split into the executable requirements
  `EVT-13` through `EVT-18` below.
