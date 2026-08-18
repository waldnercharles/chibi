# ADR 0002: Node instances own their pin arrays

Status: Accepted
Date: 2026-08-18

## Context

A `node_desc` in core/node.h carries `pin_spec *inputs` and `pin_spec *outputs`, and every descriptor in the tree points those fields at a file-static array. core/flow_nodes.c declares `sequence_outputs[]` with exactly four entries, `Then0` through `Then3`, shared by `NODE_Sequence` and `NODE_Sequence4`. `event_outputs[]` holds one `Then` pin, used by `NODE_Event`. generated/registry.c does the same for reflected Make, Break, and call descriptors.

`graph_add_node()` in core/graph.c allocates per-instance `pin_value` arrays for `n->inputs` and `n->outputs` and seeds them from the descriptor's pin types, but it never copies the `pin_spec` arrays. Every reader of pin identity goes through the descriptor: `eval_view_output()` in editor/eval_view.c tests `n->desc->outputs[pin].kind`, `pin_types_compatible()` compares two `pin_spec` pointers, and core/serialize.c validates pin indices against `desc->input_count`. So every instance of a node type shares one pin list, and a per-instance signature is impossible to express.

The one per-instance property already needed, an Event node's name, got a field on the node struct: `node.event_name` at core/node.h:67, plus `graph_set_event_name()` and `graph_event_name()` in core/graph.c and an `"event"` key in core/serialize.c. That pattern does not scale: each configurable node type adds another bespoke field, accessor pair, and serialization key.

## Decision

`graph_add_node()` copies the descriptor's `pin_spec` arrays into instance-owned storage alongside the existing `pin_value` arrays. The instance's pin list is mutable: `name`, `type`, `type_name`, `kind`, and the pin's default value change per node. `node_desc` becomes a template plus behavior (`eval`, `fn`, `flow`, `flow_kind`, `type`) and stops being the live pin list. Every reader of pin identity switches from `n->desc->inputs[i]` to the instance's copy.

`node.event_name` is removed. An Event node's name becomes one entry in the general per-instance configuration that ships with the instance-owned pin list, and `graph_set_event_name()` is expressed in those terms.

## Consequences

This is required, not merely convenient, for all of the following: Custom Events with parameters, where the parameter list is the node's output pins; user-defined function signatures and the call nodes that mirror them; variable Get and Set nodes, whose pin type is the variable's type; Sequence "Add pin", which cannot work while `sequence_outputs[]` is fixed at four; Switch on Int, Enum, and String, which need one exec output per case; Select with N option inputs; struct pin splitting, which replaces one `PIN_STRUCT` pin with one pin per `type_field`; and wildcard pins that resolve to a concrete `pin_type` on connection. None are expressible today.

It also ends the bespoke-field pattern: a configurable node type adds configuration data, not a field on `struct node`.

The costs: `graph_add_node()` gains two allocations and a copy, `graph_remove_node()` two `free()` calls. Serialization writes per-instance pin lists rather than trusting the descriptor, a schema change that lands with ADR 0003. Every `n->desc->inputs` and `n->desc->outputs` read across core/eval.c, core/flow.c, core/serialize.c, editor/editor.c, and editor/eval_view.c has to be repointed, and any missed site reads a stale shared list.

## Alternatives rejected

### Generate a descriptor variant per configuration

Emitting `Sequence3`, `Sequence5`, and `SwitchOnMyEnum` as separate static descriptors is what `NODE_Sequence4` already hints at. It fails outright on Custom Event parameters, because the parameter list is authored in the editor and no build step exists to generate a descriptor for it. It fails on wildcard pins, whose type is known only after a connection exists. It also multiplies the registry: every arity of Sequence and Select, and every enum in the program, becomes a `REGISTRY_NODES` entry.

### Keep static pins and store overrides in a side table keyed by node

A side table needs the same per-instance storage, so it saves no memory and no code, and it costs more: every pin read becomes a lookup that may or may not hit, pin identity gains a second source of truth that can disagree with the first, and the table has to be kept in sync through `graph_remove_node()`, `graph_save_nodes_json()`, and `graph_append_json()`. Pins on the instance keep identity in one place with the values it describes.
