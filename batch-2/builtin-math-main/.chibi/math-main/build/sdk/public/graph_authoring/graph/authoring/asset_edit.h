#ifndef CHIBI_GRAPH_AUTHORING_ASSET_EDIT_H
#define CHIBI_GRAPH_AUTHORING_ASSET_EDIT_H

#include "graph/model/behavior_asset.h"

graph *behavior_asset_add_function(behavior_asset *asset, const char *name, int pure);

/* Convenience forms that add the call to the default event graph. */
node *behavior_asset_add_function_call_node(behavior_asset *asset, graph *function_graph);

/* Add a call node to `caller`. Both graphs must belong to `asset`. */
node *behavior_asset_add_function_call_node_to_graph(
    behavior_asset *asset,
    graph *caller,
    graph *function_graph
);

/* Node-creation helpers for the variable and owner surfaces. All mutate the
 * default event graph and are rejected while the definition is guarded. */
node *behavior_asset_add_get_node(behavior_asset *asset, uint64_t variable_id);

/* The same in any graph of the asset, such as a function's. */
node *behavior_asset_add_get_node_to_graph(behavior_asset *asset, graph *g, uint64_t variable_id);

graph *behavior_asset_add_macro(behavior_asset *asset, const char *name);

node *behavior_asset_add_macro_call_node(behavior_asset *asset, graph *macro_graph);

/* NULL when no owner type */

node *behavior_asset_add_macro_call_node_to_graph(behavior_asset *asset, graph *caller, graph *macro_graph);

node *behavior_asset_add_self_node(behavior_asset *asset);

node *behavior_asset_add_set_node(behavior_asset *asset, uint64_t variable_id);

node *behavior_asset_add_set_node_to_graph(behavior_asset *asset, graph *g, uint64_t variable_id);

/* A new variable typed as the pin, with a Get wired to an input or a Set
 * wired from an output. The Set runs after the node when the node's only
 * execution output is free, as Unreal wires it to Then; it is added last. */
behavior_variable *behavior_asset_promote_pin_to_variable(
    behavior_asset *asset,
    node *n,
    int output,
    int index,
    const char *name
);

/* Clipboard duplication shares a pure expression body. Promote shared bodies
 * to ordinary functions so source authoring retains their common definition. */
void behavior_asset_promote_shared_expressions(behavior_asset *asset);

/* Removes the variable's Get and Set nodes and clears inputs bound to it in
 * every graph, as Unreal's delete does; returns the nodes removed. */
int behavior_asset_remove_variable_uses(behavior_asset *asset, uint64_t id);

int behavior_asset_rename_graph(behavior_asset *asset, uint64_t graph_id, const char *name);

/* VAR-5: rename/retype update every referencing Get/Set node's display and
 * pin type without rewriting references. Retype invalidates edges that are no
 * longer compatible (VAR-6). Delete leaves orphaned references (VAR-7). */
int behavior_asset_rename_variable(behavior_asset *asset, uint64_t id, const char *new_name);

int behavior_asset_retype_variable(behavior_asset *asset, uint64_t id, pin_type type, const char *type_name);

int behavior_asset_set_variable_container(behavior_asset *asset, uint64_t id, pin_container container);

#endif
