#include "authoring/presentation_schema.h"
#include "authoring/source_presentation_internal.h"
#include "language/identity.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static_assert((int)SOURCE_PRESENTATION_SYMBOL == SCRIPT_RECORD_SYMBOL);
static_assert((int)SOURCE_PRESENTATION_FUNCTION == SCRIPT_RECORD_FUNCTION);
static_assert((int)SOURCE_PRESENTATION_EXPRESSION == SCRIPT_RECORD_EXPRESSION);
static_assert((int)SOURCE_PRESENTATION_STATEMENT == SCRIPT_RECORD_STATEMENT);
static_assert((int)SOURCE_PRESENTATION_QUERY == SCRIPT_RECORD_QUERY);
static_assert((int)SOURCE_PRESENTATION_BINDING == SCRIPT_RECORD_BINDING);

char *source_view_own(graph *g, const char *text)
{
    return graph_own_str(g, text, strlen(text));
}
const script_catalog_type *source_view_type(source_projection *p, uint64_t id)
{
    for (int i = 0; i < p->catalog->type_count; i++) {
        if (p->catalog->types[i].id == id) {
            return &p->catalog->types[i];
        }
    }
    return NULL;
}
const script_catalog_capability *source_view_capability(source_projection *p, uint64_t id)
{
    for (int i = 0; i < p->catalog->capability_count; i++) {
        if (p->catalog->capabilities[i].id == id) {
            return &p->catalog->capabilities[i];
        }
    }
    return NULL;
}
pin_spec source_view_data_pin(source_projection *p, const char *name, script_type type)
{
    return authoring_presentation_pin(p->graph, p->catalog, name, type);
}
pin_spec source_view_flow_pin(source_projection *p, const char *name)
{
    return (pin_spec){ .name = source_view_own(p->graph, name),
                       .type = PIN_FLOW,
                       .kind = PIN_EXEC };
}
void source_view_origin(
    source_projection *p,
    source_presentation_kind kind,
    int record,
    script_range range,
    node *n,
    uint64_t pin,
    uint64_t capability
)
{
    char identity[192];
    uint64_t semantic = script_record_meaning(p->analysis, (script_record_kind)kind, record);
    int port = -1;
    uint64_t primary = 0;
    int node_kind = kind, node_record = record;
    if (n && pin) {
        for (int i = 0; i < acount(p->out->origins); i++) {
            const source_presentation_origin *owner = &p->out->origins[i];
            if (owner->graph == p->graph->graph_id && owner->node == n->id &&
                !owner->pin) {
                primary = owner->identity;
                node_kind = owner->kind;
                node_record = owner->record;
                break;
            }
        }
        assert(primary);
        for (int i = 0; i < n->input_count; i++) {
            if (n->input_pins[i].id == pin) {
                port = i;
            }
        }
        for (int i = 0; i < n->output_count; i++) {
            if (n->output_pins[i].id == pin) {
                port = n->input_count + i;
            }
        }
    }
    snprintf(
        identity,
        sizeof identity,
        "%llu:%d:%d:%d:%llu:%llu:%d:%llu:%d",
        (unsigned long long)script_catalog_id("program", p->analysis->identity),
        p->function,
        kind,
        record,
        (unsigned long long)semantic,
        (unsigned long long)(port + 1),
        n ? (int)n->desc->display_class : -1,
        (unsigned long long)primary,
        n && port >= n->input_count
    );
    source_presentation_origin o = {
        .identity = script_catalog_id("source-presentation", identity),
        .occurrence = script_record_id(p->occurrences, (script_record_kind)kind, record),
        .node_occurrence = script_record_id(p->occurrences, (script_record_kind)node_kind, node_record),
        .semantic = semantic,
        .capability = capability,
        .graph = p->graph->graph_id,
        .node = n ? n->id : 0,
        .pin = pin,
        .kind = kind,
        .function = p->function,
        .called_function = -1,
        .display_class = n ? n->desc->display_class : -1,
        .node_kind = node_kind,
        .node_record = node_record,
        .function_name = script_catalog_id(
            "source-anchor-owner",
            p->function < 0 ? "root" : p->graph->name
        ),
        .range = range,
        .record = record,
        .port_index = n && port >= n->input_count ? port - n->input_count : port,
        .result_index = -1,
        .parameter = -1,
        .output = n && port >= n->input_count,
    };
    if (n && port >= 0) {
        const pin_spec *spec = o.output ? &n->output_pins[o.port_index]
                                        : &n->input_pins[o.port_index];
        o.port_name = script_catalog_id("source-anchor-port", spec->name ? spec->name : "");
        o.flow = spec->kind == PIN_EXEC;
        if (o.output && !o.flow) {
            o.result_index = o.port_index;
        }
    }
    switch (kind) {
        case SOURCE_PRESENTATION_SYMBOL:
            o.operation = p->analysis->symbols[record].kind;
            o.type = p->analysis->symbols[record].type;
            o.writable = p->analysis->symbols[record].writable;
            o.disabled = p->analysis->symbols[record].disabled;
            break;
        case SOURCE_PRESENTATION_EXPRESSION:
            o.operation = p->analysis->expressions[record].kind;
            if (o.operation == SCRIPT_EXPR_CALL) {
                o.called_function = p->analysis->expressions[record].right;
            }
            o.entry = p->analysis->expressions[record].entry;
            o.effects = p->analysis->expressions[record].effects;
            o.type = p->analysis->expressions[record].type;
            o.op = p->analysis->expressions[record].op;
            o.writable = p->analysis->expressions[record].writable;
            break;
        case SOURCE_PRESENTATION_STATEMENT:
            o.operation = p->analysis->statements[record].kind;
            o.effects = p->analysis->statements[record].effects;
            o.op = p->analysis->statements[record].op;
            break;
        case SOURCE_PRESENTATION_FUNCTION:
            o.operation = p->analysis->functions[record].kind;
            o.effects = p->analysis->functions[record].effects;
            o.disabled = p->analysis->functions[record].disabled;
            break;
        case SOURCE_PRESENTATION_QUERY:
            o.operation = p->analysis->queries[record].kind;
            break;
        case SOURCE_PRESENTATION_BINDING: o.operation = 0; break;
    }
    if (p->function >= 0) {
        o.owner = script_record_meaning(p->analysis, SCRIPT_RECORD_FUNCTION, p->function);
        int parent = p->analysis->functions[p->function].parent;
        if (parent >= 0) {
            o.parent = script_record_meaning(p->analysis, SCRIPT_RECORD_FUNCTION, parent);
        }
    }
    apush(p->out->origins, o);
    if (!n || !p->previous) {
        return;
    }
    for (int i = 0; i < acount(p->previous->origins); i++) {
        const source_presentation_origin *old = &p->previous->origins[i];
        if (old->identity != o.identity || !old->node) {
            continue;
        }
        graph *g = behavior_asset_find_graph(p->previous->view, old->graph);
        node *previous = graph_node_by_id(g, old->node);
        if (!previous) {
            continue;
        }
        if (pin) {
            if (o.flow || old->flow || old->port_index < 0 ||
                old->port_index >= (old->output ? previous->output_count
                                                : previous->input_count)) {
                return;
            }
            const pin_spec *spec = old->output
                                     ? &previous->output_pins[old->port_index]
                                     : &previous->input_pins[old->port_index];
            int status = graph_set_pin_watch(
                p->graph,
                n,
                o.output,
                o.port_index,
                (spec->flags & PIN_FLAG_WATCH) != 0
            );
            assert(!status);
            (void)status;
            return;
        }
        n->x = previous->x;
        n->y = previous->y;
        n->placed = previous->placed;
        n->presentation = previous->presentation;
        n->breakpoint = previous->breakpoint;
        n->breakpoint_enabled = previous->breakpoint_enabled;
        n->show_advanced = previous->show_advanced;
        if (previous->comment) {
            graph_set_node_comment(p->graph, n, previous->comment, previous->comment_hidden);
        }
        return;
    }
}
void source_view_describe(source_projection *p, node *n, const char *text)
{
    assert(n->desc->metadata_only);
    node_desc *desc = (node_desc *)n->desc;
    desc->description = source_view_own(p->graph, text);
}
void source_view_describe_variable(source_projection *p, node *n, const script_symbol *symbol, bool write)
{
    const char *scope = "local";
    switch (symbol->kind) {
        case SCRIPT_STATE:
            scope = p->analysis->kind == SCRIPT_PROGRAM_SYSTEM
                      ? "this entity's state"
                      : "this behavior instance's";
            break;
        case SCRIPT_SHARED_STATE: scope = "this system registration's"; break;
        case SCRIPT_PARAMETER: scope = "this call's parameter"; break;
        case SCRIPT_RESULT: scope = "this call's result"; break;
        case SCRIPT_LOCAL: break;
        default: return;
    }
    char description[512];
    snprintf(
        description,
        sizeof description,
        "%s %s variable '%s'.",
        write ? "Updates" : "Reads",
        scope,
        symbol->name
    );
    source_view_describe(p, n, description);
}
node *source_view_box(
    source_projection *p,
    const char *name,
    node_display_class display,
    pin_spec *inputs,
    pin_spec *outputs
)
{
    node_desc *desc = authoring_presentation_descriptor(
        p->graph,
        name,
        display,
        "Source",
        inputs,
        acount(inputs),
        outputs,
        acount(outputs)
    );
    afree(inputs);
    afree(outputs);
    node *n = graph_add_node(p->graph, desc);
    assert(n);
    n->placed = false;
    return n;
}
/* Resolved edges may include implicit casts. The view records these source
 * dependencies directly; it does not re-run graph executable type inference. */
void source_view_wire(source_projection *p, node *from, int output, node *to, int input, graph_edge_kind kind, int order)
{
    assert(
        from && to && output >= 0 && output < from->output_count &&
        input >= 0 && input < to->input_count
    );
    apush(
        p->graph->edges,
        ((edge){ .from_id = from->id,
                 .from_pin_id = from->output_pins[output].id,
                 .to_id = to->id,
                 .to_pin_id = to->input_pins[input].id,
                 .kind = kind,
                 .order = order })
    );
}
