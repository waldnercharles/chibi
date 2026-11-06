#include "graph/authoring/asset_edit.h"

#include "graph/authoring/graph_edit.h"
#include "graph/document/graph_signature.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/graph_internal.h"
#include "graph/model/registry.h"
#include "graph/model/value_copy.h"
#include "graph/nodes/core_nodes.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static void behavior_asset_refresh_graph_references(
    behavior_asset *asset,
    uint64_t graph_id,
    const char *name
)
{
    for (int graph_index = 0; graph_index < acount(asset->graphs); graph_index++) {
        graph *g = asset->graphs[graph_index];
        for (int node_index = 0; node_index < acount(g->nodes); node_index++) {
            node *n = g->nodes[node_index];
            if (n->reference_asset || n->reference_id != graph_id ||
                (n->desc != &NODE_CallFunction && n->desc != &NODE_CallPureFunction &&
                 n->desc != &NODE_BehaviorExpression && n->desc != &NODE_CallMacro)) {
                continue;
            }
            n->reference_name = graph_own_str(g, name, strlen(name));
        }
    }
}
int behavior_asset_rename_graph(behavior_asset *asset, uint64_t graph_id, const char *name)
{
    if (!asset || !behavior_asset_mutable(asset) || graph_id == 0 || !name ||
        !name[0]) {
        return -1;
    }
    graph *g = behavior_asset_find_graph(asset, graph_id);
    graph *same = behavior_asset_find_graph_by_name(asset, name);
    if (!g || (same && same != g)) {
        return -1;
    }
    if (graph_set_metadata(g, name, g->description ? g->description : "") != 0) {
        return -1;
    }
    behavior_asset_refresh_graph_references(asset, graph_id, name);
    return 0;
}
static void refresh_reference_display(behavior_asset *asset, uint64_t id, char *display_name)
{
    for (int g = 0; g < acount(asset->graphs); g++) {
        for (int i = 0; i < acount(asset->graphs[g]->nodes); i++) {
            node *n = asset->graphs[g]->nodes[i];
            if (n->reference_id != id) {
                continue;
            }
            if (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable) {
                n->reference_name = display_name; /* both graph-owned */
            }
        }
    }
}
int behavior_asset_rename_variable(behavior_asset *asset, uint64_t id, const char *new_name)
{
    if (!asset || !behavior_asset_mutable(asset) || !new_name || !new_name[0]) {
        return -1;
    }
    behavior_variable *v = behavior_asset_find_variable_by_id(asset, id);
    if (!v || !behavior_asset_variable_name_unique(asset, new_name, id)) {
        return -1;
    }
    char *copy = graph_own_str(asset->graph, new_name, strlen(new_name));
    if (!copy) {
        return -1;
    }
    v->name = copy;
    /* VAR-5: referencing nodes keep their reference ID; only display updates. */
    refresh_reference_display(asset, id, copy);
    return 0;
}
int behavior_asset_retype_variable(behavior_asset *asset, uint64_t id, pin_type type, const char *type_name)
{
    if (!asset || !behavior_asset_mutable(asset)) {
        return -1;
    }
    behavior_variable *v = behavior_asset_find_variable_by_id(asset, id);
    if (!v) {
        return -1;
    }
    char *copy = type_name
                   ? graph_own_str(asset->graph, type_name, strlen(type_name))
                   : NULL;
    if (type_name && !copy) {
        return -1;
    }
    v->type = type;
    v->type_name = copy;
    v->default_value = (pin_value){ .type = type,
                                    .container = v->container,
                                    .type_name = copy };
    for (int g = 0; g < acount(asset->graphs); g++) {
        for (int i = 0; i < acount(asset->graphs[g]->nodes); i++) {
            graph *target = asset->graphs[g];
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
int behavior_asset_set_variable_container(behavior_asset *asset, uint64_t id, pin_container container)
{
    if (!asset || !behavior_asset_mutable(asset) ||
        container < PIN_CONTAINER_SINGLE || container > PIN_CONTAINER_VIEW) {
        return -1;
    }
    behavior_variable *v = behavior_asset_find_variable_by_id(asset, id);
    if (!v) {
        return -1;
    }
    if (v->container != container) {
        v->default_value = (pin_value){ .type = v->type,
                                        .type_name = v->type_name,
                                        .container = container };
        if (container == PIN_CONTAINER_SINGLE && v->type == PIN_NAME) {
            v->default_value.str = "";
        }
    }
    v->container = container;
    for (int g = 0; g < acount(asset->graphs); g++) {
        for (int i = 0; i < acount(asset->graphs[g]->nodes); i++) {
            node *n = asset->graphs[g]->nodes[i];
            if (n->reference_id != id) {
                continue;
            }
            graph *target = asset->graphs[g];
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
/* A Get or Set node for v in g. */
static node *add_access(graph *g, const behavior_variable *v, bool set)
{
    node *n = graph_add_node(g, set ? &NODE_SetVariable : &NODE_GetVariable);
    if (!n) {
        return NULL;
    }
    graph_set_node_reference(g, n, v->id, v->name);
    if (set) {
        graph_set_pin_type(g, n, 0, 1, v->type, v->type_name);
        graph_set_pin_container(g, n, 0, 1, v->container);
    }
    graph_set_pin_type(g, n, 1, set, v->type, v->type_name);
    graph_set_pin_container(g, n, 1, set, v->container);
    return n;
}
/* The execution output a node goes on from when it has only one, or -1. */
static int continuation(const node *n)
{
    int found = -1;
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].kind != PIN_EXEC) {
            continue;
        }
        if (found >= 0) {
            return -1;
        }
        found = i;
    }
    return found;
}
static graph *graph_of(const behavior_asset *asset, const node *n)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        if (graph_node_by_id(asset->graphs[i], n->id) == n) {
            return asset->graphs[i];
        }
    }
    return NULL;
}
behavior_variable *behavior_asset_promote_pin_to_variable(
    behavior_asset *asset,
    node *n,
    int output,
    int index,
    const char *name
)
{
    graph *g = asset && n ? graph_of(asset, n) : NULL;
    if (!g || !behavior_asset_mutable(asset) || !name || !name[0]) {
        return NULL;
    }
    pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || pins[index].kind != PIN_DATA) {
        return NULL;
    }
    behavior_variable *v = behavior_asset_add_variable(
        asset,
        name,
        pins[index].type,
        pins[index].type_name,
        pins[index].container,
        &pins[index].default_value,
        "Promoted"
    );
    node *access = v ? add_access(g, v, output) : NULL;
    if (access && output) {
        graph_connect_data(g, n, index, access, 1);
        int then = continuation(n);
        const edge *next = NULL;
        if (then >= 0 && !graph_exec_output_edges(g, n, then, &next)) {
            graph_connect_exec(g, n, then, access, 0, 0);
        }
    } else if (access) {
        graph_connect_data(g, access, 0, n, index);
    }
    return v;
}
static bool variable_node(const node *n, uint64_t id)
{
    return n->reference_id == id &&
           (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable);
}
static bool variable_binding(const behavior_asset *asset, const pin_binding *binding, uint64_t id)
{
    return binding && binding->kind == PIN_BINDING_VARIABLE &&
           binding->variable_id == id &&
           (!binding->source_asset || !strcmp(binding->source_asset, asset->name));
}
static bool variable_assignment(behavior_asset *asset, graph *g, const node *n, uint64_t id)
{
    if (n->desc != &NODE_BehaviorExpression) {
        return false;
    }
    for (int p = 0; p < n->input_count; p++) {
        if (!(n->input_pins[p].flags & PIN_FLAG_REFERENCE)) {
            continue;
        }
        if (variable_binding(asset, n->input_pins[p].binding, id)) {
            return true;
        }
        node *source = NULL;
        int pin = -1;
        pin_binding binding;
        if (graph_input_source(g, n, p, &source, &pin) &&
            graph_resolve_reference(g, source, pin, &binding) == 0 &&
            variable_binding(asset, &binding, id)) {
            return true;
        }
    }
    return false;
}
/* Removing a write must retain the independent actions after its continuation. */
static int remove_variable_node(graph *g, node *n)
{
    edge *incoming = NULL, *outgoing = NULL;
    int then = continuation(n);
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind != GRAPH_EDGE_EXEC) {
            continue;
        }
        if (e->to_id == n->id && e->from_id != n->id) {
            apush(incoming, *e);
        }
        if (then >= 0 && e->from_id == n->id && e->to_id != n->id &&
            e->from_pin_id == n->output_pins[then].id) {
            apush(outgoing, *e);
        }
    }
    int result = graph_remove_node(g, n);
    if (!result) {
        for (int i = 0; i < acount(incoming); i++) {
            for (int j = 0; j < acount(outgoing); j++) {
                int connected = graph_connect_exec_ids(
                    g,
                    incoming[i].from_id,
                    incoming[i].from_pin_id,
                    outgoing[j].to_id,
                    outgoing[j].to_pin_id,
                    incoming[i].exec_order + outgoing[j].exec_order
                );
                assert(connected == 0);
                (void)connected;
            }
        }
    }
    afree(incoming);
    afree(outgoing);
    return result;
}
int behavior_asset_remove_variable_uses(behavior_asset *asset, uint64_t id)
{
    int removed = 0;
    uint64_t *expressions = NULL;
    /* Remove assignments before their referenced Get nodes or bindings. */
    for (int g = 0; g < acount(asset->graphs); g++) {
        graph *target = asset->graphs[g];
        for (int i = acount(target->nodes) - 1; i >= 0; i--) {
            node *n = target->nodes[i];
            if (variable_assignment(asset, target, n, id)) {
                apush(expressions, n->reference_id);
                removed += remove_variable_node(target, n) == 0;
            }
        }
    }
    for (int i = 0; i < acount(expressions); i++) {
        bool used = false;
        for (int g = 0; g < acount(asset->graphs); g++) {
            for (int n = 0; n < acount(asset->graphs[g]->nodes); n++) {
                const node *call = asset->graphs[g]->nodes[n];
                used |= call->reference_id == expressions[i] && !call->reference_asset;
            }
        }
        graph *generated = behavior_asset_find_graph(asset, expressions[i]);
        if (!used && generated && generated->math_expression) {
            int deleted = behavior_asset_delete_graph(asset, expressions[i]);
            assert(deleted == 0);
            (void)deleted;
        }
    }
    afree(expressions);
    for (int g = 0; g < acount(asset->graphs); g++) {
        graph *target = asset->graphs[g];
        for (int i = acount(target->nodes) - 1; i >= 0; i--) {
            node *n = target->nodes[i];
            if (variable_node(n, id)) {
                removed += remove_variable_node(target, n) == 0;
                continue;
            }
            for (int p = 0; p < n->input_count; p++) {
                const pin_binding *binding = n->input_pins[p].binding;
                if (variable_binding(asset, binding, id)) {
                    graph_set_pin_binding(target, n, 0, p, NULL);
                }
            }
        }
    }
    return removed;
}
static node *add_variable_node(behavior_asset *asset, graph *g, uint64_t variable_id, bool set)
{
    const behavior_variable *v = behavior_asset_find_variable_by_id(asset, variable_id);
    return v && behavior_asset_mutable(asset) && g && g->owner == asset
             ? add_access(g, v, set)
             : NULL;
}
node *behavior_asset_add_get_node_to_graph(behavior_asset *asset, graph *g, uint64_t variable_id)
{
    return add_variable_node(asset, g, variable_id, false);
}
node *behavior_asset_add_set_node_to_graph(behavior_asset *asset, graph *g, uint64_t variable_id)
{
    return add_variable_node(asset, g, variable_id, true);
}
node *behavior_asset_add_get_node(behavior_asset *asset, uint64_t variable_id)
{
    return asset ? add_variable_node(asset, asset->graph, variable_id, false) : NULL;
}
node *behavior_asset_add_set_node(behavior_asset *asset, uint64_t variable_id)
{
    return asset ? add_variable_node(asset, asset->graph, variable_id, true) : NULL;
}
node *behavior_asset_add_self_node(behavior_asset *asset)
{
    if (!asset || !behavior_asset_mutable(asset) || !asset->owner_type_name) {
        return NULL;
    }
    node *n = graph_add_node(asset->graph, &NODE_Self);
    if (!n) {
        return NULL;
    }
    graph_set_pin_type(asset->graph, n, 1, 0, PIN_HANDLE, asset->owner_type_name);
    return n;
}
void behavior_asset_promote_shared_expressions(behavior_asset *asset)
{
    assert(asset && behavior_asset_mutable(asset));
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *target = asset->graphs[i];
        if (!target->math_expression || target->expression_mode != GRAPH_EXPRESSION_VALUE ||
            !graph_signature_pure(target)) {
            continue;
        }
        int callers = 0;
        for (int j = 0; j < acount(asset->graphs); j++) {
            graph *g = asset->graphs[j];
            for (int k = 0; k < acount(g->nodes); k++) {
                node *n = g->nodes[k];
                callers += n->desc == &NODE_BehaviorExpression &&
                           !n->reference_asset && n->reference_id == target->graph_id;
            }
        }
        if (callers < 2) {
            continue;
        }
        const char *name = target->name && target->name[0] ? target->name
                                                           : target->math_expression;
        char unique[160];
        for (int j = 0; j < acount(asset->graphs); j++) {
            graph *other = asset->graphs[j];
            if (other != target && other->kind == GRAPH_FUNCTION &&
                other->name && !strcmp(other->name, name)) {
                snprintf(
                    unique,
                    sizeof unique,
                    "Expression %llu",
                    (unsigned long long)target->graph_id
                );
                name = unique;
                break;
            }
        }
        int changed = graph_set_metadata(
            target,
            name,
            target->description ? target->description : ""
        );
        assert(!changed);
        (void)changed;
        target->math_expression = NULL;
        target->expression_mode = GRAPH_EXPRESSION_NONE;
        node *entry = graph_node_by_id(target, target->entry_id);
        node *ret = graph_node_by_id(target, target->return_id);
        assert(entry && ret);
        for (int j = 0; j < acount(asset->graphs); j++) {
            graph *g = asset->graphs[j];
            for (int k = 0; k < acount(g->nodes); k++) {
                node *n = g->nodes[k];
                if (n->desc != &NODE_BehaviorExpression || n->reference_asset ||
                    n->reference_id != target->graph_id) {
                    continue;
                }
                n->desc = &NODE_CallPureFunction;
                for (int side = 0; side < 2; side++) {
                    pin_spec *pins = side ? n->output_pins : n->input_pins;
                    int count = side ? n->output_count : n->input_count;
                    const pin_spec *signature = side ? ret->input_pins
                                                     : entry->output_pins;
                    int total = side ? ret->input_count : entry->output_count;
                    for (int p = 0; p < count; p++) {
                        for (int q = 0; q < total; q++) {
                            if (pins[p].declaration_id == signature[q].id ||
                                (!pins[p].declaration_id &&
                                 !strcmp(pins[p].name, signature[q].name))) {
                                pins[p].declaration_id = signature[q].id;
                                if (pins[p].flags & PIN_FLAG_WILDCARD) {
                                    pins[p].group = signature[q].group;
                                }
                                break;
                            }
                        }
                    }
                }
                changed = graph_set_node_reference(
                    g,
                    n,
                    target->graph_id,
                    target->name
                );
                assert(!changed);
            }
        }
    }
}
static int behavior_asset_add_call_pin(graph *caller, node *call, int output, const pin_spec *source)
{
    pin_spec pin = *source;
    pin.id = 0;
    pin.declaration_id = source->id;
    /* A signature pin's binding reads inside the function: a Return input
       bound to a local names a value there, not on the caller's node. */
    pin.binding = NULL;
    if (pin.type_name) {
        pin.type_name = graph_own_str(caller, pin.type_name, strlen(pin.type_name));
    }
    pin_value input = pin.default_value;
    if (((pin.container >= PIN_CONTAINER_ARRAY && input.ptr) ||
         (pin.container == PIN_CONTAINER_SINGLE &&
          (pin.type == PIN_STR || pin.type == PIN_NAME))) &&
        pin_value_copy_to_graph(caller, &pin.default_value, &input)) {
        return -1;
    }
    pin.default_value.type_name = pin.type_name;
    return graph_add_pin(caller, call, output, &pin) < 0 ? -1 : 0;
}
static node *behavior_asset_add_call_node(behavior_asset *asset, graph *caller, graph *target, graph_kind kind)
{
    if (!asset || !behavior_asset_mutable(asset) || !caller ||
        caller->owner != asset || !target ||
        (target->owner != asset &&
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
    const node_desc *desc = kind == GRAPH_MACRO ? &NODE_CallMacro
                          : target->math_expression
                              ? &NODE_BehaviorExpression
                              : (pure ? &NODE_CallPureFunction : &NODE_CallFunction);
    node *call = graph_add_node(caller, desc);
    if (!call) {
        return NULL;
    }
    bool named = target->owner != asset && target->owner->transient_function_ids;
    if (graph_set_node_reference(
            caller,
            call,
            named ? 0 : target->graph_id,
            target->name
        ) != 0) {
        graph_remove_node(caller, call);
        return NULL;
    }
    if (kind == GRAPH_MACRO) {
        graph_remove_pin(caller, call, 0, 0);
        graph_remove_pin(caller, call, 1, 0);
    }
    if (target->owner != asset) {
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
    if (named) {
        for (int side = 0; side < 2; side++) {
            pin_spec *pins = side ? call->output_pins : call->input_pins;
            int count = side ? call->output_count : call->input_count;
            for (int i = 0; i < count; i++) {
                pins[i].declaration_id = 0;
            }
        }
    }
    return call;
}
node *behavior_asset_add_function_call_node_to_graph(behavior_asset *asset, graph *caller, graph *function_graph)
{
    return behavior_asset_add_call_node(asset, caller, function_graph, GRAPH_FUNCTION);
}
node *behavior_asset_add_macro_call_node_to_graph(behavior_asset *asset, graph *caller, graph *macro_graph)
{
    return behavior_asset_add_call_node(asset, caller, macro_graph, GRAPH_MACRO);
}
node *behavior_asset_add_function_call_node(behavior_asset *asset, graph *function_graph)
{
    return behavior_asset_add_function_call_node_to_graph(asset, asset ? asset->graph : NULL, function_graph);
}
node *behavior_asset_add_macro_call_node(behavior_asset *asset, graph *macro_graph)
{
    return behavior_asset_add_macro_call_node_to_graph(asset, asset ? asset->graph : NULL, macro_graph);
}
/* Entry, and Return a column to its right so neither hides the other. */
static void add_signature_nodes(graph *g, node **entry, node **ret)
{
    static const float RETURN_COLUMN = 240;
    *entry = graph_add_node(g, &NODE_FunctionEntry);
    *ret = graph_add_node(g, &NODE_Return);
    (*ret)->x = (*entry)->x + RETURN_COLUMN;
    g->entry_id = (*entry)->id;
    g->return_id = (*ret)->id;
}
graph *behavior_asset_add_function(behavior_asset *asset, const char *name, int pure)
{
    graph *g = behavior_asset_add_graph(asset, GRAPH_FUNCTION, name);
    if (!g) {
        return NULL;
    }
    node *entry, *ret;
    add_signature_nodes(g, &entry, &ret);
    g->call_depth_limit = 64;
    if (pure) {
        if (entry->output_count && entry->output_pins[0].kind == PIN_EXEC) {
            graph_remove_pin(g, entry, 1, 0);
        }
        if (ret->input_count && ret->input_pins[0].kind == PIN_EXEC) {
            graph_remove_pin(g, ret, 0, 0);
        }
    }
    return g;
}
graph *behavior_asset_add_macro(behavior_asset *asset, const char *name)
{
    graph *g = behavior_asset_add_graph(asset, GRAPH_MACRO, name);
    if (!g) {
        return NULL;
    }
    node *entry, *ret;
    add_signature_nodes(g, &entry, &ret);
    return g;
}
