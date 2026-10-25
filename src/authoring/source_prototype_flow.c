#include "authoring/source_prototype_flow.h"

#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/text.h"

#include <stdlib.h>

typedef struct
{
    int document, symbol;
} flow_symbol;
typedef struct
{
    const authoring_document *documents;
    int count;
    const script_catalog *catalog;
    bool **symbols, **expressions; /* Marked records, per document. */
    flow_symbol *pending;
    authoring_prototype_name *names;
} flow;

static void mark_symbol(flow *f, int document, int symbol)
{
    if (symbol < 0 || f->symbols[document][symbol]) {
        return;
    }
    f->symbols[document][symbol] = true;
    apush(f->pending, ((flow_symbol){ document, symbol }));
}

static void add_name(flow *f, int document, const script_expression *constant)
{
    const script_catalog_capability
        *prototype = constant->capability
                       ? script_catalog_capability_by_id(f->catalog, constant->capability)
                       : NULL;
    if (!constant->capability) {
        arena storage = { 0 };
        const char *name;
        const char *error = pin_text_cstring(&storage, constant->constant, &name, NULL);
        int index = error ? -1 : script_catalog_find_capability(f->catalog, name, SCRIPT_CAPABILITY_PROTOTYPE);
        prototype = index >= 0 ? &f->catalog->capabilities[index] : NULL;
        arena_free(&storage);
    }
    if (!prototype || prototype->kind != SCRIPT_CAPABILITY_PROTOTYPE) {
        return;
    }
    apush(
        f->names,
        ((authoring_prototype_name){ document, constant->range, prototype->id })
    );
}

/* Follows a value that reaches a prototype parameter back to its sources. */
static void mark_value(flow *f, int document, int expression)
{
    if (expression < 0 || f->expressions[document][expression]) {
        return;
    }
    f->expressions[document][expression] = true;
    const script_expression *e = &f->documents[document].analysis->expressions[expression];
    switch (e->kind) {
        case SCRIPT_EXPR_SYMBOL: mark_symbol(f, document, e->symbol); return;
        case SCRIPT_EXPR_CONSTANT:
            /* Report typed origins too: a direct reference may reach this sink
             * through a local or authored parameter. Omitted-default snapshots
             * are still edited only at their original declaration. */
            if (!e->constant_snapshot && e->constant.container == PIN_CONTAINER_SINGLE &&
                (e->constant.type == PIN_STR || e->constant.type == PIN_NAME) &&
                pin_text(e->constant).length) {
                add_name(f, document, e);
            }
            return;
        case SCRIPT_EXPR_CAST: mark_value(f, document, e->left); return;
        case SCRIPT_EXPR_FALLBACK:
            mark_value(f, document, e->left);
            mark_value(f, document, e->right);
            return;
        default: return;
    }
}

static void mark_arguments(flow *f, int document, const script_expression *call, int parameter)
{
    for (int i = 0; i < call->argument_count; i++) {
        if (call->arguments[i].parameter == parameter) {
            mark_value(f, document, call->arguments[i].value);
        }
    }
}

/* Arguments for native prototype parameters start the walk. */
static void mark_sinks(flow *f, int document)
{
    const script_analysis *analysis = f->documents[document].analysis;
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *e = &analysis->expressions[i];
        const script_catalog_capability
            *capability = e->kind == SCRIPT_EXPR_CALL
                            ? script_catalog_capability_by_id(f->catalog, e->capability)
                            : NULL;
        for (int j = 0; capability && j < capability->parameter_count; j++) {
            if (capability->parameters[j].reference_kind == SCRIPT_REFERENCE_PROTOTYPE) {
                mark_arguments(f, document, e, j);
            }
        }
    }
}

static void mark_calls(flow *f, uint64_t function, int parameter)
{
    for (int i = 0; i < f->count; i++) {
        const script_analysis *caller = f->documents[i].analysis;
        for (int j = 0; j < caller->expression_count; j++) {
            const script_expression *e = &caller->expressions[j];
            if (e->kind == SCRIPT_EXPR_CALL && e->capability == function) {
                mark_arguments(f, i, e, parameter);
            }
        }
    }
}

/* Every call of the parameter's function, in any document, passes it on. */
static void mark_callers(flow *f, flow_symbol parameter)
{
    const script_analysis *analysis = f->documents[parameter.document].analysis;
    for (int i = 0; i < analysis->function_count; i++) {
        const script_function *function = &analysis->functions[i];
        for (int j = 0; function->symbol >= 0 && j < acount(function->parameters); j++) {
            if (function->parameters[j] == parameter.symbol) {
                mark_calls(f, analysis->symbols[function->symbol].id, j);
            }
        }
    }
}

static void mark_writes(flow *f, flow_symbol marked)
{
    const script_analysis *analysis = f->documents[marked.document].analysis;
    const script_symbol *symbol = &analysis->symbols[marked.symbol];
    mark_value(f, marked.document, symbol->initializer);
    for (int i = 0; i < analysis->statement_count; i++) {
        const script_statement *s = &analysis->statements[i];
        if (s->kind == SCRIPT_STMT_ASSIGN && s->op == SCRIPT_OP_NONE &&
            s->expression >= 0 &&
            analysis->expressions[s->expression].kind == SCRIPT_EXPR_SYMBOL &&
            analysis->expressions[s->expression].symbol == marked.symbol) {
            mark_value(f, marked.document, s->second);
        }
    }
    if (symbol->kind == SCRIPT_PARAMETER) {
        mark_callers(f, marked);
    }
}

static int compare_names(const void *left, const void *right)
{
    const authoring_prototype_name *a = left, *b = right;
    if (a->document != b->document) {
        return (a->document > b->document) - (a->document < b->document);
    }
    return (a->range.start > b->range.start) - (a->range.start < b->range.start);
}

authoring_prototype_name *authoring_prototype_names(
    const authoring_document *documents,
    int count,
    const script_catalog *catalog
)
{
    flow f = { .documents = documents, .count = count, .catalog = catalog };
    f.symbols = xcalloc((size_t)count + 1, sizeof *f.symbols);
    f.expressions = xcalloc((size_t)count + 1, sizeof *f.expressions);
    for (int i = 0; i < count; i++) {
        const script_analysis *analysis = documents[i].analysis;
        f.symbols[i] = xcalloc((size_t)analysis->symbol_count + 1, sizeof **f.symbols);
        f.expressions[i] = xcalloc(
            (size_t)analysis->expression_count + 1,
            sizeof **f.expressions
        );
    }
    for (int i = 0; i < count; i++) {
        mark_sinks(&f, i);
    }
    while (acount(f.pending)) {
        flow_symbol marked = f.pending[acount(f.pending) - 1];
        asetlen(f.pending, acount(f.pending) - 1);
        mark_writes(&f, marked);
    }
    for (int i = 0; i < count; i++) {
        free(f.symbols[i]);
        free(f.expressions[i]);
    }
    free(f.symbols);
    free(f.expressions);
    afree(f.pending);
    if (f.names) {
        qsort(f.names, (size_t)acount(f.names), sizeof *f.names, compare_names);
    }
    return f.names;
}
