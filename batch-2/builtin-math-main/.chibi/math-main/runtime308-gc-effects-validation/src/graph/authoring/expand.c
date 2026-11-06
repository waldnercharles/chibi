#include "graph/authoring/expand.h"

#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *const STALE = "Refresh the call first.";

typedef struct
{
    graph *caller, *target;
    node *call, *entry, *ret;
    node **body; /* the called graph's nodes but Function Entry and Return, by ID */
    node **copies; /* their copies in the caller, in the same order */
    edge *outside; /* the call's wires */
    edge *inside;  /* wires leaving Function Entry or reaching Return */
} expansion;

/* Pins a call and its tunnels carry: neither split parts nor exposed fields. */
static bool carried(const pin_spec *pin)
{
    return !pin->split_parent && !pin->field_source;
}

/* The place of pins[index] among the carried pins of its kind. */
static int ordinal_of(const pin_spec *pins, int index)
{
    int ordinal = 0;
    for (int i = 0; i < index; i++) {
        ordinal += carried(&pins[i]) && pins[i].kind == pins[index].kind;
    }
    return ordinal;
}

/* The carried pin of `kind` at `ordinal`, or -1. */
static int nth_pin(const pin_spec *pins, int count, pin_kind kind, int ordinal)
{
    for (int i = 0; i < count; i++) {
        if (carried(&pins[i]) && pins[i].kind == kind && ordinal-- == 0) {
            return i;
        }
    }
    return -1;
}

static int pin_at(const node *n, uint64_t pin_id)
{
    int index = -1;
    bool found = graph_pin_by_id(n, pin_id, NULL, &index);
    assert(found);
    (void)found;
    return index;
}

static bool split(const node *n)
{
    for (int i = 0; i < n->input_count; i++) {
        if (!carried(&n->input_pins[i])) {
            return true;
        }
    }
    for (int i = 0; i < n->output_count; i++) {
        if (!carried(&n->output_pins[i])) {
            return true;
        }
    }
    return false;
}

static int return_count(const graph *g)
{
    int count = 0;
    for (int i = 0; i < acount(g->nodes); i++) {
        count += g->nodes[i]->desc == &NODE_Return;
    }
    return count;
}

/* Whether execution can end in the body without reaching Return. */
static bool open_exec(const graph *g)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        for (int p = 0; p < n->output_count; p++) {
            const edge *first = NULL;
            if (n->output_pins[p].kind == PIN_EXEC &&
                !graph_exec_output_edges(g, n, p, &first)) {
                return true;
            }
        }
    }
    return false;
}

const char *behavior_asset_can_expand_call(
    const behavior_asset *asset,
    const graph *caller,
    const node *call
)
{
    assert(caller->owner == asset && graph_node_by_id(caller, call->id) == call);
    if (call->desc != &NODE_CallFunction &&
        call->desc != &NODE_CallPureFunction && call->desc != &NODE_CallMacro) {
        return "Choose a function or macro call.";
    }
    const graph *target = call->reference_asset
                            ? NULL
                            : behavior_asset_find_graph(asset, call->reference_id);
    if (!target || target->math_expression) {
        return "Choose a call to this behavior's own function or macro.";
    }
    if (target == caller) {
        return "A graph cannot expand a call to itself.";
    }
    const node *entry = graph_node_by_id(target, target->entry_id);
    const node *ret = graph_node_by_id(target, target->return_id);
    if (!entry || !ret || return_count(target) != 1) {
        return "Expand a graph with one Function Entry and one Return.";
    }
    if (split(call) || split(entry) || split(ret)) {
        return "Recombine split pins first.";
    }
    if (call->desc == &NODE_CallFunction && open_exec(target)) {
        return "Wire every execution output in the function first.";
    }
    return NULL;
}

static int compare_ids(const void *a, const void *b)
{
    uint64_t x = (*(node *const *)a)->id, y = (*(node *const *)b)->id;
    return x < y ? -1 : x > y;
}

static node *copy_of(const expansion *x, uint64_t id)
{
    for (int i = 0; i < acount(x->body); i++) {
        if (x->body[i]->id == id) {
            return x->copies[i];
        }
    }
    assert(!"a body node");
    return NULL;
}

/* The index of a body node's pin, which its copy shares. */
static int body_pin(const expansion *x, uint64_t node_id, uint64_t pin_id)
{
    return pin_at(graph_node_by_id(x->target, node_id), pin_id);
}

/* Copies the body with its top-left corner at the call, keeping its layout;
   appended nodes keep the ID order they were written in. */
static const char *copy_body(expansion *x)
{
    for (int i = 0; i < acount(x->target->nodes); i++) {
        node *n = x->target->nodes[i];
        if (n != x->entry && n != x->ret) {
            apush(x->body, n);
        }
    }
    int count = acount(x->body);
    if (!count) {
        return NULL;
    }
    qsort(x->body, (size_t)count, sizeof *x->body, compare_ids);
    float left = INFINITY, top = INFINITY;
    for (int i = 0; i < count; i++) {
        left = fminf(left, x->body[i]->x);
        top = fminf(top, x->body[i]->y);
    }
    int base = acount(x->caller->nodes);
    av_doc *doc = av_doc_new();
    av_value *body = graph_nodes_to_value(doc, x->target, x->body, count);
    int appended = body
                     ? graph_append_value(x->caller, body, x->call->x - left, x->call->y - top)
                     : -1;
    av_doc_free(doc);
    if (appended != 0 || acount(x->caller->nodes) != base + count) {
        return "Could not copy the called graph.";
    }
    for (int i = 0; i < count; i++) {
        apush(x->copies, x->caller->nodes[base + i]);
    }
    return NULL;
}

/* Takes the call's wires out, keeping them to rewire. */
static void detach(expansion *x)
{
    graph *g = x->caller;
    for (int i = 0; i < acount(g->edges); i++) {
        if (g->edges[i].from_id == x->call->id || g->edges[i].to_id == x->call->id) {
            apush(x->outside, g->edges[i]);
        }
    }
    for (int i = 0; i < acount(x->outside); i++) {
        const edge *e = &x->outside[i];
        node *from = graph_node_by_id(g, e->from_id),
             *to = graph_node_by_id(g, e->to_id);
        int removed = graph_remove_edge(g, from, pin_at(from, e->from_pin_id), to, pin_at(to, e->to_pin_id));
        assert(removed == 0);
        (void)removed;
    }
    const graph *t = x->target;
    for (int i = 0; i < acount(t->edges); i++) {
        if (t->edges[i].from_id == x->entry->id || t->edges[i].to_id == x->ret->id) {
            apush(x->inside, t->edges[i]);
        }
    }
}

/* One conversion at most survives joining two wires. */
static const char *join(
    graph *g,
    node *from,
    int from_pin,
    node *to,
    int to_pin,
    const node_desc *a,
    const node_desc *b
)
{
    if (a && b) {
        return "Wire the converted values past the call first.";
    }
    return graph_connect_data_conversion(g, from, from_pin, to, to_pin, a ? a : b)
             ? "Could not preserve a connection."
             : NULL;
}

/* An unwired pin's value or binding moves to the input it fed. */
static const char *give_value(graph *g, node *n, int pin, const pin_spec *from, const node_desc *conversion)
{
    const pin_spec *to = &n->input_pins[pin];
    if (conversion || to->type != from->type || to->container != from->container ||
        strcmp(to->type_name ? to->type_name : "", from->type_name ? from->type_name : "")) {
        return "Wire the converted values past the call first.";
    }
    int rc = from->binding && from->binding->kind != PIN_BINDING_LOCAL
               ? graph_set_pin_binding(g, n, 0, pin, from->binding)
               : graph_set_pin_default(g, n, 0, pin, &from->default_value);
    return rc ? "Could not preserve an input." : NULL;
}

/* Whatever fed the call's data input `pin` now feeds to.to_pin. */
static const char *feed(const expansion *x, int pin, node *to, int to_pin, const node_desc *conversion)
{
    if (pin < 0) {
        return STALE;
    }
    uint64_t id = x->call->input_pins[pin].id;
    for (int i = 0; i < acount(x->outside); i++) {
        const edge *e = &x->outside[i];
        if (e->to_id == x->call->id && e->to_pin_id == id) {
            node *from = graph_node_by_id(x->caller, e->from_id);
            return join(x->caller, from, pin_at(from, e->from_pin_id), to, to_pin, e->conversion, conversion);
        }
    }
    return give_value(x->caller, to, to_pin, &x->call->input_pins[pin], conversion);
}

/* The call input matching Function Entry's output `pin`. */
static int call_input(const expansion *x, uint64_t pin)
{
    int index = pin_at(x->entry, pin);
    const pin_spec *pins = x->entry->output_pins;
    return nth_pin(x->call->input_pins, x->call->input_count, pins[index].kind, ordinal_of(pins, index));
}

/* The call output matching Return's input `pin`. */
static int call_output(const expansion *x, uint64_t pin)
{
    int index = pin_at(x->ret, pin);
    const pin_spec *pins = x->ret->input_pins;
    return nth_pin(x->call->output_pins, x->call->output_count, pins[index].kind, ordinal_of(pins, index));
}

/* Execution reaching Return's input `pin` leaves by the matching call output. */
static const char *exit_to(const expansion *x, node *from, int from_pin, uint64_t pin, int order)
{
    int out = call_output(x, pin);
    if (out < 0) {
        return STALE;
    }
    for (int i = 0; i < acount(x->outside); i++) {
        const edge *e = &x->outside[i];
        if (e->from_id != x->call->id ||
            e->from_pin_id != x->call->output_pins[out].id) {
            continue;
        }
        node *to = graph_node_by_id(x->caller, e->to_id);
        if (graph_connect_exec(x->caller, from, from_pin, to, pin_at(to, e->to_pin_id), order)) {
            return "Could not preserve execution.";
        }
    }
    return NULL;
}

/* Execution into a call input continues where Function Entry's matching
   output leads. */
static const char *enter_from(const expansion *x, const edge *wire)
{
    node *from = graph_node_by_id(x->caller, wire->from_id);
    int from_pin = pin_at(from, wire->from_pin_id);
    int in = pin_at(x->call, wire->to_pin_id);
    int out = nth_pin(
        x->entry->output_pins,
        x->entry->output_count,
        PIN_EXEC,
        ordinal_of(x->call->input_pins, in)
    );
    if (out < 0) {
        return STALE;
    }
    const char *error = NULL;
    for (int i = 0; !error && i < acount(x->inside); i++) {
        const edge *e = &x->inside[i];
        if (e->from_id != x->entry->id ||
            e->from_pin_id != x->entry->output_pins[out].id) {
            continue;
        }
        if (e->to_id == x->ret->id) {
            error = exit_to(x, from, from_pin, e->to_pin_id, wire->exec_order);
            continue;
        }
        node *to = copy_of(x, e->to_id);
        if (graph_connect_exec(x->caller, from, from_pin, to, body_pin(x, e->to_id, e->to_pin_id), wire->exec_order)) {
            error = "Could not preserve execution.";
        }
    }
    return error;
}

static const char *wire_exec(const expansion *x)
{
    const char *error = NULL;
    for (int i = 0; !error && i < acount(x->outside); i++) {
        const edge *e = &x->outside[i];
        if (e->kind == GRAPH_EDGE_EXEC && e->to_id == x->call->id) {
            error = enter_from(x, e);
        }
    }
    for (int i = 0; !error && i < acount(x->inside); i++) {
        const edge *e = &x->inside[i];
        if (e->kind == GRAPH_EDGE_EXEC && e->to_id == x->ret->id &&
            e->from_id != x->entry->id) {
            node *from = copy_of(x, e->from_id);
            error = exit_to(x, from, body_pin(x, e->from_id, e->from_pin_id), e->to_pin_id, e->exec_order);
        }
    }
    return error;
}

/* Body inputs Function Entry fed take what fed the call. */
static const char *wire_inputs(const expansion *x)
{
    const char *error = NULL;
    for (int i = 0; !error && i < acount(x->inside); i++) {
        const edge *e = &x->inside[i];
        if (e->kind != GRAPH_EDGE_DATA || e->from_id != x->entry->id ||
            e->to_id == x->ret->id) {
            continue;
        }
        node *to = copy_of(x, e->to_id);
        error = feed(x, call_input(x, e->from_pin_id), to, body_pin(x, e->to_id, e->to_pin_id), e->conversion);
    }
    return error;
}

/* Consumers of the call's results read what reached Return: a body node's
   output, a call input passed through, or Return's own value. */
static const char *wire_outputs(const expansion *x)
{
    const char *error = NULL;
    for (int i = 0; !error && i < acount(x->outside); i++) {
        const edge *e = &x->outside[i];
        if (e->kind != GRAPH_EDGE_DATA || e->from_id != x->call->id) {
            continue;
        }
        int ordinal = ordinal_of(x->call->output_pins, pin_at(x->call, e->from_pin_id));
        int result = nth_pin(x->ret->input_pins, x->ret->input_count, PIN_DATA, ordinal);
        if (result < 0) {
            return STALE;
        }
        node *to = graph_node_by_id(x->caller, e->to_id);
        int to_pin = pin_at(to, e->to_pin_id);
        const edge *source = NULL;
        for (int j = 0; j < acount(x->inside); j++) {
            const edge *in = &x->inside[j];
            source = in->to_id == x->ret->id &&
                             in->to_pin_id == x->ret->input_pins[result].id
                       ? in
                       : source;
        }
        if (!source) {
            error = give_value(x->caller, to, to_pin, &x->ret->input_pins[result], e->conversion);
        } else if (source->from_id == x->entry->id) {
            error = source->conversion && e->conversion
                      ? "Wire the converted values past the call first."
                      : feed(
                            x,
                            call_input(x, source->from_pin_id),
                            to,
                            to_pin,
                            source->conversion ? source->conversion : e->conversion
                        );
        } else {
            node *from = copy_of(x, source->from_id);
            error = join(
                x->caller,
                from,
                body_pin(x, source->from_id, source->from_pin_id),
                to,
                to_pin,
                source->conversion,
                e->conversion
            );
        }
    }
    return error;
}

static const char *expand(expansion *x)
{
    const char *error = copy_body(x);
    if (error) {
        return error;
    }
    detach(x);
    error = wire_exec(x);
    error = error ? error : wire_inputs(x);
    error = error ? error : wire_outputs(x);
    if (!error) {
        int removed = graph_remove_node(x->caller, x->call);
        assert(removed == 0);
        (void)removed;
    }
    return error;
}

const char *behavior_asset_expand_call(
    const behavior_asset *asset,
    const graph *caller,
    const node *call,
    behavior_asset **out_asset,
    uint64_t **out_ids
)
{
    *out_asset = NULL;
    const char *error = behavior_asset_can_expand_call(asset, caller, call);
    if (error) {
        return error;
    }
    behavior_asset *copy = behavior_asset_clone(asset);
    if (!copy) {
        return "Could not copy the behavior asset.";
    }
    expansion x = { .caller = behavior_asset_find_graph(copy, caller->graph_id),
                    .target = behavior_asset_find_graph(copy, call->reference_id) };
    x.call = graph_node_by_id(x.caller, call->id);
    x.entry = graph_node_by_id(x.target, x.target->entry_id);
    x.ret = graph_node_by_id(x.target, x.target->return_id);
    error = expand(&x);
    for (int i = 0; !error && i < acount(x.copies); i++) {
        apush(*out_ids, x.copies[i]->id);
    }
    if (error) {
        behavior_asset_free(copy);
    } else {
        *out_asset = copy;
    }
    afree(x.body);
    afree(x.copies);
    afree(x.outside);
    afree(x.inside);
    return error;
}
