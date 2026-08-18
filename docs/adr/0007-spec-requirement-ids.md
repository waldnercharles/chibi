# ADR 0007: Spec requirements carry stable IDs cited by tests and roadmap

Status: Accepted
Date: 2026-08-18

## Context

Features here have shipped half-built, and the missing halves went unnoticed.

Named events are one case. The data model has `graph_set_event_name()` and `graph_event_name()` in core/graph.c, a `node.event_name` field on `struct node` (core/node.h:67), serialization of the `"event"` key in core/serialize.c, and named dispatch through `graph_run_event()` in core/flow.c. The editor has none of it: `grep event_name editor/` returns nothing, and the only caller that sets a name is app/demo_graph.c, which hard-codes `"OnStart"`. A user placing an Event node in editor/editor.c cannot name it, so the whole dispatch path is unreachable from the UI.

Execution is the larger case. core/flow.c is a complete imperative runtime: `flow_run_start()`, `flow_run_resume()`, `flow_run_tick_at()`, `flow_run_status()`, `flow_run_error()`, a continuation stack, pause and resume against a host clock, and topology-change detection. No line of editor/ or app/ constructs a `flow_run`; only test/self_check.c does. The editor calls `eval_view_refresh()` and nothing else, so the flow runtime is dead code from the application's point of view.

Both gaps have the same shape. A requirement existed in someone's head, half of it got built, and no artifact recorded that the other half was outstanding. The previous design document described what the system does, and a companion document listed deferred editor features, but neither said which requirements had no implementation and no test.

## Decision

docs/spec.md numbers every normative requirement with a stable short ID. The prefix names the area and the number is a counter: `PIN-4` for a pin type rule, `EXEC-11` for an execution rule, `UI-6` for an editor interaction rule.

Roadmap acceptance criteria cite those IDs, and so do test names in test/self_check.c, inside the string passed to `check()`, so a line reads `check(..., "EXEC-11: paused run rejects graph mutation")`. A requirement with no citing test and no citing acceptance criterion is visibly unimplemented, and finding those is a grep across docs/spec.md, the roadmap, and test/self_check.c rather than a judgment call.

IDs are append-only: never renumbered, never reordered, never reused. A requirement that no longer applies is marked withdrawn in place and its number is retired.

## Consequences

There is a mechanical answer to "what is half-built": extract the ID set from docs/spec.md and subtract the IDs cited by `check()` calls and by roadmap criteria. Applied to the state above, `graph_set_event_name()` with no editor UI is a `UI-` requirement with no citing test, and the unreferenced flow runtime is a set of `EXEC-` requirements cited only in test/self_check.c.

Diagnostics also get a vocabulary: a validation error or a bug report can name the requirement it enforces or violates without either side quoting paragraphs.

The ongoing cost is discipline. Every new requirement needs an ID at write time, every new test needs the right ID in its name, and the append-only rule means docs/spec.md accumulates retired numbers and gaps. Changing what a requirement demands means withdrawing it and appending a new one, which is deliberately more friction than a silent edit.

## Alternatives rejected

### A separate status or checklist document

A checklist beside the spec has to restate each requirement to be readable, and the restatement drifts the first time either side is edited. It also has no mechanical link to the tests, so a checked box means somebody believed the item was done rather than that a test asserts it. The `graph_set_event_name()` gap is the failure a checklist produces: the data-model half looks done from outside, so the box gets ticked.

### Tracking parity in an issue tracker

An issue tracker separates the requirement text from the requirement status. What the system must do ends up in a ticket description that no test can cite and no reader of the repository sees, while docs/spec.md becomes a second, drifting copy. It also hides status from anyone reading the source, whereas a `check()` string in test/self_check.c names the requirement it proves.
