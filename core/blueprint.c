/* core/blueprint.c -- authored Blueprint definitions (BP-1). The definition
 * owns its default event graph (BP-8), its ID counter, metadata, optional
 * nominal owner handle type (BP-12), and variable list. All strings live in
 * the graph's string table so a single graph_free releases them. */
#include "blueprint.h"

#include "registry.h"
#include "xalloc.h"

#include <string.h>

static int var_name_unique(const blueprint *bp, const char *name, uint64_t except_id)
{
    for (int i = 0; i < acount(bp->variables); i++)
        if (bp->variables[i].id != except_id && strcmp(bp->variables[i].name, name) == 0)
            return 0;
    return 1;
}

blueprint *blueprint_new(const char *name)
{
    blueprint *bp = (blueprint *)xcalloc(1, sizeof *bp);
    bp->graph = (graph *)xcalloc(1, sizeof *bp->graph);
    graph_init(bp->graph);
    bp->name = graph_own_str(bp->graph, name ? name : "", name ? strlen(name) : 0);
    bp->description = graph_own_str(bp->graph, "", 0);
    return bp;
}

void blueprint_free(blueprint *bp)
{
    if (!bp) return;
    afree(bp->variables);
    graph_free(bp->graph);
    free(bp->graph);
    free(bp);
}

int blueprint_mutable(const blueprint *bp)
{ return bp && bp->graph && bp->graph->flow_pause_count == 0; }

void blueprint_set_description(blueprint *bp, const char *description)
{
    if (!bp) return;
    char *copy = graph_own_str(
        bp->graph,
        description ? description : "",
        description ? strlen(description) : 0
    );
    if (copy) bp->description = copy;
}

int blueprint_set_owner_type(blueprint *bp, const char *type_name)
{
    if (!bp || !blueprint_mutable(bp) || !type_name || !type_name[0]) return -1;
    char *copy = graph_own_str(bp->graph, type_name, strlen(type_name));
    if (!copy) return -1;
    bp->owner_type_name = copy;
    return 0;
}

blueprint_variable *blueprint_add_variable(
    blueprint *bp,
    const char *name,
    pin_type type,
    const char *type_name,
    pin_container container,
    const pin_value *default_value,
    const char *category
)
{
    if (!bp || !blueprint_mutable(bp) || !name || !name[0]) return NULL;
    if (!var_name_unique(bp, name, 0)) return NULL;
    blueprint_variable v = { 0 };
    v.id = graph_alloc_id(bp->graph);
    v.name = graph_own_str(bp->graph, name, strlen(name));
    v.type = type;
    v.type_name = type_name ? graph_own_str(bp->graph, type_name, strlen(type_name))
                            : NULL;
    v.container = container;
    v.category = category && category[0]
                   ? graph_own_str(bp->graph, category, strlen(category))
                   : NULL;
    if (default_value) {
        v.default_value = *default_value;
        v.default_value.type = type;
        v.default_value.type_name = v.type_name;
        if ((type == PIN_STR || type == PIN_NAME) && v.default_value.str)
            v.default_value.str = graph_own_str(
                bp->graph,
                v.default_value.str,
                strlen(v.default_value.str)
            );
    } else {
        v.default_value = (pin_value){ .type = type, .type_name = v.type_name };
        if (type == PIN_STR || type == PIN_NAME) v.default_value.str = "";
    }
    apush(bp->variables, v);
    return &bp->variables[acount(bp->variables) - 1];
}

blueprint_variable *blueprint_find_variable_by_name(const blueprint *bp, const char *name)
{
    if (!bp || !name) return NULL;
    for (int i = 0; i < acount(bp->variables); i++)
        if (strcmp(bp->variables[i].name, name) == 0) return &bp->variables[i];
    return NULL;
}

blueprint_variable *blueprint_find_variable_by_id(const blueprint *bp, uint64_t id)
{
    if (!bp) return NULL;
    for (int i = 0; i < acount(bp->variables); i++)
        if (bp->variables[i].id == id) return &bp->variables[i];
    return NULL;
}

static void refresh_reference_display(blueprint *bp, uint64_t id, char *display_name)
{
    for (int i = 0; i < acount(bp->graph->nodes); i++) {
        node *n = bp->graph->nodes[i];
        if (n->reference_id != id) continue;
        if (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable)
            n->reference_name = display_name; /* both graph-owned */
    }
}

int blueprint_rename_variable(blueprint *bp, uint64_t id, const char *new_name)
{
    if (!bp || !blueprint_mutable(bp) || !new_name || !new_name[0]) return -1;
    blueprint_variable *v = blueprint_find_variable_by_id(bp, id);
    if (!v || !var_name_unique(bp, new_name, id)) return -1;
    char *copy = graph_own_str(bp->graph, new_name, strlen(new_name));
    if (!copy) return -1;
    v->name = copy;
    /* VAR-5: referencing nodes keep their reference ID; only display updates. */
    refresh_reference_display(bp, id, copy);
    return 0;
}

int blueprint_retype_variable(blueprint *bp, uint64_t id, pin_type type, const char *type_name)
{
    if (!bp || !blueprint_mutable(bp)) return -1;
    blueprint_variable *v = blueprint_find_variable_by_id(bp, id);
    if (!v) return -1;
    char *copy = type_name ? graph_own_str(bp->graph, type_name, strlen(type_name))
                           : NULL;
    if (type_name && !copy) return -1;
    v->type = type;
    v->type_name = copy;
    v->default_value = (pin_value){ .type = type, .type_name = copy };
    for (int i = 0; i < acount(bp->graph->nodes); i++) {
        node *n = bp->graph->nodes[i];
        if (n->reference_id != id) continue;
        if (n->desc == &NODE_GetVariable)
            graph_set_pin_type(bp->graph, n, 1, 0, type, copy);
        else if (n->desc == &NODE_SetVariable) {
            graph_set_pin_type(bp->graph, n, 0, 1, type, copy);
            graph_set_pin_type(bp->graph, n, 1, 1, type, copy);
        }
    }
    return 0;
}

int blueprint_delete_variable(blueprint *bp, uint64_t id)
{
    if (!bp || !blueprint_mutable(bp)) return -1;
    for (int i = 0; i < acount(bp->variables); i++)
        if (bp->variables[i].id == id) {
            /* VAR-7: referencing nodes keep their reference ID and become
             * orphaned; evaluation reports the lost variable. */
            adel(bp->variables, i);
            return 0;
        }
    return -1;
}

int blueprint_set_variable_default(blueprint *bp, uint64_t id, const pin_value *value)
{
    if (!bp || !blueprint_mutable(bp) || !value) return -1;
    blueprint_variable *v = blueprint_find_variable_by_id(bp, id);
    if (!v || value->type != v->type) return -1;
    v->default_value = *value;
    v->default_value.type_name = v->type_name;
    if ((value->type == PIN_STR || value->type == PIN_NAME) && value->str)
        v->default_value.str = graph_own_str(bp->graph, value->str, strlen(value->str));
    return 0;
}

node *blueprint_add_get_node(blueprint *bp, uint64_t variable_id)
{
    if (!bp || !blueprint_mutable(bp)) return NULL;
    blueprint_variable *v = blueprint_find_variable_by_id(bp, variable_id);
    if (!v) return NULL;
    node *n = graph_add_node(bp->graph, &NODE_GetVariable);
    if (!n) return NULL;
    graph_set_node_reference(bp->graph, n, v->id, v->name);
    graph_set_pin_type(bp->graph, n, 1, 0, v->type, v->type_name);
    return n;
}

node *blueprint_add_set_node(blueprint *bp, uint64_t variable_id)
{
    if (!bp || !blueprint_mutable(bp)) return NULL;
    blueprint_variable *v = blueprint_find_variable_by_id(bp, variable_id);
    if (!v) return NULL;
    node *n = graph_add_node(bp->graph, &NODE_SetVariable);
    if (!n) return NULL;
    graph_set_node_reference(bp->graph, n, v->id, v->name);
    graph_set_pin_type(bp->graph, n, 0, 1, v->type, v->type_name);
    graph_set_pin_type(bp->graph, n, 1, 1, v->type, v->type_name);
    return n;
}

node *blueprint_add_self_node(blueprint *bp)
{
    if (!bp || !blueprint_mutable(bp) || !bp->owner_type_name) return NULL;
    node *n = graph_add_node(bp->graph, &NODE_Self);
    if (!n) return NULL;
    graph_set_pin_type(bp->graph, n, 1, 0, PIN_HANDLE, bp->owner_type_name);
    return n;
}

node *blueprint_add_call_event_node(blueprint *bp, node *event_node)
{
    if (!bp || !blueprint_mutable(bp) || !event_node ||
        event_node->desc->flow_kind != NODE_FLOW_EVENT)
        return NULL;
    node *n = graph_add_node(bp->graph, &NODE_CallEvent);
    if (!n) return NULL;
    graph_set_node_reference(bp->graph, n, event_node->id, event_node->reference_name);
    for (int i = 0; i < event_node->output_count; i++) {
        const pin_spec *p = &event_node->output_pins[i];
        if (p->kind != PIN_DATA) continue;
        pin_spec param = *p;
        param.id = 0;
        param.default_value = (pin_value){ 0 };
        if (graph_add_pin(bp->graph, n, 0, &param) < 0) return NULL;
    }
    return n;
}
