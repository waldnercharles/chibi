#include "graph/model/behavior_asset.h"

#include "graph/model/registry.h"
#include "graph/runtime/value_array.h"
#include "support/xalloc.h"

#include <string.h>

const char *behavior_domain_name(behavior_domain domain)
{
    switch (domain) {
        case BEHAVIOR_DOMAIN_EVENT: return "behavior";
        case BEHAVIOR_DOMAIN_LIBRARY: return "library";
        case BEHAVIOR_DOMAIN_MATERIAL: return "material";
        case BEHAVIOR_DOMAIN_SHAPE: return "shape";
        case BEHAVIOR_DOMAIN_EVENT_DEFINITION: return "event";
        case BEHAVIOR_DOMAIN_SHADER_LIBRARY: return "shader-library";
    }
    return "invalid";
}

static int var_name_unique(const behavior_asset *bp, const char *name, uint64_t except_id)
{
    for (int i = 0; i < acount(bp->variables); i++) {
        if (bp->variables[i].id != except_id &&
            strcmp(bp->variables[i].name, name) == 0) {
            return 0;
        }
    }
    return 1;
}

behavior_asset *behavior_asset_new(const char *name)
{
    behavior_asset *bp = (behavior_asset *)xcalloc(1, sizeof *bp);
    bp->graph = (graph *)xcalloc(1, sizeof *bp->graph);
    graph_init(bp->graph);
    bp->graph->owner = bp;
    bp->graph->graph_id = 0;
    bp->graph->shared_id_counter = &bp->graph->id_counter;
    apush(bp->graphs, bp->graph);
    bp->name = graph_own_str(bp->graph, name ? name : "", name ? strlen(name) : 0);
    bp->description = graph_own_str(bp->graph, "", 0);
    return bp;
}

void behavior_asset_free(behavior_asset *bp)
{
    if (!bp) {
        return;
    }
    afree(bp->variables);
    for (int i = 0; i < acount(bp->graphs); i++) {
        graph_free(bp->graphs[i]);
        free(bp->graphs[i]);
    }
    afree(bp->graphs);
    free(bp);
}

graph *behavior_asset_add_graph(behavior_asset *bp, graph_kind kind, const char *name)
{
    if (!bp || !behavior_asset_mutable(bp) || !name || !name[0]) {
        return NULL;
    }
    if (behavior_asset_find_graph_by_name(bp, name)) {
        return NULL;
    }
    graph *g = (graph *)xcalloc(1, sizeof *g);
    graph_init(g);
    g->owner = bp;
    g->kind = kind;
    g->graph_id = graph_alloc_id(bp->graph);
    g->shared_id_counter = &bp->graph->id_counter;
    g->id_counter = bp->graph->id_counter;
    g->name = graph_own_str(g, name, strlen(name));
    g->description = graph_own_str(g, "", 0);
    apush(bp->graphs, g);
    return g;
}

graph *behavior_asset_find_graph(const behavior_asset *bp, uint64_t graph_id)
{
    if (!bp) {
        return NULL;
    }
    if (graph_id == 0) {
        return bp->graph;
    }
    for (int i = 0; i < acount(bp->graphs); i++) {
        if (bp->graphs[i]->graph_id == graph_id) {
            return bp->graphs[i];
        }
    }
    return NULL;
}

graph *behavior_asset_find_graph_by_name(const behavior_asset *bp, const char *name)
{
    if (!bp || !name) {
        return NULL;
    }
    for (int i = 0; i < acount(bp->graphs); i++) {
        if (bp->graphs[i]->name && strcmp(bp->graphs[i]->name, name) == 0) {
            return bp->graphs[i];
        }
    }
    return NULL;
}

static void behavior_asset_refresh_graph_references(behavior_asset *bp, uint64_t graph_id, const char *name)
{
    for (int graph_index = 0; graph_index < acount(bp->graphs); graph_index++) {
        graph *g = bp->graphs[graph_index];
        for (int node_index = 0; node_index < acount(g->nodes); node_index++) {
            node *n = g->nodes[node_index];
            if (n->reference_asset || n->reference_id != graph_id ||
                (n->desc != &NODE_CallFunction &&
                 n->desc != &NODE_CallPureFunction && n->desc != &NODE_CallMacro)) {
                continue;
            }
            n->reference_name = graph_own_str(g, name, strlen(name));
        }
    }
}

int behavior_asset_rename_graph(behavior_asset *bp, uint64_t graph_id, const char *name)
{
    if (!bp || !behavior_asset_mutable(bp) || graph_id == 0 || !name || !name[0]) {
        return -1;
    }
    graph *g = behavior_asset_find_graph(bp, graph_id);
    graph *same = behavior_asset_find_graph_by_name(bp, name);
    if (!g || (same && same != g)) {
        return -1;
    }
    if (graph_set_metadata(g, name, g->description ? g->description : "") != 0) {
        return -1;
    }
    behavior_asset_refresh_graph_references(bp, graph_id, name);
    return 0;
}

int behavior_asset_delete_graph(behavior_asset *bp, uint64_t graph_id)
{
    if (!bp || !behavior_asset_mutable(bp) || graph_id == 0) {
        return -1;
    }
    for (int i = 1; i < acount(bp->graphs); i++) {
        if (bp->graphs[i]->graph_id == graph_id) {
            graph *g = bp->graphs[i];
            adel(bp->graphs, i);
            graph_free(g);
            free(g);
            return 0;
        }
    }
    return -1;
}

int behavior_asset_mutable(const behavior_asset *bp)
{
    return bp && bp->graph && bp->graph->flow_pause_count == 0;
}

int behavior_asset_set_name(behavior_asset *bp, const char *name)
{
    if (!bp || !behavior_asset_mutable(bp) || !name) {
        return -1;
    }
    char *copy = graph_own_str(bp->graph, name, strlen(name));
    if (!copy) {
        return -1;
    }
    bp->name = copy;
    return 0;
}

void behavior_asset_set_description(behavior_asset *bp, const char *description)
{
    if (!bp) {
        return;
    }
    char *copy = graph_own_str(
        bp->graph,
        description ? description : "",
        description ? strlen(description) : 0
    );
    if (copy) {
        bp->description = copy;
    }
}

int behavior_asset_set_owner_type(behavior_asset *bp, const char *type_name)
{
    if (!bp || !behavior_asset_mutable(bp) || !type_name || !type_name[0]) {
        return -1;
    }
    char *copy = graph_own_str(bp->graph, type_name, strlen(type_name));
    if (!copy) {
        return -1;
    }
    bp->owner_type_name = copy;
    return 0;
}

behavior_variable *behavior_asset_add_variable(
    behavior_asset *bp,
    const char *name,
    pin_type type,
    const char *type_name,
    pin_container container,
    const pin_value *default_value,
    const char *category
)
{
    if (!bp || !behavior_asset_mutable(bp) || !name || !name[0]) {
        return NULL;
    }
    if (!var_name_unique(bp, name, 0)) {
        return NULL;
    }
    behavior_variable v = { 0 };
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
        v.default_value.container = container;
        v.default_value.type_name = v.type_name;
        if (container >= PIN_CONTAINER_ARRAY && default_value->ptr) {
            v.default_value.ptr = pin_array_clone_to_graph(
                bp->graph,
                (const pin_array *)default_value->ptr
            );
            if (!v.default_value.ptr) {
                return NULL;
            }
        }
        if ((type == PIN_STR || type == PIN_NAME) && v.default_value.str) {
            v.default_value.str = graph_own_str(
                bp->graph,
                v.default_value.str,
                strlen(v.default_value.str)
            );
        }
    } else {
        v.default_value = (pin_value){ .type = type,
                                       .container = container,
                                       .type_name = v.type_name };
        if (type == PIN_STR || type == PIN_NAME) {
            v.default_value.str = "";
        }
    }
    apush(bp->variables, v);
    return &bp->variables[acount(bp->variables) - 1];
}

behavior_variable *behavior_asset_find_variable_by_name(const behavior_asset *bp, const char *name)
{
    if (!bp || !name) {
        return NULL;
    }
    for (int i = 0; i < acount(bp->variables); i++) {
        if (strcmp(bp->variables[i].name, name) == 0) {
            return &bp->variables[i];
        }
    }
    return NULL;
}

behavior_variable *behavior_asset_find_variable_by_id(const behavior_asset *bp, uint64_t id)
{
    if (!bp) {
        return NULL;
    }
    for (int i = 0; i < acount(bp->variables); i++) {
        if (bp->variables[i].id == id) {
            return &bp->variables[i];
        }
    }
    return NULL;
}

static void refresh_reference_display(behavior_asset *bp, uint64_t id, char *display_name)
{
    for (int g = 0; g < acount(bp->graphs); g++) {
        for (int i = 0; i < acount(bp->graphs[g]->nodes); i++) {
            node *n = bp->graphs[g]->nodes[i];
            if (n->reference_id != id) {
                continue;
            }
            if (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable) {
                n->reference_name = display_name; /* both graph-owned */
            }
        }
    }
}

int behavior_asset_rename_variable(behavior_asset *bp, uint64_t id, const char *new_name)
{
    if (!bp || !behavior_asset_mutable(bp) || !new_name || !new_name[0]) {
        return -1;
    }
    behavior_variable *v = behavior_asset_find_variable_by_id(bp, id);
    if (!v || !var_name_unique(bp, new_name, id)) {
        return -1;
    }
    char *copy = graph_own_str(bp->graph, new_name, strlen(new_name));
    if (!copy) {
        return -1;
    }
    v->name = copy;
    /* VAR-5: referencing nodes keep their reference ID; only display updates. */
    refresh_reference_display(bp, id, copy);
    return 0;
}

int behavior_asset_retype_variable(behavior_asset *bp, uint64_t id, pin_type type, const char *type_name)
{
    if (!bp || !behavior_asset_mutable(bp)) {
        return -1;
    }
    behavior_variable *v = behavior_asset_find_variable_by_id(bp, id);
    if (!v) {
        return -1;
    }
    char *copy = type_name ? graph_own_str(bp->graph, type_name, strlen(type_name))
                           : NULL;
    if (type_name && !copy) {
        return -1;
    }
    v->type = type;
    v->type_name = copy;
    v->default_value = (pin_value){ .type = type,
                                    .container = v->container,
                                    .type_name = copy };
    for (int g = 0; g < acount(bp->graphs); g++) {
        for (int i = 0; i < acount(bp->graphs[g]->nodes); i++) {
            graph *target = bp->graphs[g];
            node *n = target->nodes[i];
            if (n->reference_id != id) {
                continue;
            }
            if (n->desc == &NODE_GetVariable) {
                graph_set_pin_type(target, n, 1, 0, type, copy);
            } else if (n->desc == &NODE_SetVariable) {
                graph_set_pin_type(target, n, 0, 1, type, copy);
                graph_set_pin_type(target, n, 1, 1, type, copy);
            }
        }
    }
    return 0;
}

int behavior_asset_set_variable_container(behavior_asset *bp, uint64_t id, pin_container container)
{
    if (!bp || !behavior_asset_mutable(bp) ||
        container < PIN_CONTAINER_SINGLE || container > PIN_CONTAINER_MAP) {
        return -1;
    }
    behavior_variable *v = behavior_asset_find_variable_by_id(bp, id);
    if (!v) {
        return -1;
    }
    v->container = container;
    v->default_value.container = container;
    for (int g = 0; g < acount(bp->graphs); g++) {
        for (int i = 0; i < acount(bp->graphs[g]->nodes); i++) {
            node *n = bp->graphs[g]->nodes[i];
            if (n->reference_id != id) {
                continue;
            }
            graph *target = bp->graphs[g];
            if (n->desc == &NODE_GetVariable) {
                graph_set_pin_container(target, n, 1, 0, container);
            } else if (n->desc == &NODE_SetVariable) {
                graph_set_pin_container(target, n, 0, 1, container);
                graph_set_pin_container(target, n, 1, 1, container);
            }
        }
    }
    return 0;
}

int behavior_asset_set_variable_category(behavior_asset *bp, uint64_t id, const char *category)
{
    if (!bp || !behavior_asset_mutable(bp) || !category) {
        return -1;
    }
    behavior_variable *v = behavior_asset_find_variable_by_id(bp, id);
    if (!v) {
        return -1;
    }
    v->category = graph_own_str(bp->graph, category, strlen(category));
    return v->category ? 0 : -1;
}

behavior_variable *behavior_asset_promote_pin_to_variable(
    behavior_asset *bp,
    node *n,
    int output,
    int index,
    const char *name
)
{
    if (!bp || !n || !behavior_asset_mutable(bp) || !name || !name[0]) {
        return NULL;
    }
    pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || pins[index].kind != PIN_DATA) {
        return NULL;
    }
    behavior_variable *v = behavior_asset_add_variable(
        bp,
        name,
        pins[index].type,
        pins[index].type_name,
        pins[index].container,
        &pins[index].default_value,
        "Promoted"
    );
    if (!v) {
        return NULL;
    }
    node *access = output ? behavior_asset_add_get_node(bp, v->id)
                          : behavior_asset_add_set_node(bp, v->id);
    if (!access) {
        return v;
    }
    if (output) {
        graph_connect_data(bp->graph, access, 0, n, index);
    } else {
        graph_connect_data(bp->graph, n, index, access, 1);
    }
    return v;
}

int behavior_asset_delete_variable(behavior_asset *bp, uint64_t id)
{
    if (!bp || !behavior_asset_mutable(bp)) {
        return -1;
    }
    for (int i = 0; i < acount(bp->variables); i++) {
        if (bp->variables[i].id == id) {
            /* VAR-7: referencing nodes keep their reference ID and become
             * orphaned; evaluation reports the lost variable. */
            adel(bp->variables, i);
            return 0;
        }
    }
    return -1;
}

int behavior_asset_set_variable_default(behavior_asset *bp, uint64_t id, const pin_value *value)
{
    if (!bp || !behavior_asset_mutable(bp) || !value) {
        return -1;
    }
    behavior_variable *v = behavior_asset_find_variable_by_id(bp, id);
    if (!v || value->type != v->type) {
        return -1;
    }
    v->default_value = *value;
    v->default_value.container = v->container;
    v->default_value.type_name = v->type_name;
    if (v->container >= PIN_CONTAINER_ARRAY && value->ptr) {
        v->default_value
            .ptr = pin_array_clone_to_graph(bp->graph, (const pin_array *)value->ptr);
    }
    if ((value->type == PIN_STR || value->type == PIN_NAME) && value->str) {
        v->default_value.str = graph_own_str(bp->graph, value->str, strlen(value->str));
    }
    return 0;
}

node *behavior_asset_add_get_node(behavior_asset *bp, uint64_t variable_id)
{
    if (!bp || !behavior_asset_mutable(bp)) {
        return NULL;
    }
    behavior_variable *v = behavior_asset_find_variable_by_id(bp, variable_id);
    if (!v) {
        return NULL;
    }
    node *n = graph_add_node(bp->graph, &NODE_GetVariable);
    if (!n) {
        return NULL;
    }
    graph_set_node_reference(bp->graph, n, v->id, v->name);
    graph_set_pin_type(bp->graph, n, 1, 0, v->type, v->type_name);
    return n;
}

node *behavior_asset_add_set_node(behavior_asset *bp, uint64_t variable_id)
{
    if (!bp || !behavior_asset_mutable(bp)) {
        return NULL;
    }
    behavior_variable *v = behavior_asset_find_variable_by_id(bp, variable_id);
    if (!v) {
        return NULL;
    }
    node *n = graph_add_node(bp->graph, &NODE_SetVariable);
    if (!n) {
        return NULL;
    }
    graph_set_node_reference(bp->graph, n, v->id, v->name);
    graph_set_pin_type(bp->graph, n, 0, 1, v->type, v->type_name);
    graph_set_pin_type(bp->graph, n, 1, 1, v->type, v->type_name);
    return n;
}

node *behavior_asset_add_self_node(behavior_asset *bp)
{
    if (!bp || !behavior_asset_mutable(bp) || !bp->owner_type_name) {
        return NULL;
    }
    node *n = graph_add_node(bp->graph, &NODE_Self);
    if (!n) {
        return NULL;
    }
    graph_set_pin_type(bp->graph, n, 1, 0, PIN_HANDLE, bp->owner_type_name);
    return n;
}

static int behavior_asset_add_call_pin(graph *caller, node *call, int output, const pin_spec *source)
{
    pin_spec pin = *source;
    pin.id = 0;
    pin.declaration_id = source->id;
    if (pin.type_name) {
        pin.type_name = graph_own_str(caller, pin.type_name, strlen(pin.type_name));
    }
    if (pin.container >= PIN_CONTAINER_ARRAY && pin.default_value.ptr) {
        pin.default_value.ptr = pin_array_clone_to_graph(
            caller,
            (const pin_array *)pin.default_value.ptr
        );
    } else if (
        (pin.type == PIN_STR || pin.type == PIN_NAME) && pin.default_value.str
    ) {
        pin.default_value.str = graph_own_str(
            caller,
            pin.default_value.str,
            strlen(pin.default_value.str)
        );
    }
    return graph_add_pin(caller, call, output, &pin) < 0 ? -1 : 0;
}

static node *behavior_asset_add_call_node(behavior_asset *bp, graph *caller, graph *target, graph_kind kind)
{
    if (!bp || !behavior_asset_mutable(bp) || !caller || caller->owner != bp || !target ||
        (target->owner != bp &&
         (!target->owner || (target->owner->domain != BEHAVIOR_DOMAIN_LIBRARY &&
                             target->owner->domain != BEHAVIOR_DOMAIN_SHADER_LIBRARY))) ||
        target->kind != kind) {
        return NULL;
    }
    node *entry = graph_node_by_id(target, target->entry_id);
    node *ret = graph_node_by_id(target, target->return_id);
    if (!entry || !ret) {
        return NULL;
    }
    int pure = kind == GRAPH_FUNCTION;
    for (int i = 0; pure && i < entry->output_count; i++) {
        pure = entry->output_pins[i].kind != PIN_EXEC;
    }
    for (int i = 0; pure && i < ret->input_count; i++) {
        pure = ret->input_pins[i].kind != PIN_EXEC;
    }
    const node_desc *desc = kind == GRAPH_MACRO
                              ? &NODE_CallMacro
                              : (pure ? &NODE_CallPureFunction : &NODE_CallFunction);
    node *call = graph_add_node(caller, desc);
    if (!call) {
        return NULL;
    }
    if (graph_set_node_reference(caller, call, target->graph_id, target->name) != 0) {
        graph_remove_node(caller, call);
        return NULL;
    }
    if (kind == GRAPH_MACRO) {
        graph_remove_pin(caller, call, 0, 0);
        graph_remove_pin(caller, call, 1, 0);
    }
    if (target->owner != bp) {
        call->reference_asset = graph_own_str(
            caller,
            target->owner->name,
            strlen(target->owner->name)
        );
    }
    for (int i = 0; i < entry->output_count; i++) {
        if ((entry->output_pins[i].kind == PIN_DATA || kind == GRAPH_MACRO) &&
            !entry->output_pins[i].field_source &&
            behavior_asset_add_call_pin(caller, call, 0, &entry->output_pins[i]) != 0) {
            graph_remove_node(caller, call);
            return NULL;
        }
    }
    for (int i = 0; i < ret->input_count; i++) {
        if ((ret->input_pins[i].kind == PIN_DATA || kind == GRAPH_MACRO) &&
            behavior_asset_add_call_pin(caller, call, 1, &ret->input_pins[i]) != 0) {
            graph_remove_node(caller, call);
            return NULL;
        }
    }
    return call;
}

node *behavior_asset_add_function_call_node_to_graph(behavior_asset *bp, graph *caller, graph *function_graph)
{
    return behavior_asset_add_call_node(bp, caller, function_graph, GRAPH_FUNCTION);
}

node *behavior_asset_add_macro_call_node_to_graph(behavior_asset *bp, graph *caller, graph *macro_graph)
{
    return behavior_asset_add_call_node(bp, caller, macro_graph, GRAPH_MACRO);
}

node *behavior_asset_add_function_call_node(behavior_asset *bp, graph *function_graph)
{
    return behavior_asset_add_function_call_node_to_graph(bp, bp ? bp->graph : NULL, function_graph);
}

node *behavior_asset_add_macro_call_node(behavior_asset *bp, graph *macro_graph)
{
    return behavior_asset_add_macro_call_node_to_graph(bp, bp ? bp->graph : NULL, macro_graph);
}

graph *behavior_asset_add_function(behavior_asset *bp, const char *name, int pure)
{
    graph *g = behavior_asset_add_graph(bp, GRAPH_FUNCTION, name);
    if (!g) {
        return NULL;
    }
    node *entry = graph_add_node(g, &NODE_FunctionEntry);
    node *ret = graph_add_node(g, &NODE_Return);
    g->entry_id = entry ? entry->id : 0;
    g->return_id = ret ? ret->id : 0;
    g->call_depth_limit = 64;
    if (pure && entry && ret) {
        if (entry->output_count && entry->output_pins[0].kind == PIN_EXEC) {
            graph_remove_pin(g, entry, 1, 0);
        }
        if (ret->input_count && ret->input_pins[0].kind == PIN_EXEC) {
            graph_remove_pin(g, ret, 0, 0);
        }
    }
    return g;
}

graph *behavior_asset_add_macro(behavior_asset *bp, const char *name)
{
    graph *g = behavior_asset_add_graph(bp, GRAPH_MACRO, name);
    if (!g) {
        return NULL;
    }
    node *entry = graph_add_node(g, &NODE_FunctionEntry);
    node *ret = graph_add_node(g, &NODE_Return);
    g->entry_id = entry ? entry->id : 0;
    g->return_id = ret ? ret->id : 0;
    return g;
}
