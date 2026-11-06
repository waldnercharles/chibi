#ifndef CHIBI_GRAPH_SIGNATURE_H
#define CHIBI_GRAPH_SIGNATURE_H

#include "graph/model/behavior_asset.h"

/* The signature of a function or macro: its inputs are the outputs of its
 * Entry node and its outputs the inputs of Return, addressed by pin index
 * (`result` picks the side). A function's are data pins; a macro's may also
 * be execution pins (Unreal's tunnels). Every edit updates the graph's calls
 * in its asset: a function's through graph_library_refresh_call, a macro's
 * pin for pin, since its calls mirror its tunnels. A call wire that a removed
 * or retyped pin can no longer carry is removed, as Unreal's node refresh
 * does. Edits return 0, or -1 when refused; a refused edit changes nothing. */
bool graph_signature_pure(const graph *function);
/* A pin that declares an input or output: data or, in a macro, execution;
 * not a field or split part. */
bool graph_signature_declared(const graph *g, const pin_spec *pin);
/* A pure function has no execution pins. Making one pure removes them from
 * its calls and joins each call's incoming execution to its outgoing wire; it
 * is refused while the body has nodes with execution pins. Functions of
 * assets that compute values only (behavior_domain_pure) stay pure. */
int graph_signature_set_pure(behavior_asset *asset, graph *function, bool pure);
/* `pin` gives the kind, name, type, type_name and container. Names are unique
 * among the pins of a side. */
int graph_signature_add(behavior_asset *asset, graph *g, bool result, const pin_spec *pin);
int graph_signature_remove(behavior_asset *asset, graph *g, bool result, int index);
int graph_signature_rename(behavior_asset *asset, graph *g, bool result, int index, const char *name);
/* Moves the pin at `from` to `to`, the index of another pin of the side, as
 * dnd-kit's arrayMove does; calls keep their wires, bindings and defaults.
 * Every Return of a graph gives its results in this order, so the others
 * follow the primary one. */
int graph_signature_move(behavior_asset *asset, graph *g, bool result, int from, int to);
/* Takes the kind, type, type_name and container of `type`. */
int graph_signature_retype(behavior_asset *asset, graph *g, bool result, int index, const pin_spec *type);
/* A data input's default, which unwired call pins show. Calls still holding
 * the old default take the new one; values set on a call stay (Unreal). */
int graph_signature_set_default(behavior_asset *asset, graph *g, int index, const pin_value *value);
/* The Entry pin that declares a call's input, or NULL for other nodes and
 * calls into other assets. */
const pin_spec *graph_signature_call_input(const behavior_asset *asset, const node *call, int input);

#endif
