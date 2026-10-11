#include "authoring/source_graph_library.h"

#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/model/registry.h"
#include "support/array.h"

#include <assert.h>
#include <string.h>

typedef struct
{
    const script_analysis *analysis;
    const script_catalog *catalog;
    const script_function *function;
    graph *graph;
    node *entry;
} lowering;

static const script_catalog_capability *capability(const lowering *l, uint64_t id)
{
    for (int i = 0; i < l->catalog->capability_count; i++) {
        if (l->catalog->capabilities[i].id == id) {
            return &l->catalog->capabilities[i];
        }
    }
    return NULL;
}

static int parameter(const lowering *l, int symbol)
{
    for (int i = 0; i < l->function->parameter_count; i++) {
        if (l->function->parameters[i] == symbol) {
            return i;
        }
    }
    return -1;
}

/* A native call becomes the graph node of its qualified name, as the graph
 * catalog declares it. The node and capability share every port, in order. */
static node *call(const lowering *l, const script_expression *e);

/* Feeds `to`'s input from an expression: a constant becomes the input's
 * default, a parameter an entry output and a call its node. */
static bool feed(const lowering *l, int expression, node *to, int input)
{
    const script_expression *e = &l->analysis->expressions[expression];
    if (e->kind == SCRIPT_EXPR_CONSTANT) {
        return !graph_set_pin_default(l->graph, to, 0, input, &e->constant);
    }
    if (e->kind == SCRIPT_EXPR_SYMBOL) {
        int index = parameter(l, e->symbol);
        return index >= 0 && !graph_connect_data(l->graph, l->entry, index, to, input);
    }
    if (e->kind == SCRIPT_EXPR_CALL) {
        node *n = call(l, e);
        return n && !graph_connect_data(l->graph, n, 0, to, input);
    }
    return false;
}

static node *call(const lowering *l, const script_expression *e)
{
    const script_catalog_capability *c = capability(l, e->capability);
    const node_desc *desc = c && c->pure
                              ? registry_node_by_name(
                                    c->qualified_name ? c->qualified_name : c->name
                                )
                              : NULL;
    if (!desc || desc->input_count != c->parameter_count || !desc->output_count) {
        return NULL;
    }
    node *n = graph_add_node(l->graph, desc);
    assert(n);
    for (int i = 0; i < e->argument_count; i++) {
        if (!feed(l, e->arguments[i].value, n, e->arguments[i].parameter)) {
            return NULL;
        }
    }
    return n;
}

/* A scalar port: graph functions here carry plain numbers and flags. */
static bool scalar(const script_type type)
{
    return !type.nominal && !type.container && type.storage != PIN_STRUCT &&
           type.storage != PIN_HANDLE && type.storage != PIN_ENUM;
}

static void add_pin(graph *g, node *n, int output, const script_symbol *s)
{
    pin_spec pin = { .name = graph_own_str(g, s->name, strlen(s->name)),
                     .type = s->type.storage };
    int added = graph_add_pin(g, n, output, &pin);
    assert(added >= 0);
    (void)added;
}

/* The function's only statement: a return that assigns each result once. */
static const script_statement *returned(const script_analysis *a, const script_function *f)
{
    const script_statement *body = &a->statements[f->body];
    if (body->kind == SCRIPT_STMT_BLOCK && body->child_count == 1) {
        body = &a->statements[body->children[0]];
    }
    if (body->kind != SCRIPT_STMT_RETURN || body->child_count != f->result_count) {
        return NULL;
    }
    for (int i = 0; i < body->child_count; i++) {
        const script_statement *assign = &a->statements[body->children[i]];
        if (assign->kind != SCRIPT_STMT_ASSIGN || assign->symbol != f->results[i]) {
            return NULL;
        }
    }
    return body;
}

/* Scalar ports, and parameter defaults that are constants for the entry pins. */
static bool lowerable(const script_analysis *a, const script_function *f)
{
    if (f->kind != SCRIPT_FUNCTION_AUTHORED || !f->pure || f->symbol < 0 ||
        f->type_parameter_count || !f->result_count || !returned(a, f)) {
        return false;
    }
    for (int i = 0; i < f->parameter_count; i++) {
        const script_symbol *p = &a->symbols[f->parameters[i]];
        if (!scalar(p->type) ||
            (p->initializer >= 0 &&
             a->expressions[p->initializer].kind != SCRIPT_EXPR_CONSTANT)) {
            return false;
        }
    }
    for (int i = 0; i < f->result_count; i++) {
        if (!scalar(a->symbols[f->results[i]].type)) {
            return false;
        }
    }
    return true;
}

static void lower_function(
    behavior_asset *asset,
    const script_catalog *catalog,
    const script_analysis *a,
    const script_function *f
)
{
    if (!lowerable(a, f)) {
        return;
    }
    graph *g = behavior_asset_add_function(asset, a->symbols[f->symbol].name, true);
    assert(g);
    lowering l = { a, catalog, f, g, graph_node_by_id(g, g->entry_id) };
    node *ret = graph_node_by_id(g, g->return_id);
    bool fed = true;
    for (int i = 0; i < f->parameter_count; i++) {
        const script_symbol *p = &a->symbols[f->parameters[i]];
        add_pin(g, l.entry, 1, p);
        if (p->initializer >= 0) {
            const pin_value *value = &a->expressions[p->initializer].constant;
            fed &= !graph_set_pin_default(g, l.entry, 1, i, value);
        }
    }
    for (int i = 0; i < f->result_count; i++) {
        add_pin(g, ret, 0, &a->symbols[f->results[i]]);
    }
    const script_statement *exit = returned(a, f);
    for (int i = 0; fed && i < exit->child_count; i++) {
        fed = feed(&l, a->statements[exit->children[i]].second, ret, i);
    }
    if (!fed) {
        int deleted = behavior_asset_delete_graph(asset, g->graph_id);
        assert(!deleted);
        (void)deleted;
    }
}

behavior_asset **authoring_source_graph_libraries(const authoring_sources *sources)
{
    behavior_asset **libraries = NULL;
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        const script_analysis *a = source->analysis;
        if (!source->valid || a->kind != SCRIPT_PROGRAM_LIBRARY) {
            continue;
        }
        behavior_asset *asset = behavior_asset_new(a->name);
        asset->domain = BEHAVIOR_DOMAIN_LIBRARY;
        for (int f = 0; f < a->function_count; f++) {
            lower_function(asset, authoring_sources_catalog(sources), a, &a->functions[f]);
        }
        apush(libraries, asset);
    }
    return libraries;
}

void authoring_source_graph_libraries_free(behavior_asset **libraries)
{
    for (int i = 0; i < acount(libraries); i++) {
        behavior_asset_free(libraries[i]);
    }
    afree(libraries);
}
