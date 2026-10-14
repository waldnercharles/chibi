#include "authoring/document_observation.h"
#include "authoring/document_semantics_definition.h"
#include "execution/function.h"
#include "graph/authoring/layout.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/core_nodes.h"
#include "runtime/value_json.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "types/value_document.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *text(arena *storage, const char *value)
{
    if (!value) {
        value = "";
    }
    char *copy = arena_alloc(storage, strlen(value) + 1);
    strcpy(copy, value);
    return copy;
}
static char *identity(arena *storage, uint64_t value)
{
    char value_text[32];
    snprintf(value_text, sizeof value_text, "%llu", (unsigned long long)value);
    return text(storage, value_text);
}
static bool parse_identity(const char *value, uint64_t *out)
{
    if (!value || !*value || *value == '-') {
        return false;
    }
    for (const char *p = value; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    char *end;
    errno = 0;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (errno || *end) {
        return false;
    }
    *out = parsed;
    return true;
}
static pin_array *rows(arena *storage, pin_value *out, const char *name, int count, size_t size)
{
    pin_array *array = pin_array_snapshot_allocate(storage, PIN_STRUCT, name, count);
    for (int i = 0; i < count; i++) {
        void *row = arena_alloc(storage, size);
        memset(row, 0, size);
        array->items[i] = (pin_value){ .type = PIN_STRUCT, .type_name = name, .ptr = row };
    }
    out->ptr = array;
    return array;
}
/* Semantic display facts are reusable; consumers choose their layout and styling. */
static const char *node_symbol(const node_desc *desc)
{
    const char *symbol = "";
    switch (desc->jit_kind) {
        case NODE_JIT_ADD: symbol = "+"; break;
        case NODE_JIT_SUB: symbol = "−"; break;
        case NODE_JIT_MUL: symbol = "×"; break;
        case NODE_JIT_DIV: symbol = "÷"; break;
        default: break;
    }
    int binding = 0;
    const char *spelling = behavior_expression_spelling(desc, &binding);
    if (spelling && binding >= 3 && binding != BEHAVIOR_EXPRESSION_PREFIX_BINDING) {
        symbol = behavior_expression_symbol(spelling);
    }
    if (spelling && !binding) {
        if (!strcmp(spelling, "sin")) symbol = "SIN";
        if (!strcmp(spelling, "cos")) symbol = "COS";
        if (!strcmp(spelling, "tan")) symbol = "TAN";
    }
    return symbol ? symbol : "";
}
static const char *source_operator_symbol(script_operator op)
{
    switch (op) {
        case SCRIPT_OP_ADD: return "+";
        case SCRIPT_OP_SUB: return "−";
        case SCRIPT_OP_MUL: return "×";
        case SCRIPT_OP_DIV: return "÷";
        case SCRIPT_OP_MOD: return "%";
        case SCRIPT_OP_EQ: return "==";
        case SCRIPT_OP_NE: return "≠";
        case SCRIPT_OP_LT: return "<";
        case SCRIPT_OP_LE: return "≤";
        case SCRIPT_OP_GT: return ">";
        case SCRIPT_OP_GE: return "≥";
        case SCRIPT_OP_AND: return "&&";
        case SCRIPT_OP_OR: return "||";
        case SCRIPT_OP_BIT_AND: return "&";
        case SCRIPT_OP_BIT_OR: return "|";
        case SCRIPT_OP_BIT_XOR: return "^";
        case SCRIPT_OP_SHIFT_LEFT: return "<<";
        case SCRIPT_OP_SHIFT_RIGHT: return ">>";
        default: return "";
    }
}
static bool port_connected(const graph *g, const node *n, bool output, int pin)
{
    const pin_spec *spec = output ? &n->output_pins[pin] : &n->input_pins[pin];
    if (output && graph_output_local(g, n, pin)) return true;
    node *source = NULL;
    int source_pin = 0;
    if (!output && graph_input_source(g, n, pin, &source, &source_pin)) return true;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (output ? e->from_id == n->id && e->from_pin_id == spec->id
                   : e->to_id == n->id && e->to_pin_id == spec->id) return true;
    }
    return false;
}
static const char *node_category(const node *n, bool expression, bool call)
{
    if (expression) return "expression";
    if (call) return "call";
    switch (n->desc->display_class) {
        case NODE_DISPLAY_EVENT: return "event";
        case NODE_DISPLAY_FUNCTION: return "function";
        case NODE_DISPLAY_FLOW: return "flow";
        case NODE_DISPLAY_VARIABLE: return "variable";
        case NODE_DISPLAY_LITERAL: return "literal";
        default: return "pure";
    }
}
static void inspect(const document_semantics *s, arena *storage, pin_value *out)
{
    out[0].b = document_semantics_valid(s);
    out[7].str = text(storage, document_semantics_dialect(s));
    int count = document_semantics_diagnostic_count(s);
    pin_array *diagnostics = rows(storage, &out[1], "DocumentDiagnostic", count, sizeof(document_diagnostic_row));
    for (int i = 0; i < count; i++) {
        const script_diagnostic *d = document_semantics_diagnostic(s, i);
        *(document_diagnostic_row *)diagnostics->items[i].ptr = (document_diagnostic_row){
            d->range.start,
            d->range.end,
            d->range.line,
            d->range.column,
            text(storage, d->message)
        };
    }
    const authoring_source_presentation *p = document_semantics_presentation(s);
    const behavior_asset *asset = document_semantics_graph(s);
    int nodes = 0, edges = 0, ports = 0;
    if (asset) {
        for (int i = 0; i < acount(asset->graphs); i++) {
            nodes += acount(asset->graphs[i]->nodes);
            for (int j = 0; j < acount(asset->graphs[i]->nodes); j++) {
                ports += asset->graphs[i]->nodes[j]->input_count +
                         asset->graphs[i]->nodes[j]->output_count;
            }
            edges += acount(asset->graphs[i]->edges);
        }
    }
    pin_array *node_rows = rows(storage, &out[2], "DocumentNode", nodes, sizeof(document_node_row));
    pin_array *edge_rows = rows(storage, &out[3], "DocumentEdge", edges, sizeof(document_edge_row));
    pin_array *port_rows = rows(storage, &out[5], "DocumentPort", ports, sizeof(document_port_row));
    int ni = 0, ei = 0, pi = 0;
    if (asset) {
        for (int i = 0; i < acount(asset->graphs); i++) {
            const graph *g = asset->graphs[i];
            for (int j = 0; j < acount(g->nodes); j++) {
                const node *n = g->nodes[j];
                const source_presentation_origin *
                    origin = p ? authoring_source_presentation_origin(p, g->graph_id, n->id, 0)
                               : NULL;
                for (int side = 0; side < 2; side++) {
                    int count = side ? n->output_count : n->input_count;
                    const pin_spec *pins = side ? n->output_pins : n->input_pins;
                    for (int k = 0; k < count; k++) {
                        const source_presentation_origin *po = p ? authoring_source_presentation_origin(
                                                                       p,
                                                                       g->graph_id,
                                                                       n->id,
                                                                       pins[k].id
                                                                   )
                                                                 : NULL;
                        av_doc *value_doc = av_doc_new();
                        av_value
                            *value = pin_value_to_document(value_doc, &pins[k].default_value);
                        char *literal = value ? av_value_write(value) : NULL;
                        char *json = NULL, json_error[256];
                        value_to_json(&pins[k].default_value, &json, json_error, sizeof json_error);
                        document_port_row *port = port_rows->items[pi++].ptr;
                        *port = (document_port_row){
                            .graph = identity(storage, g->graph_id),
                            .node = identity(storage, n->id),
                            .pin = identity(storage, pins[k].id),
                            .origin = identity(storage, po ? po->identity : 0),
                            .label = text(storage, pins[k].display_name ? pins[k].display_name : pins[k].name),
                            .type = text(
                                storage,
                                pins[k].type_name ? pins[k].type_name
                                                  : pin_type_name(pins[k].type)
                            ),
                            .value = text(storage, literal),
                            .value_json = text(storage, json),
                            .text = text(
                                storage,
                                pins[k].type == PIN_STR || pins[k].type == PIN_NAME
                                    ? pins[k].default_value.str
                                    : ""
                            ),
                            .source = "",
                            .bound = pins[k].binding != NULL,
                            .vector_width = pin_vector_width(&pins[k]),
                            .connected = port_connected(g, n, side != 0, k),
                            .visible = graph_pin_visible(g, n, side != 0, k),
                            .named = side && graph_output_local(g, n, k) != NULL,
                            .output = side != 0,
                            .flow = pins[k].kind == PIN_EXEC,
                            .index = k,
                            .editable = pins[k].kind == PIN_DATA && (!side || graph_output_default_editable(n, k)),
                            .advanced = (pins[k].flags & PIN_FLAG_ADVANCED) != 0,
                            .color = (pins[k].flags & PIN_FLAG_COLOR) != 0,
                            .container = pins[k].container,
                            .boolean = pins[k].type == PIN_BOOL &&
                                       pins[k].default_value.b,
                            .number = pins[k].type == PIN_F32
                                        ? pins[k].default_value.f
                                    : pins[k].type == PIN_F64
                                        ? pins[k].default_value.d
                                    : pins[k].type == PIN_I32
                                        ? pins[k].default_value.i
                                        : 0
                        };
                        if (!side && pins[k].kind == PIN_DATA) {
                            char source_text[4096];
                            behavior_expression_format(g, n, k, source_text, sizeof source_text, NULL);
                            port->source = text(storage, source_text);
                        }
                        port->start = port->end = -1;
                        if (p && po && po->kind == SOURCE_PRESENTATION_EXPRESSION &&
                            !side && !po->flow) {
                            const char *source = authoring_source_presentation_text(p);
                            int start = po->range.start, end = po->range.end;
                            if (start >= 0 && end >= start &&
                                (size_t)end <= strlen(source)) {
                                char *expression = arena_alloc(storage, (size_t)(end - start) + 1);
                                memcpy(expression, source + start, (size_t)(end - start));
                                expression[end - start] = 0;
                                port->text = expression;
                                port->source = expression;
                                port->start = start;
                                port->end = end;
                            }
                        }
                        if (p) {
                            port->editable = port->start >= 0;
                        }
                        free(json);
                        free(literal);
                        av_doc_free(value_doc);
                        if (!origin && po) {
                            origin = po;
                        }
                    }
                }
                document_execution execution = { 0 };
                if (p) {
                    document_execution_query(s, g->graph_id, n->id, &execution, NULL, 0);
                }
                bool call = n->desc == &NODE_CallFunction || n->desc == &NODE_CallPureFunction || n->desc == &NODE_CallMacro || n->desc == &NODE_BehaviorExpression;
                const char *expression = behavior_expression_source(asset, n);
                bool source_expression = origin && origin->kind == SOURCE_PRESENTATION_EXPRESSION;
                const char *node_source = expression ? expression : "";
                if (p && source_expression) {
                    const char *source = authoring_source_presentation_text(p);
                    int start = origin->range.start, end = origin->range.end;
                    if (start >= 0 && end >= start && (size_t)end <= strlen(source)) {
                        char *copy = arena_alloc(storage, (size_t)(end - start) + 1);
                        memcpy(copy, source + start, (size_t)(end - start));
                        copy[end - start] = 0;
                        node_source = copy;
                    }
                    call = origin->operation == SCRIPT_EXPR_CALL;
                }
                uint64_t called_graph = 0;
                if (call && n->reference_id && behavior_asset_find_graph(asset, n->reference_id)) called_graph = n->reference_id;
                if (p && source_expression && origin->operation == SCRIPT_EXPR_CALL && origin->called_function >= 0) {
                    for (int target = 0; target < acount(asset->graphs); target++) {
                        const graph *candidate = asset->graphs[target];
                        const source_presentation_origin *entry = authoring_source_presentation_origin(p, candidate->graph_id, candidate->entry_id, 0);
                        if (entry && entry->kind == SOURCE_PRESENTATION_FUNCTION && entry->function == origin->called_function) {
                            called_graph = candidate->graph_id;
                            break;
                        }
                    }
                }
                *(document_node_row *)node_rows->items[ni++].ptr = (document_node_row){
                    .graph = identity(storage, g->graph_id),
                    .node = identity(storage, n->id),
                    .origin = identity(storage, origin ? origin->identity : 0),
                    .label = text(storage, n->desc->name),
                    .start = origin ? origin->range.start : 0,
                    .end = origin ? origin->range.end : 0,
                    .symbol = text(storage, source_expression && origin->operation == SCRIPT_EXPR_BINARY
                        ? source_operator_symbol(origin->op) : node_symbol(n->desc)),
                    .source = text(storage, node_source),
                    .category = text(storage, node_category(n, expression != NULL || source_expression, call)),
                    .source_kind = origin ? origin->kind : -1,
                    .operation = source_expression ? origin->operation : -1,
                    .call = call,
                    .reference_name = text(storage, n->reference_name),
                    .called_graph = identity(storage, called_graph),
                    .kind = n->desc->display_class,
                    .x = n->x,
                    .y = n->y,
                    .disabled = n->disabled,
                    .breakpoint = n->breakpoint,
                    .advanced = n->show_advanced,
                    .presentation = n->presentation,
                    .execution_module = execution.module,
                    .execution_function = execution.function,
                    .execution_occurrence = execution.occurrence
                };
            }
            for (int j = 0; j < acount(g->edges); j++) {
                const edge *e = &g->edges[j];
                *(document_edge_row *)edge_rows->items[ei++].ptr = (document_edge_row){
                    identity(storage, g->graph_id),
                    identity(storage, e->from_id),
                    identity(storage, e->from_pin_id),
                    identity(storage, e->to_id),
                    identity(storage, e->to_pin_id),
                    e->kind == GRAPH_EDGE_EXEC
                };
            }
        }
    }
    authoring_highlight *highlights = document_semantics_highlights(s, &count);
    pin_array *tokens = rows(storage, &out[4], "DocumentToken", count, sizeof(document_token_row));
    for (int i = 0; i < count; i++) {
        *(document_token_row *)tokens->items[i].ptr = (document_token_row){
            highlights[i].range.start,
            highlights[i].range.end,
            text(storage, highlights[i].cls)
        };
    }
    free(highlights);
}
static const char *run(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out,
    int op
)
{
    document_semantics_service *service = host;
    arena *storage = context->value_storage;
    char error[256] = { 0 };
    int error_index = op == 0 ? 6 : (op == 1 || op == 6) ? 3 : 1;
    out[error_index].str = text(storage, "");
    if (op == 0) {
        out[0].b = false;
        out[7].str = text(storage, "");
        rows(storage, &out[1], "DocumentDiagnostic", 0, sizeof(document_diagnostic_row));
        rows(storage, &out[2], "DocumentNode", 0, sizeof(document_node_row));
        rows(storage, &out[3], "DocumentEdge", 0, sizeof(document_edge_row));
        rows(storage, &out[4], "DocumentToken", 0, sizeof(document_token_row));
        rows(storage, &out[5], "DocumentPort", 0, sizeof(document_port_row));
    }
    if (op == 1) {
        out[0].i = 0;
        out[1].i = 0;
        rows(storage, &out[2], "DocumentCompletion", 0, sizeof(document_completion_row));
    }
    if (op == 2) {
        out[0].str = text(storage, "");
    }
    if (op == 3) {
        rows(storage, &out[0], "DocumentReference", 0, sizeof(document_reference_row));
    }
    if (op >= 4) {
        out[0].b = false;
    }
    if (!service || !service->documents) {
        if (op == 6) {
            out[1].i = -1;
            out[2].i = -1;
        }
        out[error_index].str = text(storage, "Semantic document service is unavailable");
        return NULL;
    }
    if (op == 6) {
        int start, end;
        bool ok = document_semantics_find(
            service->documents,
            in[0].i,
            in[1].i,
            in[2].str,
            in[3].i,
            in[4].b,
            &start,
            &end,
            error,
            sizeof error
        );
        out[0].b = ok && start >= 0;
        out[1].i = start;
        out[2].i = end;
        out[3].str = text(storage, error);
        return NULL;
    }
    document_semantics *
        s = document_semantics_service_snapshot(service, in[0].i, in[1].i, error, sizeof error);
    if (!s) {
        out[error_index].str = text(storage, error);
        return NULL;
    }
    const authoring_navigation *navigation = document_semantics_navigation(s);
    if (op == 0) {
        inspect(s, storage, out);
    }
    if (op == 1) {
        authoring_completions completion = { 0 };
        document_semantics_complete(s, in[2].i, service->ui_project, &completion);
        out[0].i = completion.range.start;
        out[1].i = completion.range.end;
        pin_array *items = rows(
            storage,
            &out[2],
            "DocumentCompletion",
            completion.count,
            sizeof(document_completion_row)
        );
        for (int i = 0; i < completion.count; i++) {
            const authoring_completion *c = &completion.items[i];
            *(document_completion_row *)items->items[i].ptr = (document_completion_row){
                text(storage, c->label),
                text(storage, c->detail),
                text(storage, c->insert),
                c->kind
            };
        }
        authoring_completions_free(&completion);
    }
    if (op == 2) {
        char *hover = document_semantics_hover(s, in[2].i, service->ui_project);
        out[0].str = text(storage, hover);
        free(hover);
    }
    if (op == 3 && navigation) {
        const authoring_reference
            *reference = authoring_navigation_at(navigation, 0, in[2].i);
        int count = 0;
        if (reference) {
            for (int i = 0; i < authoring_navigation_reference_count(navigation); i++) {
                if (authoring_target_equal(
                        reference->target,
                        authoring_navigation_reference_at(navigation, i)->target
                    )) {
                    count++;
                }
            }
        }
        pin_array *items = rows(storage, &out[0], "DocumentReference", count, sizeof(document_reference_row));
        int row = 0;
        if (reference) {
            for (int i = 0; i < authoring_navigation_reference_count(navigation); i++) {
                const authoring_reference *r = authoring_navigation_reference_at(navigation, i);
                if (!authoring_target_equal(reference->target, r->target)) {
                    continue;
                }
                *(document_reference_row *)items->items[row++].ptr = (document_reference_row){
                    text(storage, authoring_navigation_name(navigation, r->target)),
                    r->location.range.start,
                    r->location.range.end,
                    r->kind
                };
            }
        }
    }
    if (op == 3 && !navigation && document_semantics_ui(s)) {
        const ui_source *source = document_semantics_ui(s);
        ui_source_symbol selected = ui_source_symbol_at(source, in[2].i);
        ui_source_symbol *symbols = NULL;
        ui_source_symbols(source, &symbols);
        int count = 0;
        for (int i = 0; i < acount(symbols); i++) {
            if (selected.kind != UI_SYMBOL_NONE && symbols[i].kind == selected.kind &&
                !strcmp(symbols[i].name, selected.name)) {
                count++;
            }
        }
        pin_array *items = rows(storage, &out[0], "DocumentReference", count, sizeof(document_reference_row));
        int at = 0;
        for (int i = 0; i < acount(symbols); i++) {
            const ui_source_symbol *symbol = &symbols[i];
            if (selected.kind == UI_SYMBOL_NONE || symbol->kind != selected.kind ||
                strcmp(symbol->name, selected.name)) {
                continue;
            }
            *(document_reference_row *)items->items[at++].ptr = (document_reference_row){
                text(storage, symbol->name),
                symbol->start,
                symbol->end,
                symbol->definition ? AUTHORING_DEFINE : AUTHORING_READ
            };
        }
        afree(symbols);
    }
    if (op == 4) {
        out[0].b = document_semantics_replace(
            service->documents,
            s,
            in[2].i,
            in[3].i,
            in[4].str,
            in[5].b,
            error,
            sizeof error
        );
    }
    if (op == 5) {
        authoring_edit_request request = { .kind = in[2].i,
                                           .producer_result = in[5].i,
                                           .expression = in[6].str,
                                           .fragment = in[6].str,
                                           .name = in[6].str,
                                           .capability_name = in[6].str,
                                           .signature = in[6].str,
                                           .reference = in[6].str };
        const authoring_source_presentation *p = document_semantics_presentation(s);
        if (!p) {
            snprintf(error, sizeof error, "Document has no semantic graph presentation");
        } else if (
            !parse_identity(in[3].str, &request.origin) ||
            !parse_identity(in[4].str, &request.producer)
        ) {
            snprintf(error, sizeof error, "Graph identities must be unsigned decimal text");
        } else if (request.kind < AUTHORING_EDIT_EXPRESSION || request.kind > AUTHORING_EDIT_RETYPE_VARIABLE) {
            snprintf(error, sizeof error, "Unknown semantic edit kind");
        } else {
            request.generation = authoring_source_edit_generation(p);
            out[0].b = document_semantics_edit(service->documents, s, &request, error, sizeof error);
        }
    }
    if (op == 7) {
        out[0]
            .b = document_semantics_rename(service->documents, s, in[2].i, in[3].str, error, sizeof error);
    }
    out[error_index].str = text(storage, error);
    document_semantics_free(s);
    return NULL;
}
#define CALLBACK(N)                                                                           \
    static const char *                                                                       \
    call##N(const behavior_function_context *c, void *s, const pin_value *in, pin_value *out) \
    {                                                                                         \
        return run(c, s, in, out, N);                                                         \
    }
CALLBACK(0) CALLBACK(1) CALLBACK(2) CALLBACK(3) CALLBACK(4) CALLBACK(5) CALLBACK(6) CALLBACK(7)
#define CALL(N)                                                                \
    { .service = "document_semantics",                                         \
      .optional_service = true,                                                \
      .missing_service = "Semantic document service is unavailable",           \
      .context = call##N },
const node_host_call DOCUMENT_SEMANTICS_CALLS[8]={CALL(0) CALL(1) CALL(2) CALL(3) CALL(4) CALL(5) CALL(6) CALL(7)};
const node_access DOCUMENT_SEMANTICS_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
const node_access DOCUMENT_SEMANTICS_WRITE_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
