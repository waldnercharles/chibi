#ifndef CHIBI_COMPONENT_NODES_H
#define CHIBI_COMPONENT_NODES_H

#include "ecs/ecs.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"

/* Get, Set and Has Component: one authored node per access. The Component
 * setting holds a reflected component key and types the Value pins; Entity
 * defaults to Self. Get shows the fields it uses as exposed outputs. Set
 * writes its Value, or only the field inputs it shows. Lowering replaces each
 * node with the generated per-component descriptors, which remain the only
 * nodes evaluation, compilation and debugging see. */
typedef enum
{
    COMPONENT_NODE_NONE = 0,
    COMPONENT_NODE_GET,
    COMPONENT_NODE_SET,
    COMPONENT_NODE_HAS,
} component_node_kind;

extern node_desc NODE_GetComponent, NODE_SetComponent, NODE_HasComponent;
extern const registry_pack CORE_COMPONENT_PACK;

component_node_kind component_node_kind_of(const node_desc *desc);
/* Input index of the Component setting; Entity follows it. */
int component_node_setting(component_node_kind kind);
/* Empty when no component is chosen. */
const char *component_node_key(const node *n);
/* A component with generated access descriptors, by key. */
const ecs_component_desc *component_by_key(const char *key);
/* The registered component a generated access descriptor or a configured
 * component node addresses. */
const ecs_component_desc *component_node_component(const node *n);
/* The generated Self getter a node reads through: a Get_<Component> node, or
 * a Get Component whose Entity is Self. NULL otherwise. */
const node_desc *component_node_getter(const graph *g, const node *n);
/* A component node whose Entity has no wire or binding addresses Self. */
bool component_node_self(const graph *g, const node *n);
/* Set shows field inputs instead of Value. */
bool component_node_split(const node *n);
/* A field Set can show on its own: a top-level field with a generated setter. */
bool component_field_settable(const ecs_component_desc *component, const char *field);

/* Chooses the component: types the Value pins, drops field pins and wires the
 * new types cannot carry. Setting the Component input's default does this. */
int component_node_retarget(graph *g, node *n, const char *key);
/* Set: shows or hides one settable field input. Hiding the last field input
 * restores Value; showing one replaces Value. */
int component_node_show_field(graph *g, node *n, const char *field, bool shown);
/* Set: shows every settable field; false restores Value. */
int component_node_split_value(graph *g, node *n, bool split);

/* Documents omit what the Component setting implies. Loading prepares a node
 * from its key and field names before applying pin entries; writing compares
 * pins with the same shapes. */
int component_node_prepare(graph *g, node *n, const char *key, bool split);
int component_node_template_count(const node *n, int output);
pin_spec component_node_template(const node *n, int output, int index);
bool component_node_field_pin(const node *n, const char *field, pin_spec *out);

/* Replaces component nodes with generated descriptors, keeping node and pin
 * IDs. A Set with several field inputs becomes a chain of field setters: the
 * first keeps the node's ID and Exec, the last its Then and Value. */
int graph_lower_component_nodes(graph *g, char *error, size_t error_size);
int behavior_asset_lower_component_nodes(behavior_asset *asset, char *error, size_t error_size);
bool behavior_asset_has_component_nodes(const behavior_asset *asset);
/* The inverse for one generated access node; false when the node has no
 * equivalent component node, such as an explicit-entity node without an
 * entity. Keeps node and pin IDs. */
bool component_node_raise(graph *g, node *n);

#endif
