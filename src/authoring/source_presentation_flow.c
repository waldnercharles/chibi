#include "authoring/source_presentation_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *statement_name(script_statement_kind kind)
{
    static const char *names[] = { "Block", "Declare",  "Assign", "Evaluate",
                                   "If",    "While",    "For",    "For Each",
                                   "Break", "Continue", "Return", "Label",
                                   "Goto",  "Sequence", "Match",  "Case" };
    assert(kind >= SCRIPT_STMT_BLOCK && kind <= SCRIPT_STMT_CASE);
    return names[kind];
}
static node *statement(source_projection *p, int index)
{
    if (index < 0) {
        return NULL;
    }
    assert(index < p->analysis->statement_count);
    if (p->statements[index]) {
        return p->statements[index];
    }
    const script_statement *s = &p->analysis->statements[index];
    pin_spec *inputs = NULL, *outputs = NULL;
    int *values = NULL, *bodies = NULL;
    apush(inputs, source_view_flow_pin(p, "Exec"));
    bool loop = s->kind == SCRIPT_STMT_WHILE || s->kind == SCRIPT_STMT_FOR ||
                s->kind == SCRIPT_STMT_FOREACH;
    bool terminal = s->kind == SCRIPT_STMT_RETURN || s->kind == SCRIPT_STMT_GOTO ||
                    s->kind == SCRIPT_STMT_BREAK || s->kind == SCRIPT_STMT_CONTINUE;
    if (loop) {
        apush(inputs, source_view_flow_pin(p, "Continue"));
    }
    int first_value = acount(inputs);
    if (s->expression >= 0) {
        apush(values, s->expression);
    }
    if (s->second >= 0) {
        apush(values, s->second);
    }
    if (s->third >= 0) {
        apush(values, s->third);
    }
    for (int i = 0; i < acount(values); i++) {
        apush(
            inputs,
            source_view_data_pin(
                p,
                i == 0 ? "Value" : "Operand",
                p->analysis->expressions[values[i]].type
            )
        );
    }
    if (!terminal) {
        apush(outputs, source_view_flow_pin(p, "Then"));
    }
    if (s->body >= 0) {
        apush(bodies, s->body);
    }
    if (s->alternative >= 0) {
        apush(bodies, s->alternative);
    }
    for (int i = 0; i < s->child_count; i++) {
        apush(bodies, s->children[i]);
    }
    int first_body = acount(outputs);
    for (int i = 0; i < acount(bodies); i++) {
        char name[32];
        snprintf(name, sizeof name, "Step %d", i + 1);
        const char *label = s->kind == SCRIPT_STMT_IF ? (i ? "False" : "True")
                          : loop                      ? "Body"
                                                      : name;
        apush(outputs, source_view_flow_pin(p, label));
    }
    const char *name = s->symbol >= 0 ? p->analysis->symbols[s->symbol].name
                                      : statement_name(s->kind);
    node *n = source_view_box(p, name, NODE_DISPLAY_FLOW, inputs, outputs);
    p->statements[index] = n;
    if (s->symbol >= 0) {
        n->reference_id = p->analysis->symbols[s->symbol].id;
    }
    source_view_origin(p, SOURCE_PRESENTATION_STATEMENT, index, s->range, n, 0, 0);
    for (int i = 0; i < first_value; i++) {
        source_view_origin(
            p,
            SOURCE_PRESENTATION_STATEMENT,
            index,
            s->range,
            n,
            n->input_pins[i].id,
            0
        );
    }
    if (!terminal) {
        source_view_origin(
            p,
            SOURCE_PRESENTATION_STATEMENT,
            index,
            s->range,
            n,
            n->output_pins[0].id,
            0
        );
    }
    for (int i = 0; i < acount(values); i++) {
        node *value = source_view_expression(p, values[i]);
        source_view_wire(p, value, 0, n, first_value + i, GRAPH_EDGE_DATA, 0);
        source_view_origin(
            p,
            SOURCE_PRESENTATION_EXPRESSION,
            values[i],
            p->analysis->expressions[values[i]].range,
            n,
            n->input_pins[first_value + i].id,
            p->analysis->expressions[values[i]].capability
        );
    }
    /* These ports expose resolved structured control regions. They are view
     * dependencies, not native node execution connections. Source order and
     * statement kind remain in the origin record for source editing. */
    for (int i = 0; i < acount(bodies); i++) {
        source_view_origin(
            p,
            SOURCE_PRESENTATION_STATEMENT,
            bodies[i],
            p->analysis->statements[bodies[i]].range,
            n,
            n->output_pins[first_body + i].id,
            0
        );
        node *body = statement(p, bodies[i]);
        source_view_wire(p, n, first_body + i, body, 0, GRAPH_EDGE_EXEC, i);
    }
    afree(values);
    afree(bodies);
    return n;
}
node *source_view_query(source_projection *p, int index)
{
    const script_query *q = &p->analysis->queries[index];
    const char *names[] = { "Has", "All", "Any", "Not" };
    pin_spec *inputs = NULL, *outputs = NULL;
    script_type boolean = { .storage = PIN_BOOL };
    for (int i = 0; i < q->child_count; i++) {
        apush(inputs, source_view_data_pin(p, "Predicate", boolean));
    }
    apush(outputs, source_view_data_pin(p, "Matches", boolean));
    const char *name = names[q->kind];
    char *label = NULL;
    for (int i = 0; q->component && i < p->catalog->type_count; i++) {
        const script_catalog_type *type = &p->catalog->types[i];
        if (type->component_id != q->component) {
            continue;
        }
        const char *component = type->source_name ? type->source_name : type->name;
        size_t size = strlen(name) + strlen(component) + 3;
        label = xmalloc(size);
        snprintf(label, size, "%s(%s)", name, component);
        break;
    }
    node *n = source_view_box(p, label ? label : name, NODE_DISPLAY_PURE, inputs, outputs);
    free(label);
    n->reference_id = q->component;
    source_view_origin(p, SOURCE_PRESENTATION_QUERY, index, q->range, n, 0, q->component);
    for (int i = 0; i < q->child_count; i++) {
        source_view_wire(p, source_view_query(p, q->children[i]), 0, n, i, GRAPH_EDGE_DATA, 0);
    }
    return n;
}
void source_view_declaration(source_projection *p, int index)
{
    const script_symbol *s = &p->analysis->symbols[index];
    pin_spec *inputs = NULL, *outputs = NULL;
    if (s->initializer >= 0) {
        apush(
            inputs,
            source_view_data_pin(
                p,
                "Initial",
                p->analysis->expressions[s->initializer].type
            )
        );
    }
    const script_expression *tuple = source_view_tuple(p, s->initializer);
    int count = tuple ? tuple->result_count : 1;
    for (int i = 0; i < count; i++) {
        apush(
            outputs,
            source_view_data_pin(
                p,
                tuple ? source_view_result_name(p, tuple, i) : s->name,
                tuple ? tuple->results[i] : s->type
            )
        );
    }
    node *n = source_view_box(p, s->name, NODE_DISPLAY_VARIABLE, inputs, outputs);
    n->reference_id = s->id;
    n->disabled = s->disabled;
    source_view_origin(p, SOURCE_PRESENTATION_SYMBOL, index, s->declaration, n, 0, s->capability);
    for (int i = 0; i < n->output_count; i++) {
        source_view_origin(
            p,
            SOURCE_PRESENTATION_SYMBOL,
            index,
            s->declaration,
            n,
            n->output_pins[i].id,
            s->capability
        );
        p->out->origins[acount(p->out->origins) - 1].type = tuple
                                                              ? tuple->results[i]
                                                              : s->type;
    }
    if (s->initializer >= 0) {
        source_view_origin(
            p,
            SOURCE_PRESENTATION_EXPRESSION,
            s->initializer,
            p->analysis->expressions[s->initializer].range,
            n,
            n->input_pins[0].id,
            s->capability
        );
        source_view_wire(p, source_view_expression(p, s->initializer), 0, n, 0, GRAPH_EDGE_DATA, 0);
    }
}
static char *function_name(source_projection *p, int index)
{
    const script_function *f = &p->analysis->functions[index];
    const char *label = f->symbol >= 0 ? p->analysis->symbols[f->symbol].name : NULL;
    const script_catalog_capability *event = source_view_capability(p, f->event);
    if (!label && event) {
        label = event->name;
    }
    if (!label && f->parent >= 0 && p->analysis->functions[f->parent].symbol >= 0) {
        label = p->analysis->symbols[p->analysis->functions[f->parent].symbol].name;
    }
    if (!label) {
        label = p->analysis->kind == SCRIPT_PROGRAM_SYSTEM ? "System" : "Handler";
    }
    size_t size = strlen(label) + (f->entry ? strlen(f->entry) : 0) + 32;
    char *name = xmalloc(size);
    snprintf(name, size, "%s%s%s", label, f->entry ? "." : "", f->entry ? f->entry : "");
    if (behavior_asset_find_graph_by_name(p->out->view, name)) {
        snprintf(name, size, "%s%s%s [%d]", label, f->entry ? "." : "", f->entry ? f->entry : "", index + 1);
    }
    return name;
}
void source_view_function_graph(source_projection *p, int index)
{
    const script_function *f = &p->analysis->functions[index];
    char *name = function_name(p, index);
    bool macro = f->kind == SCRIPT_FUNCTION_MACRO_ENTRY ||
                 (f->symbol >= 0 && p->analysis->symbols[f->symbol].kind == SCRIPT_MACRO);
    graph_kind kind = macro                               ? GRAPH_MACRO
                    : f->kind == SCRIPT_FUNCTION_AUTHORED ? GRAPH_FUNCTION
                                                          : GRAPH_EVENT;
    p->graph = behavior_asset_add_graph(p->out->view, kind, name);
    free(name);
    assert(p->graph);
    p->function = index;
    memset(p->expressions, 0, (size_t)p->analysis->expression_count * sizeof *p->expressions);
    memset(p->statements, 0, (size_t)p->analysis->statement_count * sizeof *p->statements);
    pin_spec *inputs = NULL, *outputs = NULL;
    apush(outputs, source_view_flow_pin(p, "Then"));
    for (int i = 0; i < f->parameter_count; i++) {
        const script_symbol *s = &p->analysis->symbols[f->parameters[i]];
        apush(outputs, source_view_data_pin(p, s->name, s->type));
    }
    node *entry = source_view_box(p, f->entry ? f->entry : "Entry", NODE_DISPLAY_EVENT, inputs, outputs);
    if (f->kind == SCRIPT_FUNCTION_HANDLER) {
        const script_catalog_capability *event = source_view_capability(p, f->event);
        if (event) {
            entry->reference_name = source_view_own(p->graph, event->name);
        }
    }
    entry->disabled = f->disabled;
    p->graph->entry_id = entry->id;
    source_view_origin(
        p,
        SOURCE_PRESENTATION_FUNCTION,
        index,
        f->range,
        entry,
        0,
        f->event ? f->event : f->action
    );
    source_view_origin(
        p,
        SOURCE_PRESENTATION_FUNCTION,
        index,
        f->range,
        entry,
        entry->output_pins[0].id,
        f->event ? f->event : f->action
    );
    for (int i = 0; i < f->parameter_count; i++) {
        const script_symbol *s = &p->analysis->symbols[f->parameters[i]];
        source_view_origin(
            p,
            SOURCE_PRESENTATION_SYMBOL,
            f->parameters[i],
            s->declaration,
            entry,
            entry->output_pins[i + 1].id,
            s->capability
        );
    }
    for (int i = 0; i < f->type_parameter_count; i++) {
        source_view_declaration(p, f->type_parameters[i]);
    }
    pin_spec *return_inputs = NULL, *return_outputs = NULL;
    apush(return_inputs, source_view_flow_pin(p, "Exec"));
    for (int i = 0; i < f->result_count; i++) {
        const script_symbol *s = &p->analysis->symbols[f->results[i]];
        apush(return_inputs, source_view_data_pin(p, s->name, s->type));
    }
    for (int i = 0; macro && i < p->analysis->symbol_count; i++) {
        const script_symbol *s = &p->analysis->symbols[i];
        if (s->kind == SCRIPT_LABEL && s->outcome && s->scope == f->scope) {
            apush(return_outputs, source_view_flow_pin(p, s->name));
        }
    }
    node *returned = source_view_box(p, "Return", NODE_DISPLAY_FLOW, return_inputs, return_outputs);
    p->graph->return_id = returned->id;
    source_view_origin(
        p,
        SOURCE_PRESENTATION_FUNCTION,
        index,
        f->range,
        returned,
        0,
        f->event ? f->event : f->action
    );
    source_view_origin(
        p,
        SOURCE_PRESENTATION_FUNCTION,
        index,
        f->range,
        returned,
        returned->input_pins[0].id,
        f->event ? f->event : f->action
    );
    for (int i = 0; i < f->result_count; i++) {
        const script_symbol *s = &p->analysis->symbols[f->results[i]];
        source_view_origin(
            p,
            SOURCE_PRESENTATION_SYMBOL,
            f->results[i],
            s->declaration,
            returned,
            returned->input_pins[i + 1].id,
            s->capability
        );
        if (s->initializer >= 0) {
            source_view_origin(
                p,
                SOURCE_PRESENTATION_EXPRESSION,
                s->initializer,
                p->analysis->expressions[s->initializer].range,
                returned,
                returned->input_pins[i + 1].id,
                s->capability
            );
        }
    }
    node *body = statement(p, f->body);
    if (body) {
        source_view_wire(p, entry, 0, body, 0, GRAPH_EDGE_EXEC, 0);
    }
    for (int i = 0; i < p->analysis->symbol_count; i++) {
        const script_symbol *s = &p->analysis->symbols[i];
        if (s->function != index || s->declaration.start < 0) {
            continue;
        }
        if (s->kind == SCRIPT_LOCAL || s->kind == SCRIPT_LAZY || s->kind == SCRIPT_ACTION ||
            s->kind == SCRIPT_RESULT || s->kind == SCRIPT_LABEL) {
            source_view_declaration(p, i);
        }
    }
    graph_reindex(p->graph);
}
