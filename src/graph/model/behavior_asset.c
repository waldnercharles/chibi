#include "graph/model/behavior_asset.h"

#include "graph/compiler/behavior_program.h"
#include "graph/model/registry.h"
#include "graph/runtime/value_array.h"
#include "support/xalloc.h"

#include <stdatomic.h>
#include <string.h>

static atomic_ullong behavior_asset_identity = 1;

const char *behavior_domain_name(behavior_domain domain)
{
    switch (domain) {
        case BEHAVIOR_DOMAIN_FIELD: return "field";
        case BEHAVIOR_DOMAIN_EVENT: return "behavior";
        case BEHAVIOR_DOMAIN_LIBRARY: return "library";
        case BEHAVIOR_DOMAIN_MATERIAL: return "material";
        case BEHAVIOR_DOMAIN_SHAPE: return "shape";
        case BEHAVIOR_DOMAIN_EVENT_DEFINITION: return "event";
        case BEHAVIOR_DOMAIN_SHADER_LIBRARY: return "shader-library";
    }
    return "invalid";
}

bool behavior_domain_pure(behavior_domain domain)
{
    return domain == BEHAVIOR_DOMAIN_MATERIAL || domain == BEHAVIOR_DOMAIN_SHAPE ||
           domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY ||
           domain == BEHAVIOR_DOMAIN_FIELD;
}

static int var_name_unique(const behavior_asset *asset, const char *name, uint64_t except_id)
{
    for (int i = 0; i < acount(asset->variables); i++) {
        if (asset->variables[i].id != except_id &&
            strcmp(asset->variables[i].name, name) == 0) {
            return 0;
        }
    }
    return 1;
}

behavior_asset *behavior_asset_new(const char *name)
{
    behavior_asset *asset = (behavior_asset *)xcalloc(1, sizeof *asset);
    asset->identity = atomic_fetch_add_explicit(&behavior_asset_identity, 1, memory_order_relaxed);
    if (!asset->identity) {
        asset->identity = atomic_fetch_add_explicit(&behavior_asset_identity, 1, memory_order_relaxed);
    }
    asset->graph = (graph *)xcalloc(1, sizeof *asset->graph);
    graph_init(asset->graph);
    asset->graph->owner = asset;
    asset->graph->graph_id = 0;
    asset->graph->shared_id_counter = &asset->graph->id_counter;
    apush(asset->graphs, asset->graph);
    asset->name = graph_own_str(asset->graph, name ? name : "", name ? strlen(name) : 0);
    asset->description = graph_own_str(asset->graph, "", 0);
    return asset;
}

void behavior_asset_free(behavior_asset *asset)
{
    if (!asset) {
        return;
    }
    afree(asset->variables);
    behavior_program_free(asset->program);
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph_free(asset->graphs[i]);
        free(asset->graphs[i]);
    }
    afree(asset->graphs);
    free(asset);
}

graph *behavior_asset_add_graph(behavior_asset *asset, graph_kind kind, const char *name)
{
    if (!asset || !behavior_asset_mutable(asset) || !name || !name[0]) {
        return NULL;
    }
    if (behavior_asset_find_graph_by_name(asset, name)) {
        return NULL;
    }
    graph *g = (graph *)xcalloc(1, sizeof *g);
    graph_init(g);
    g->owner = asset;
    g->kind = kind;
    g->graph_id = graph_alloc_id(asset->graph);
    g->shared_id_counter = &asset->graph->id_counter;
    g->id_counter = asset->graph->id_counter;
    g->name = graph_own_str(g, name, strlen(name));
    g->description = graph_own_str(g, "", 0);
    apush(asset->graphs, g);
    return g;
}

graph *behavior_asset_find_graph(const behavior_asset *asset, uint64_t graph_id)
{
    if (!asset) {
        return NULL;
    }
    if (graph_id == 0) {
        return asset->graph;
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        if (asset->graphs[i]->graph_id == graph_id) {
            return asset->graphs[i];
        }
    }
    return NULL;
}

graph *behavior_asset_find_graph_by_name(const behavior_asset *asset, const char *name)
{
    if (!asset || !name) {
        return NULL;
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        if (asset->graphs[i]->name && strcmp(asset->graphs[i]->name, name) == 0) {
            return asset->graphs[i];
        }
    }
    return NULL;
}

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

int behavior_asset_delete_graph(behavior_asset *asset, uint64_t graph_id)
{
    if (!asset || !behavior_asset_mutable(asset) || graph_id == 0) {
        return -1;
    }
    for (int i = 1; i < acount(asset->graphs); i++) {
        if (asset->graphs[i]->graph_id == graph_id) {
            graph *g = asset->graphs[i];
            adel(asset->graphs, i);
            graph_free(g);
            free(g);
            return 0;
        }
    }
    return -1;
}

int behavior_asset_mutable(const behavior_asset *asset)
{
    return asset && asset->graph && asset->graph->flow_pause_count == 0;
}

int behavior_asset_set_name(behavior_asset *asset, const char *name)
{
    if (!asset || !behavior_asset_mutable(asset) || !name) {
        return -1;
    }
    const char *old_name = asset->name;
    char *copy = graph_own_str(asset->graph, name, strlen(name));
    if (!copy) {
        return -1;
    }
    asset->name = copy;
    for (int g = 0; g < acount(asset->graphs); g++) {
        graph *target = asset->graphs[g];
        for (int i = 0; i < acount(target->nodes); i++) {
            node *n = target->nodes[i];
            for (int p = 0; p < n->input_count; p++) {
                const pin_binding *binding = n->input_pins[p].binding;
                if (!binding || binding->kind != PIN_BINDING_VARIABLE ||
                    (binding->source_asset && old_name &&
                     strcmp(binding->source_asset, old_name) != 0)) {
                    continue;
                }
                pin_binding updated = *binding;
                updated.source_asset = copy;
                graph_set_pin_binding(target, n, 0, p, &updated);
            }
        }
    }
    return 0;
}

void behavior_asset_set_description(behavior_asset *asset, const char *description)
{
    if (!asset) {
        return;
    }
    char *copy = graph_own_str(
        asset->graph,
        description ? description : "",
        description ? strlen(description) : 0
    );
    if (copy) {
        asset->description = copy;
    }
}

int behavior_asset_set_owner_type(behavior_asset *asset, const char *type_name)
{
    if (!asset || !behavior_asset_mutable(asset) || !type_name || !type_name[0]) {
        return -1;
    }
    char *copy = graph_own_str(asset->graph, type_name, strlen(type_name));
    if (!copy) {
        return -1;
    }
    asset->owner_type_name = copy;
    return 0;
}

behavior_variable *behavior_asset_add_variable(
    behavior_asset *asset,
    const char *name,
    pin_type type,
    const char *type_name,
    pin_container container,
    const pin_value *default_value,
    const char *category
)
{
    if (!asset || !behavior_asset_mutable(asset) || !name || !name[0]) {
        return NULL;
    }
    if (!var_name_unique(asset, name, 0)) {
        return NULL;
    }
    behavior_variable v = { 0 };
    v.id = graph_alloc_id(asset->graph);
    v.name = graph_own_str(asset->graph, name, strlen(name));
    v.type = type;
    v.type_name = type_name
                    ? graph_own_str(asset->graph, type_name, strlen(type_name))
                    : NULL;
    v.container = container;
    v.category = category && category[0]
                   ? graph_own_str(asset->graph, category, strlen(category))
                   : NULL;
    if (default_value) {
        v.default_value = *default_value;
        v.default_value.type = type;
        v.default_value.container = container;
        v.default_value.type_name = v.type_name;
        if (container >= PIN_CONTAINER_ARRAY && default_value->ptr) {
            v.default_value.ptr = pin_array_clone_to_graph(
                asset->graph,
                (const pin_array *)default_value->ptr
            );
            if (!v.default_value.ptr) {
                return NULL;
            }
        }
        if (container == PIN_CONTAINER_SINGLE &&
            (type == PIN_STR || type == PIN_NAME) && v.default_value.str) {
            v.default_value.str = graph_own_str(
                asset->graph,
                v.default_value.str,
                strlen(v.default_value.str)
            );
        }
    } else {
        v.default_value = (pin_value){ .type = type,
                                       .container = container,
                                       .type_name = v.type_name };
        if (container == PIN_CONTAINER_SINGLE && (type == PIN_STR || type == PIN_NAME)) {
            v.default_value.str = "";
        }
    }
    apush(asset->variables, v);
    return &asset->variables[acount(asset->variables) - 1];
}

behavior_variable *behavior_asset_find_variable_by_name(const behavior_asset *asset, const char *name)
{
    if (!asset || !name) {
        return NULL;
    }
    for (int i = 0; i < acount(asset->variables); i++) {
        if (strcmp(asset->variables[i].name, name) == 0) {
            return &asset->variables[i];
        }
    }
    return NULL;
}

behavior_variable *behavior_asset_find_variable_by_id(const behavior_asset *asset, uint64_t id)
{
    if (!asset) {
        return NULL;
    }
    for (int i = 0; i < acount(asset->variables); i++) {
        if (asset->variables[i].id == id) {
            return &asset->variables[i];
        }
    }
    return NULL;
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
    if (!v || !var_name_unique(asset, new_name, id)) {
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
        container < PIN_CONTAINER_SINGLE || container > PIN_CONTAINER_MAP) {
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
        if (container == PIN_CONTAINER_SINGLE &&
            (v->type == PIN_STR || v->type == PIN_NAME)) {
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

int behavior_asset_set_variable_category(behavior_asset *asset, uint64_t id, const char *category)
{
    if (!asset || !behavior_asset_mutable(asset) || !category) {
        return -1;
    }
    behavior_variable *v = behavior_asset_find_variable_by_id(asset, id);
    if (!v) {
        return -1;
    }
    v->category = graph_own_str(asset->graph, category, strlen(category));
    return v->category ? 0 : -1;
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
    } else if (access) {
        graph_connect_data(g, access, 0, n, index);
    }
    return v;
}

int behavior_asset_delete_variable(behavior_asset *asset, uint64_t id)
{
    if (!asset || !behavior_asset_mutable(asset)) {
        return -1;
    }
    for (int i = 0; i < acount(asset->variables); i++) {
        if (asset->variables[i].id == id) {
            /* VAR-7: referencing nodes keep their reference ID and become
             * orphaned; evaluation reports the lost variable. */
            adel(asset->variables, i);
            return 0;
        }
    }
    return -1;
}

int behavior_asset_set_variable_default(behavior_asset *asset, uint64_t id, const pin_value *value)
{
    if (!asset || !behavior_asset_mutable(asset) || !value) {
        return -1;
    }
    behavior_variable *v = behavior_asset_find_variable_by_id(asset, id);
    if (!v || value->type != v->type) {
        return -1;
    }
    pin_value source = *value;
    source.container = v->container;
    source.type_name = v->type_name;
    pin_value owned = source;
    /* A vector or other struct is copied, as pin defaults are. */
    if (source.container == PIN_CONTAINER_SINGLE && source.type == PIN_STRUCT &&
        source.ptr && pin_value_copy_to_graph(asset->graph, &owned, &source)) {
        return -1;
    }
    v->default_value = owned;
    v->default_value.type_name = v->type_name;
    if (v->container >= PIN_CONTAINER_ARRAY && value->ptr) {
        v->default_value.ptr = pin_array_clone_to_graph(
            asset->graph,
            (const pin_array *)value->ptr
        );
    }
    if (v->container == PIN_CONTAINER_SINGLE &&
        (value->type == PIN_STR || value->type == PIN_NAME) && value->str) {
        v->default_value
            .str = graph_own_str(asset->graph, value->str, strlen(value->str));
    }
    return 0;
}

behavior_variable *behavior_asset_duplicate_variable(behavior_asset *asset, uint64_t id, const char *name)
{
    behavior_variable *v = behavior_asset_find_variable_by_id(asset, id);
    if (!v) {
        return NULL;
    }
    /* Adding may move the variables, and with them v. */
    behavior_variable source = *v;
    return behavior_asset_add_variable(
        asset,
        name,
        source.type,
        source.type_name,
        source.container,
        &source.default_value,
        source.category
    );
}

static bool variable_node(const node *n, uint64_t id)
{
    return n->reference_id == id &&
           (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable);
}

int behavior_asset_remove_variable_uses(behavior_asset *asset, uint64_t id)
{
    int removed = 0;
    for (int g = 0; g < acount(asset->graphs); g++) {
        graph *target = asset->graphs[g];
        for (int i = acount(target->nodes) - 1; i >= 0; i--) {
            node *n = target->nodes[i];
            if (variable_node(n, id)) {
                removed += graph_remove_node(target, n) == 0;
                continue;
            }
            for (int p = 0; p < n->input_count; p++) {
                const pin_binding *binding = n->input_pins[p].binding;
                if (binding && binding->kind == PIN_BINDING_VARIABLE &&
                    binding->variable_id == id &&
                    (!binding->source_asset ||
                     !strcmp(binding->source_asset, asset->name))) {
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
    if (graph_set_node_reference(caller, call, target->graph_id, target->name) != 0) {
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
