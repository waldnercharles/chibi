#include "graph/document/function_signature.h"

#include "graph/document/library.h"
#include "graph/model/registry.h"

#include <assert.h>
#include <string.h>

/* Function Entry declares parameters on its outputs, Return results on its
   inputs; a call takes parameters as inputs and gives results as outputs. */
static node *signature_node(const graph *function, bool result)
{
    return graph_node_by_id(function, result ? function->return_id : function->entry_id);
}

static pin_spec *side_pins(node *n, int output, int *count)
{
    *count = output ? n->output_count : n->input_count;
    return output ? n->output_pins : n->input_pins;
}

static bool has_exec(node *n, int output)
{
    int count;
    const pin_spec *pins = side_pins(n, output, &count);
    for (int i = 0; i < count; i++) {
        if (pins[i].kind == PIN_EXEC) {
            return true;
        }
    }
    return false;
}

bool function_signature_pure(const graph *function)
{
    return !has_exec(signature_node(function, false), 1) &&
           !has_exec(signature_node(function, true), 0);
}

static bool calls(const node *n, const graph *function)
{
    return !n->reference_asset && n->reference_id == function->graph_id &&
           (n->desc == &NODE_CallFunction || n->desc == &NODE_CallPureFunction);
}

/* Every call can follow an edit: its graph is not running and its kind
   matches the function's, so refreshing it cannot be refused. */
static bool editable(const behavior_asset *asset, const graph *function)
{
    if (!behavior_asset_mutable(asset) || function->owner != asset ||
        function->kind != GRAPH_FUNCTION || function->math_expression ||
        function->flow_pause_count || !signature_node(function, false) ||
        !signature_node(function, true)) {
        return false;
    }
    bool pure = function_signature_pure(function);
    for (int g = 0; g < acount(asset->graphs); g++) {
        const graph *caller = asset->graphs[g];
        for (int i = 0; i < acount(caller->nodes); i++) {
            const node *n = caller->nodes[i];
            if (calls(n, function) && (caller->flow_pause_count ||
                                       (n->desc == &NODE_CallPureFunction) != pure)) {
                return false;
            }
        }
    }
    return true;
}

static void refresh_calls(behavior_asset *asset, const graph *function)
{
    for (int g = 0; g < acount(asset->graphs); g++) {
        graph *caller = asset->graphs[g];
        for (int i = 0; i < acount(caller->nodes); i++) {
            if (calls(caller->nodes[i], function)) {
                const char *error = graph_library_refresh_call(caller, caller->nodes[i], function);
                assert(!error);
                (void)error;
            }
        }
    }
}

bool function_signature_declared(const pin_spec *pin)
{
    return pin->kind == PIN_DATA && !pin->field_source && !pin->split_parent;
}

static bool declaration(node *n, int output, int index)
{
    int count;
    const pin_spec *pins = side_pins(n, output, &count);
    return index >= 0 && index < count && function_signature_declared(&pins[index]);
}

static bool name_taken(node *n, int output, const char *name, int except)
{
    int count;
    const pin_spec *pins = side_pins(n, output, &count);
    for (int i = 0; i < count; i++) {
        if (i != except && pins[i].name && !strcmp(pins[i].name, name)) {
            return true;
        }
    }
    return false;
}

/* The call pin that follows `declared`, matched as graph_library_refresh_call
   matches it. */
static int call_pin(node *call, int output, const pin_spec *declared)
{
    int count;
    const pin_spec *pins = side_pins(call, output, &count);
    for (int i = 0; i < count; i++) {
        const pin_spec *p = &pins[i];
        if (p->kind == PIN_DATA && !p->field_source &&
            (p->declaration_id
                 ? p->declaration_id == declared->id
                 : p->name && declared->name && !strcmp(p->name, declared->name))) {
            return i;
        }
    }
    return -1;
}

/* Removes the fields a pin exposes; returns the pin's index afterwards. */
static int remove_fields(graph *g, node *n, int output, int index)
{
    uint64_t id = output ? n->output_pins[index].id : 0;
    for (int i = n->output_count - 1; id && i >= 0; i--) {
        if (n->output_pins[i].field_source == id) {
            int removed = graph_remove_pin(g, n, 1, i);
            assert(removed == 0);
            (void)removed;
            index -= i < index;
        }
    }
    return index;
}

static void remove_pin(graph *g, node *n, int output, int index)
{
    int removed = graph_remove_pin(g, n, output, remove_fields(g, n, output, index));
    assert(removed == 0);
    (void)removed;
}

static void retype_pin(graph *g, node *n, int output, int index, const pin_spec *type)
{
    index = remove_fields(g, n, output, index);
    /* The container first: retyping drops the wires the new pin can't carry. */
    graph_set_pin_container(g, n, output, index, type->container);
    graph_set_pin_type(g, n, output, index, type->type, type->type_name);
}

typedef struct
{
    graph *caller;
    node *call;
    int pin;
} call_site;

/* Every call pin that follows `declared`. */
static call_site *call_sites(behavior_asset *asset, const graph *function, bool result, const pin_spec *declared)
{
    call_site *sites = NULL;
    for (int g = 0; g < acount(asset->graphs); g++) {
        graph *caller = asset->graphs[g];
        for (int i = 0; i < acount(caller->nodes); i++) {
            node *n = caller->nodes[i];
            int at = calls(n, function) ? call_pin(n, result, declared) : -1;
            if (at >= 0) {
                apush(sites, ((call_site){ caller, n, at }));
            }
        }
    }
    return sites;
}

int function_signature_add(behavior_asset *asset, graph *function, bool result, const pin_spec *pin)
{
    node *owner = editable(asset, function) ? signature_node(function, result) : NULL;
    if (!owner || !pin->name || !pin->name[0] || pin->type == PIN_FLOW ||
        name_taken(owner, !result, pin->name, -1)) {
        return -1;
    }
    pin_spec added = { .name = pin->name,
                       .type = pin->type,
                       .type_name = pin->type_name,
                       .kind = PIN_DATA,
                       .container = pin->container };
    if (graph_add_pin(function, owner, !result, &added) < 0) {
        return -1;
    }
    refresh_calls(asset, function);
    return 0;
}

int function_signature_remove(behavior_asset *asset, graph *function, bool result, int index)
{
    node *owner = editable(asset, function) ? signature_node(function, result) : NULL;
    if (!owner || !declaration(owner, !result, index)) {
        return -1;
    }
    int count;
    pin_spec declared = side_pins(owner, !result, &count)[index];
    call_site *sites = call_sites(asset, function, result, &declared);
    for (int i = 0; i < acount(sites); i++) {
        remove_pin(sites[i].caller, sites[i].call, result, sites[i].pin);
    }
    afree(sites);
    remove_pin(function, owner, !result, index);
    refresh_calls(asset, function);
    return 0;
}

int function_signature_rename(behavior_asset *asset, graph *function, bool result, int index, const char *name)
{
    node *owner = editable(asset, function) ? signature_node(function, result) : NULL;
    if (!owner || !declaration(owner, !result, index) || !name[0] ||
        name_taken(owner, !result, name, index)) {
        return -1;
    }
    int count;
    pin_spec declared = side_pins(owner, !result, &count)[index];
    /* A call pin read by name keeps following its declaration once renamed. */
    call_site *sites = call_sites(asset, function, result, &declared);
    for (int i = 0; i < acount(sites); i++) {
        side_pins(sites[i].call, result, &count)[sites[i].pin]
            .declaration_id = declared.id;
    }
    afree(sites);
    graph_set_pin_name(function, owner, !result, index, name);
    refresh_calls(asset, function);
    return 0;
}

int function_signature_retype(behavior_asset *asset, graph *function, bool result, int index, const pin_spec *type)
{
    node *owner = editable(asset, function) ? signature_node(function, result) : NULL;
    if (!owner || !declaration(owner, !result, index) || type->type == PIN_FLOW) {
        return -1;
    }
    int count;
    pin_spec declared = side_pins(owner, !result, &count)[index];
    call_site *sites = call_sites(asset, function, result, &declared);
    for (int i = 0; i < acount(sites); i++) {
        retype_pin(sites[i].caller, sites[i].call, result, sites[i].pin, type);
    }
    afree(sites);
    retype_pin(function, owner, !result, index, type);
    refresh_calls(asset, function);
    return 0;
}

/* ---------- Purity ---------- */

static void add_exec_pin(graph *g, node *n, int output, const char *name)
{
    pin_spec pin = { .name = name, .type = PIN_FLOW, .kind = PIN_EXEC };
    int moved = graph_move_pin(g, n, output, graph_add_pin(g, n, output, &pin), 0);
    assert(moved == 0);
    (void)moved;
}

static void remove_exec_pins(graph *g, node *n, int output)
{
    for (int i = (output ? n->output_count : n->input_count) - 1; i >= 0; i--) {
        const pin_spec *pins = output ? n->output_pins : n->input_pins;
        if (pins[i].kind == PIN_EXEC) {
            remove_pin(g, n, output, i);
        }
    }
}

/* Nodes other than Entry and Return that run in order. */
static bool body_executes(const graph *function)
{
    for (int i = 0; i < acount(function->nodes); i++) {
        node *n = function->nodes[i];
        if (n->id != function->entry_id && n->id != function->return_id &&
            (has_exec(n, 0) || has_exec(n, 1))) {
            return true;
        }
    }
    return false;
}

typedef struct
{
    uint64_t node, pin;
    int order;
} exec_end;

/* A call becoming pure hands its execution on: whatever ran it now runs
   what followed it. */
static void make_call_pure(graph *g, node *call)
{
    exec_end *sources = NULL;
    exec_end next = { 0 };
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind == GRAPH_EDGE_EXEC && e->to_id == call->id) {
            apush(sources, ((exec_end){ e->from_id, e->from_pin_id, e->exec_order }));
        } else if (e->kind == GRAPH_EDGE_EXEC && e->from_id == call->id) {
            next = (exec_end){ e->to_id, e->to_pin_id, e->exec_order };
        }
    }
    remove_exec_pins(g, call, 0);
    remove_exec_pins(g, call, 1);
    call->desc = &NODE_CallPureFunction;
    for (int i = 0; next.node && i < acount(sources); i++) {
        graph_connect_exec_ids(
            g,
            sources[i].node,
            sources[i].pin,
            next.node,
            next.pin,
            sources[i].order
        );
    }
    afree(sources);
}

static void make_call_impure(graph *g, node *call)
{
    call->desc = &NODE_CallFunction;
    add_exec_pin(g, call, 0, "Exec");
    add_exec_pin(g, call, 1, "Then");
}

int function_signature_set_pure(behavior_asset *asset, graph *function, bool pure)
{
    if (!editable(asset, function) || (pure && body_executes(function))) {
        return -1;
    }
    if (function_signature_pure(function) == pure) {
        return 0;
    }
    node *entry = signature_node(function, false);
    node *ret = signature_node(function, true);
    if (pure) {
        remove_exec_pins(function, entry, 1);
        remove_exec_pins(function, ret, 0);
    } else {
        add_exec_pin(function, entry, 1, "Exec");
        add_exec_pin(function, ret, 0, "Exec");
        graph_connect_exec(function, entry, 0, ret, 0, 0);
    }
    for (int g = 0; g < acount(asset->graphs); g++) {
        graph *caller = asset->graphs[g];
        for (int i = 0; i < acount(caller->nodes); i++) {
            node *n = caller->nodes[i];
            if (!calls(n, function)) {
                continue;
            }
            if (pure) {
                make_call_pure(caller, n);
            } else {
                make_call_impure(caller, n);
            }
        }
    }
    refresh_calls(asset, function);
    return 0;
}
