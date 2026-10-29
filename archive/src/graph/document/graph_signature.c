#include "graph/document/graph_signature.h"

#include "graph/authoring/graph_edit.h"
#include "graph/authoring/pin_edit.h"
#include "graph/document/library.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "types/array.h"

#include <assert.h>
#include <string.h>

/* Entry declares inputs on its outputs, Return outputs on its inputs; a call
   takes inputs as inputs and gives outputs as outputs. */
static node *signature_node(const graph *g, bool result)
{
    return graph_node_by_id(g, result ? g->return_id : g->entry_id);
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

bool graph_signature_pure(const graph *function)
{
    return !has_exec(signature_node(function, false), 1) &&
           !has_exec(signature_node(function, true), 0);
}

static bool macro(const graph *g)
{
    return g->kind == GRAPH_MACRO;
}

static bool calls(const node *n, const graph *g)
{
    return !n->reference_asset && n->reference_id == g->graph_id &&
           (macro(g) ? n->desc == &NODE_CallMacro
                     : n->desc == &NODE_CallFunction ||
                           n->desc == &NODE_CallPureFunction);
}

/* Every call can follow an edit: its graph is not running and a function
   call's kind matches the function's, so refreshing it cannot be refused. */
static bool editable(const behavior_asset *asset, const graph *g)
{
    if (!behavior_asset_mutable(asset) || g->owner != asset ||
        (g->kind != GRAPH_FUNCTION && !macro(g)) || g->math_expression ||
        g->flow_pause_count || !signature_node(g, false) || !signature_node(g, true)) {
        return false;
    }
    bool pure = !macro(g) && graph_signature_pure(g);
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *caller = asset->graphs[i];
        for (int j = 0; j < acount(caller->nodes); j++) {
            const node *n = caller->nodes[j];
            if (calls(n, g) &&
                (caller->flow_pause_count ||
                 (!macro(g) && (n->desc == &NODE_CallPureFunction) != pure))) {
                return false;
            }
        }
    }
    return true;
}

/* A macro's calls take each edit directly. */
static void refresh_calls(behavior_asset *asset, const graph *g)
{
    if (macro(g)) {
        return;
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *caller = asset->graphs[i];
        for (int j = 0; j < acount(caller->nodes); j++) {
            if (calls(caller->nodes[j], g)) {
                const char *error = graph_library_refresh_call(caller, caller->nodes[j], g);
                assert(!error);
                (void)error;
            }
        }
    }
}

bool graph_signature_declared(const graph *g, const pin_spec *pin)
{
    return (pin->kind == PIN_DATA || (macro(g) && pin->kind == PIN_EXEC)) &&
           !pin->field_source && !pin->split_parent;
}

static bool declaration(const graph *g, node *n, int output, int index)
{
    int count;
    const pin_spec *pins = side_pins(n, output, &count);
    return index >= 0 && index < count && graph_signature_declared(g, &pins[index]);
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
        if (p->kind == declared->kind && !p->field_source &&
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

/* Execution and data wires never mix, so a change of kind drops every wire. */
static void retype_pin(graph *g, node *n, int output, int index, const pin_spec *type)
{
    index = remove_fields(g, n, output, index);
    pin_spec *pin = &(output ? n->output_pins : n->input_pins)[index];
    if (pin->kind != type->kind) {
        graph_break_pin_links(g, n, output, index);
        pin->kind = type->kind;
    }
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
static call_site *call_sites(behavior_asset *asset, const graph *g, bool result, const pin_spec *declared)
{
    call_site *sites = NULL;
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *caller = asset->graphs[i];
        for (int j = 0; j < acount(caller->nodes); j++) {
            node *n = caller->nodes[j];
            int at = calls(n, g) ? call_pin(n, result, declared) : -1;
            if (at >= 0) {
                apush(sites, ((call_site){ caller, n, at }));
            }
        }
    }
    return sites;
}

/* A new macro pin on each call, after its other pins as on the tunnel. */
static void add_macro_call_pins(behavior_asset *asset, const graph *g, bool result, const pin_spec *declared)
{
    pin_spec pin = { .name = declared->name,
                     .kind = declared->kind,
                     .type = declared->type,
                     .container = declared->container,
                     .declaration_id = declared->id };
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *caller = asset->graphs[i];
        for (int j = 0; j < acount(caller->nodes); j++) {
            node *n = caller->nodes[j];
            if (!calls(n, g)) {
                continue;
            }
            int at = graph_add_pin(caller, n, result, &pin);
            graph_set_pin_type(caller, n, result, at, declared->type, declared->type_name);
        }
    }
}

static node *owner_of(behavior_asset *asset, graph *g, bool result)
{
    return editable(asset, g) ? signature_node(g, result) : NULL;
}

int graph_signature_add(behavior_asset *asset, graph *g, bool result, const pin_spec *pin)
{
    node *owner = owner_of(asset, g, result);
    bool exec = pin->kind == PIN_EXEC;
    if (!owner || !pin->name || !pin->name[0] || (exec && !macro(g)) ||
        exec != (pin->type == PIN_FLOW) || name_taken(owner, !result, pin->name, -1)) {
        return -1;
    }
    pin_spec added = { .name = pin->name,
                       .kind = pin->kind,
                       .type = pin->type,
                       .container = exec ? PIN_CONTAINER_SINGLE : pin->container };
    int at = graph_add_pin(g, owner, !result, &added), count;
    graph_set_pin_type(g, owner, !result, at, pin->type, pin->type_name);
    if (macro(g)) {
        add_macro_call_pins(asset, g, result, &side_pins(owner, !result, &count)[at]);
    }
    refresh_calls(asset, g);
    return 0;
}

int graph_signature_remove(behavior_asset *asset, graph *g, bool result, int index)
{
    node *owner = owner_of(asset, g, result);
    if (!owner || !declaration(g, owner, !result, index)) {
        return -1;
    }
    int count;
    pin_spec declared = side_pins(owner, !result, &count)[index];
    call_site *sites = call_sites(asset, g, result, &declared);
    for (int i = 0; i < acount(sites); i++) {
        remove_pin(sites[i].caller, sites[i].call, result, sites[i].pin);
    }
    afree(sites);
    remove_pin(g, owner, !result, index);
    refresh_calls(asset, g);
    return 0;
}

int graph_signature_rename(behavior_asset *asset, graph *g, bool result, int index, const char *name)
{
    node *owner = owner_of(asset, g, result);
    if (!owner || !declaration(g, owner, !result, index) || !name[0] ||
        name_taken(owner, !result, name, index)) {
        return -1;
    }
    int count;
    pin_spec declared = side_pins(owner, !result, &count)[index];
    /* A call pin read by name keeps following its declaration once renamed. */
    call_site *sites = call_sites(asset, g, result, &declared);
    for (int i = 0; i < acount(sites); i++) {
        side_pins(sites[i].call, result, &count)[sites[i].pin]
            .declaration_id = declared.id;
        graph_set_pin_name(sites[i].caller, sites[i].call, result, sites[i].pin, name);
    }
    afree(sites);
    graph_set_pin_name(g, owner, !result, index, name);
    refresh_calls(asset, g);
    return 0;
}

/* The pin of `n` that follows `declared` moves to where the one following
   `target` is. */
static void follow_move(graph *g, node *n, int output, const pin_spec *declared, const pin_spec *target)
{
    int from = call_pin(n, output, declared), to = call_pin(n, output, target);
    if (from >= 0 && to >= 0) {
        graph_move_pin(g, n, output, from, to);
    }
}

int graph_signature_move(behavior_asset *asset, graph *g, bool result, int from, int to)
{
    node *owner = owner_of(asset, g, result);
    if (!owner || !declaration(g, owner, !result, from) ||
        !declaration(g, owner, !result, to)) {
        return -1;
    }
    int count;
    const pin_spec *pins = side_pins(owner, !result, &count);
    pin_spec declared = pins[from], target = pins[to];
    graph_move_pin(g, owner, !result, from, to);
    for (int i = 0; result && i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        if (n->desc == &NODE_Return && n != owner) {
            follow_move(g, n, 0, &declared, &target);
        }
    }
    for (int i = 0; macro(g) && i < acount(asset->graphs); i++) {
        graph *caller = asset->graphs[i];
        for (int j = 0; j < acount(caller->nodes); j++) {
            if (calls(caller->nodes[j], g)) {
                follow_move(caller, caller->nodes[j], result, &declared, &target);
            }
        }
    }
    refresh_calls(asset, g);
    return 0;
}

int graph_signature_retype(behavior_asset *asset, graph *g, bool result, int index, const pin_spec *type)
{
    node *owner = owner_of(asset, g, result);
    bool exec = type->kind == PIN_EXEC;
    if (!owner || !declaration(g, owner, !result, index) ||
        (exec && !macro(g)) || exec != (type->type == PIN_FLOW)) {
        return -1;
    }
    pin_spec kind = *type;
    kind.container = exec ? PIN_CONTAINER_SINGLE : type->container;
    int count;
    pin_spec declared = side_pins(owner, !result, &count)[index];
    call_site *sites = call_sites(asset, g, result, &declared);
    for (int i = 0; i < acount(sites); i++) {
        retype_pin(sites[i].caller, sites[i].call, result, sites[i].pin, &kind);
    }
    afree(sites);
    retype_pin(g, owner, !result, index, &kind);
    refresh_calls(asset, g);
    return 0;
}

int graph_signature_set_default(behavior_asset *asset, graph *g, int index, const pin_value *value)
{
    node *entry = owner_of(asset, g, false);
    int count;
    pin_spec *pins = entry ? side_pins(entry, 1, &count) : NULL;
    if (!entry || !declaration(g, entry, 1, index) ||
        pins[index].kind != PIN_DATA || value->type != pins[index].type) {
        return -1;
    }
    pin_spec declared = pins[index];
    call_site *sites = call_sites(asset, g, false, &declared);
    for (int i = 0; i < acount(sites); i++) {
        const pin_spec *pin = &sites[i].call->input_pins[sites[i].pin];
        if (pin_value_equal(&pin->default_value, &declared.default_value)) {
            graph_set_pin_default(sites[i].caller, sites[i].call, 0, sites[i].pin, value);
        }
    }
    afree(sites);
    return graph_set_pin_default(g, entry, 1, index, value);
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

int graph_signature_set_pure(behavior_asset *asset, graph *function, bool pure)
{
    if (!editable(asset, function) || macro(function) ||
        (pure && body_executes(function)) ||
        (!pure && behavior_domain_pure(asset->domain))) {
        return -1;
    }
    if (graph_signature_pure(function) == pure) {
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
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *caller = asset->graphs[i];
        for (int j = 0; j < acount(caller->nodes); j++) {
            node *n = caller->nodes[j];
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

const pin_spec *graph_signature_call_input(const behavior_asset *asset, const node *call, int input)
{
    const graph *g = call->reference_asset
                       ? NULL
                       : behavior_asset_find_graph(asset, call->reference_id);
    if (!g || !calls(call, g)) {
        return NULL;
    }
    node *entry = signature_node(g, false);
    for (int i = 0; i < entry->output_count; i++) {
        if (call_pin((node *)call, 0, &entry->output_pins[i]) == input) {
            return &entry->output_pins[i];
        }
    }
    return NULL;
}
