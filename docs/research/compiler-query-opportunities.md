# Compiler and scheduling opportunities

This audit accompanies the query, signature-unit and escape-analysis work based
on `78d155015eaa9833ae7c652176b5971359c6d805`. The findings below identify work in
the source, not measured frame-time speedups. Allocation and operation counters
should establish removed work before representative gameplay measurements.

## Query rows and memory

A relationship generator creates one execution row per matching tuple. Several
independent generators can multiply the row count. An exclusive relationship
limits targets per source; it does not prevent different sources from sharing
the same target.

Independent row state gives each tuple its own timers, state and suspended
tasks. Its storage necessarily grows with retained rows and live continuations.
Stateless synchronous generator systems stream through a reusable row header;
they do not retain a coroutine per row, but still allocate and release a temporary
activation for each admitted body. Reusing frames safely requires resetting call,
host, result, reference and arena ownership and remains a separate opportunity.
Value filters use a temporary admission bitmap to preserve evaluation before
body writes. Query cache limits bound cached
matching rows, not the separately owned state or task results.
Low retained memory does not imply zero allocation traffic: predicate evaluators,
helper calls and body activations still set up temporary storage. Measure both
peak retained bytes and allocation counts before making a gameplay speed claim.

The current native 64-bit Debug build has a 288-byte `execution_instance`
header and a 984-byte `execution_activation` header, excluding owned buffers.
For example, 80,000 retained tuples use about 22 MiB in instance headers; if all
80,000 also suspend, activation headers add about 75 MiB. State banks, payloads,
query indexes and allocator overhead are additional. Eligible streaming systems
retain neither header per tuple. These are compiler layout measurements, not a
universal memory budget; use `execution_program_memory_usage` for the actual
program. The row hash index retains its peak capacity for reuse.

Published-write change detection avoids snapshots of old component values. Its
tradeoff is revision storage and write bookkeeping, and it deliberately treats
a same-value write as a change. Mutable native row pointers still require the
normal publication call; an unreported write supplies no change-detection proof.
Each retained row stores 32 bytes per `changed` term for consumed and sampled
source/revision pairs. Shared world tracking costs eight bytes per entity slot
per tracked component and is allocated only for components that need it.

## Remaining work to evaluate

| Opportunity | Evidence and possible gain | Cost or required proof |
| --- | --- | --- |
| Reuse synchronous activations | Streaming removes retained row state but still creates a temporary activation for each body. | Reset every call, host, result, reference and arena owner; preserve error, inspection and continuation behavior. |
| Cache reactive binding order | `execution/program_bindings.c` reconstructs instance/binding work and topological order during maintenance. Stable dependency graphs need not be rebuilt. | Invalidate for membership, module generation, subscription targets and field dependencies; retain graph storage. |
| Prepare independent scheduler batches | `runtime/engine.c` disables source preparation when a native system shares the phase. `runtime/behaviors/source.c` combines source parallel eligibility across a whole cohort. | Prepare after preceding dependencies have committed; preserve observer/debug behavior, structural barriers and deterministic failure ordering. |
| Retain unchanged schedules and scratch | Authored synchronization re-registers systems and dirties the schedule. `runtime/systems/scheduler.c` rebuilds edges and allocates batch scratch. | Compare immutable module generation plus phase/order/effects, not just program pointers; retained capacity consumes idle memory. |
| Narrow compilation invalidation | Declaration discovery still reparses sources. Catalog equality includes presentation metadata and acts as a global reuse gate. | Split semantic dependencies from presentation metadata; retain syntax with explicit ownership and account for memory. |
| Extend collection borrowing | `execution/opt.c` rejects borrowing when it encounters broad classes of calls and writes. Per-value escape and mutation information could permit more cases. | Prove no retention, no conflicting mutation and valid lifetime across every call; debugger snapshots and suspension remain ownership boundaries. |

## Language contracts that could help

Prefer inferred, verified facts over mandatory performance annotations. A `pure`
function does not by itself prove that a passed object is unretained. Callee
bodies and exact native access/lifetime contracts must establish that fact.
Likewise, writing only to `self` does not prove query rows disjoint when a root
has several target bindings.

Two future language directions merit separate design review:

- **Explicit reductions:** shared-target sums or minima could be combined safely
  in parallel rather than issuing conflicting writes. Integer overflow rules
  must be defined; floating-point combination order changes rounding and needs
  an explicit reproducibility decision.
- **Checked access contracts:** inferred read/write footprints can retain more
  precision through calls and joins. Optional checked declarations could help
  library authors state intent, but ordinary code should receive the same safe
  optimizations. Field-level independence needs alias and layout proofs, not
  just different field names.

Units in function signatures improve compile-time dimensional checking. They
are erased and do not inherently make arithmetic faster. Range, alias, lifetime
and effect invariants can justify optimizations only when every producer and
mutation path enforces them.
