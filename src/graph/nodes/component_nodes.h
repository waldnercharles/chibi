#ifndef CHIBI_COMPONENT_NODES_H
#define CHIBI_COMPONENT_NODES_H

#include "ecs/ecs.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"

/* Authored nodes that stand for families of generated descriptors. A setting
 * names the member: a reflected component key for Get, Set, Has, Add and
 * Remove Component, or a struct type for Make and Break Struct. Lowering
 * replaces each node with the generated descriptor it names, which remain
 * the only nodes evaluation, compilation and debugging see.
 *
 * Get Component shows the fields it uses as exposed outputs, and Set writes
 * its Value, or only the field inputs it shows; Entity follows their setting
 * and defaults to Self. Add, Remove, Make and Break end with their setting,
 * after the pins of the generated node they become. */
typedef enum
{
    COMPONENT_NODE_NONE = 0,
    COMPONENT_NODE_GET,
    COMPONENT_NODE_SET,
    COMPONENT_NODE_HAS,
    COMPONENT_NODE_ADD,
    COMPONENT_NODE_REMOVE,
    COMPONENT_NODE_MAKE,
    COMPONENT_NODE_BREAK,
} component_node_kind;

extern node_desc NODE_GetComponent, NODE_SetComponent, NODE_HasComponent,
    NODE_AddComponent, NODE_RemoveComponent, NODE_MakeStruct, NODE_BreakStruct;
extern const registry_pack CORE_COMPONENT_PACK;

component_node_kind component_node_kind_of(const node_desc *desc);
/* The kind whose node lowers to this generated descriptor; NONE otherwise. */
component_node_kind component_generated_kind(const node_desc *desc);
/* Input index of the Component or Type setting. */
int component_node_setting(const node *n);
/* Empty when nothing is chosen. */
const char *component_node_key(const node *n);
/* A component with generated access descriptors, by key or name. */
const ecs_component_desc *component_by_key(const char *key);
/* The stored form of a setting: a component's key or a struct type's name.
 * NULL when the kind has no member by that key. */
const char *component_node_canonical_key(component_node_kind kind, const char *key);
/* The registered component a generated access descriptor or a configured
 * Get, Set, Has, Add or Remove node addresses. */
const ecs_component_desc *component_node_component(const node *n);
/* The generated Make_<T> or Break_<T> a Make or Break Struct node, or a
 * generated one, stands for. NULL otherwise. */
const node_desc *component_node_struct(const node *n);
/* The generated node an Add, Remove, Make or Break node with this setting
 * lowers to. NULL for other kinds and unknown settings. */
const node_desc *component_node_shape(const node_desc *desc, const char *key);
/* The generated descriptor a component node lowers to (a split Set's first
 * field setter), or any other node's own. NULL without a component or type. */
const node_desc *component_node_target(const graph *g, const node *n);
/* Names the access either form performs: Get, Set, Has, Add, Remove, Make or
 * Break, and the component or struct type it addresses. */
bool component_node_access(const node *n, const char **verb, const char **name);
/* The generated Self getter a node reads through: a Get_<Component> node, or
 * a Get Component whose Entity is Self. NULL otherwise. */
const node_desc *component_node_getter(const graph *g, const node *n);
/* A Get, Set or Has node whose Entity has no wire, binding or handle
 * addresses Self. */
bool component_node_self(const graph *g, const node *n);
/* Set shows field inputs instead of Value. */
bool component_node_split(const node *n);
/* A field Set can show on its own: a top-level field with a generated setter. */
bool component_field_settable(const ecs_component_desc *component, const char *field);

/* Chooses the component or type and reshapes the pins, dropping field pins
 * and the wires new types cannot carry. Setting the setting's default does
 * this. */
int component_node_retarget(graph *g, node *n, const char *key);
/* Set: shows or hides one settable field input. Hiding the last field input
 * restores Value; showing one replaces Value. */
int component_node_show_field(graph *g, node *n, const char *field, bool shown);
/* Set: shows every settable field; false restores Value. */
int component_node_split_value(graph *g, node *n, bool split);

/* A configured component node that search offers: the node, its Component
 * or Type setting, and for Set the one field it writes. Type names the
 * component's or struct's type. */
typedef struct
{
    const node_desc *desc;
    const char *setting;
    const char *field;
    const type_desc *type;
} component_node_preset;

/* Get, Set, Has, Add and Remove for each component, Set for each settable
 * field, and Make and Break for each struct. The caller frees the array. */
component_node_preset *component_node_presets();
/* A sentence on what the preset does, such as "Writes Health's current on
 * Entity, which defaults to Self." */
void component_node_preset_describe(const component_node_preset *preset, char *buf, size_t size);
/* Configures a new component node as its preset. */
int component_node_configure(graph *g, node *n, const char *setting, const char *field);

/* Documents omit what the setting implies. Loading prepares a node from its
 * key and field names before applying pin entries; writing compares pins
 * with the same shapes. */
int component_node_prepare(graph *g, node *n, const char *key, bool split);
int component_node_template_count(const node *n, int output);
pin_spec component_node_template(const node *n, int output, int index);
bool component_node_field_pin(const node *n, const char *field, pin_spec *out);

/* Replaces component nodes with generated descriptors, keeping node and pin
 * IDs. A Set with several field inputs becomes a chain of field setters: the
 * first keeps the node's ID and Exec, the last its Then and Value. */
/* Lowers one configured node, preserving unrelated authored nodes. */
int component_node_lower(graph *g, node *n, char *error, size_t error_size);
int graph_lower_component_nodes(graph *g, char *error, size_t error_size);
int behavior_asset_lower_component_nodes(behavior_asset *asset, char *error, size_t error_size);
bool behavior_asset_has_component_nodes(const behavior_asset *asset);
/* The inverse for one generated node; false when the node has no equivalent
 * component node, such as an explicit-entity node without an entity. Keeps
 * node and pin IDs. */
bool component_node_raise(graph *g, node *n);

#endif
