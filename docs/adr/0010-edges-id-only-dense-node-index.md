# ADR 0010: Edges reference nodes by ID only; a dense id→node table gives O(1) lookup

Status: Accepted
Date: 2026-08-19

## Context

`struct edge` (core/graph.h) carries two representations of each endpoint:
pointer+index fields (`from`, `to`, `from_pin`, `to_pin`) and ID fields
(`from_id`, `from_pin_id`, `to_id`, `to_pin_id`). ADR 0003 declared IDs the
authoritative endpoints, but in memory the pointer/index fields are the live
truth: `make_edge()` in core/graph.c and the append path in core/serialize.c
compute the ID fields from the pointers, and readers split — core/graph.c and
core/serialize.c read the IDs while core/flow.c and editor/editor.c read the
pointers. `graph_move_pin()` and `graph_remove_pin()` hand-resync the pin-index
cache from the IDs, so the two mirrors can fall out of step and a stale index
silently retargets an edge.

The pointer cache exists because lookup is otherwise expensive:
`graph_node_by_id()` is a linear scan, and the editor reads edge endpoints every
frame, so removing the cache naively turns per-frame link rendering from O(E)
into O(E·N).

## Decision

`struct edge` keeps only the four ID fields plus `kind` and `exec_order`; the
pointer/index fields are deleted. `struct graph` gains a dense `node **id_table`
indexed by node ID with NULL after removal, so `graph_node_by_id()` is a
bounds-checked table read at O(1). Pin lookup stays a per-node scan
(`graph_pin_by_id`), since a node has a handful of pins.

The dense table deliberately has no generation counter or free list (the
arch_ecs pattern). ID-2 already forbids reissuing a retired ID, so a tombstoned
slot can never be mistaken for a new node, and ID-4's "retired resolves to
nothing" is simply a NULL slot. A generation counter would only matter if we
reused slots, which ID-2 rules out.

## Consequences

`make_edge()` and the serialize append path stop deriving IDs from pointers.
`graph_move_pin()` and `graph_remove_pin()` lose their edge re-sync loops: pins
keep their IDs when moved, so an ID-keyed edge resolves to the new index with no
per-edge update. The run-scoped watch table (core/flow.c) keys by node/pin ID
instead of node pointer + pin index, matching DBG-4/DBG-5.

`id_counter` is shared by nodes and pins, so node IDs are interleaved with pin
IDs and the table holds roughly (nodes + pins) pointer slots — a few KB for a
real graph. Splitting the counter would tighten it but touches ID-2 and the file
format, so it is not done.

## Alternatives rejected

### Keep the pointer cache and centralize the re-sync

A single refresh function recomputing pin indices from IDs after every mutation
keeps the hot path pointer-based but leaves two sources of truth and the bug
class that produced the hand-written re-sync in the first place.

### Adopt slot reuse with a generation counter (arch_ecs `ecs_entity`)

Bounding the ID space requires amending ID-2's monotonic-never-reuse guarantee
and changing the serialized counter, for no benefit at editor scale.
