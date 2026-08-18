# ADR 0003: Graph JSON v3 uses stable IDs; v1 and v2 loaders are dropped

Status: Accepted
Date: 2026-08-18

## Context

Graph JSON is at `GRAPH_SCHEMA_VERSION 2` (core/serialize.c:159). An edge is written by resolving both endpoints through the static helper `node_index()` and storing the results as the integer `"from"` and `"to"` keys. On load, an edge is rejected only if it "references an out-of-range node index". Pins are plain integer indices into the descriptor's pin arrays.

Array indices are not stable identity. `graph_remove_node()` in core/graph.c ends with a shift loop, `for (int i = idx; i < g->node_count - 1; i++) g->nodes[i] = g->nodes[i + 1];`, so deleting any node renumbers every node after it. In-memory edges survive because `struct edge` stores `node *from` and `node *to` pointers, but any index held outside the edge list silently retargets. `graph_save_nodes_json()` states the same problem in its own contract: ids are local to the subset.

Stable node IDs were previously parked, and node and pin endpoints still use array indices. This ADR reverses that deferral, because six features in the ADR 0001 scope need identity that outlives an edit. Nothing has shipped: no saved graph exists other than the repository's own NodeEditor.json, so there is no migration burden.

## Decision

Every node and every pin carries a stable 64-bit ID, allocated from a per-graph monotonic counter that is itself serialized, so a reload followed by new allocations never reuses a retired ID. `graph_add_node()` assigns the node ID and one ID per pin as it copies the descriptor's pin specs into instance storage (ADR 0002).

JSON schema version 3 references nodes and pins by ID rather than index, and `node_index()` and `subset_index()` in core/serialize.c go away. The versionless, v1, and v2 loaders are removed: `graph_load_json()` accepts version 3 only. `graph_save_nodes_json()` and `graph_append_json()` keep real IDs instead of subset-local ones, with `graph_append_json()` reassigning IDs from the destination graph's counter and remapping the appended edges.

## Consequences

Six features become implementable that indices cannot support. Breakpoints stay attached to the node the user set them on. Watched pins keep watching the same pin. Compile diagnostics keep pointing at the node that produced them. A variable Get or Set node stores a variable ID, so renaming a variable is one edit to the variable table and costs the nodes nothing. A call node in one graph references a user-defined function in another graph by ID. Undo, redo, copy, and paste round-trip through JSON while preserving selection and breakpoints, because the identity in the snapshot is the identity in the live graph.

Removing three loaders is a net deletion: the version branch at core/serialize.c:337 collapses along with the `schema` variable and its legacy data-only path.

The costs: every JSON file in the tree, including NodeEditor.json, has to be regenerated. Edge resolution on load becomes a lookup, so a hash map from ID to node replaces direct indexing. And the ID counter is part of the graph's persistent state, so failing to serialize it is a correctness bug.

## Alternatives rejected

### Keep indices and add a parallel identity scheme later

This means shipping breakpoints and watches against indices, hitting the retargeting bug the first time someone deletes a node mid-session, then carrying two endpoint representations in core/serialize.c at once and forcing every consumer of identity to know which applies. The retargeting failure is silent, the worst property a persistence bug can have.

### Version 3 with a version 2 importer

An importer is worth writing when saved data exists that someone cares about. None does. It would also be a lie in the interesting cases: it can invent IDs for nodes and pins, but it cannot recover a v2 file's per-instance pin configuration, because v2 has none (ADR 0002). Writing it means maintaining a second endpoint-resolution path forever to convert files nobody has.
