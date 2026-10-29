#include "authoring/source_graph_library.h"

#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One library's lowering. Each function lowers once, before its first caller
 * in any library; its graph is NULL while it lowers and when it cannot, so a
 * recursive call, or one to a function that cannot lower, is left out. */
typedef struct
{
    const script_catalog *catalog;
    const script_analysis *analysis;
    behavior_asset *asset;
    graph **graphs;
    bool *visited;
} library;
typedef struct
{
    library *all; /* array.h vector of every source library */
    library *library;
    const script_function *function;
    graph *graph;
    node *entry;
} lowering;

static graph *lower_function(library *all, library *, int function);

static const script_catalog_capability *capability(const lowering *l, uint64_t id)
{
    const script_catalog *catalog = l->library->catalog;
    for (int i = 0; i < catalog->capability_count; i++) {
        if (catalog->capabilities[i].id == id) {
            return &catalog->capabilities[i];
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

/* A call becomes a node fed by its arguments. */
static node *call(const lowering *l, const script_expression *e);

/* Feeds `to`'s input from an expression: a constant becomes the input's
 * default, a parameter an entry output and a call its node. */
static bool feed(const lowering *l, int expression, node *to, int input)
{
    const script_expression *e = &l->library->analysis->expressions[expression];
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

/* A call to another function of this library calls its graph, lowered first.
 * An expression takes one value, so the function returns one. */
static node *function_call(const lowering *l, int function)
{
    graph *target = lower_function(l->all, l->library, function);
    if (!target || graph_node_by_id(target, target->return_id)->input_count != 1) {
        return NULL;
    }
    return behavior_asset_add_function_call_node_to_graph(l->library->asset, l->graph, target);
}

/* Another library's function, called by name: the field that links this
 * library links that one too. Pins and defaults follow the catalog. */
static node *library_call(const lowering *l, const script_catalog_capability *c, const char *library)
{
    if (c->result_count != 1) {
        return NULL;
    }
    graph *g = l->graph;
    node *n = graph_add_node(g, &NODE_CallPureFunction);
    assert(n);
    int named = graph_set_node_reference(g, n, 0, c->name) |
                graph_set_node_reference_asset(g, n, library);
    assert(!named);
    (void)named;
    for (int side = 0; side < 2; side++) {
        const script_catalog_parameter *pins = side ? c->results : c->parameters;
        int count = side ? c->result_count : c->parameter_count;
        for (int i = 0; i < count; i++) {
            pin_spec pin = {
                .name = graph_own_str(g, pins[i].name, strlen(pins[i].name)),
                .type = pins[i].type.storage
            };
            int added = graph_add_pin(g, n, side, &pin);
            assert(added == i);
            (void)added;
            if (pins[i].has_default &&
                graph_set_pin_default(g, n, side, i, &pins[i].default_value)) {
                return NULL;
            }
        }
    }
    return n;
}

/* A capability without a node may be another source library's function,
 * which lowers first: the call needs its graph. */
static node *source_call(const lowering *l, const script_catalog_capability *c)
{
    for (int i = 0; i < acount(l->all); i++) {
        library *other = &l->all[i];
        const script_analysis *a = other->analysis;
        if (other == l->library) {
            continue;
        }
        for (int f = 0; f < a->function_count; f++) {
            int symbol = a->functions[f].symbol;
            if (symbol >= 0 && a->symbols[symbol].id == c->id) {
                return lower_function(l->all, other, f) ? library_call(l, c, a->name)
                                                        : NULL;
            }
        }
    }
    return NULL;
}

/* A native call becomes the graph node of its qualified name, as the graph
 * catalog declares it. The node and capability share every port, in order. */
static node *native_call(const lowering *l, uint64_t id)
{
    const script_catalog_capability *c = capability(l, id);
    if (!c || !c->pure) {
        return NULL;
    }
    const node_desc *desc = registry_node_by_name(
        c->qualified_name ? c->qualified_name : c->name
    );
    if (!desc) {
        return source_call(l, c);
    }
    if (desc->input_count != c->parameter_count || !desc->output_count) {
        return NULL;
    }
    node *n = graph_add_node(l->graph, desc);
    assert(n);
    return n;
}

static node *call(const lowering *l, const script_expression *e)
{
    node *n = e->right >= 0 ? function_call(l, e->right)
                            : native_call(l, e->capability);
    if (!n) {
        return NULL;
    }
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
           type.storage != PIN_HANDLE && type.storage != PIN_ENUM &&
           type.storage != PIN_POINTER;
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
        !f->result_count || !returned(a, f)) {
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

static graph *lower_function(library *all, library *lib, int function)
{
    if (lib->visited[function]) {
        return lib->graphs[function];
    }
    lib->visited[function] = true;
    const script_analysis *a = lib->analysis;
    const script_function *f = &a->functions[function];
    if (!lowerable(a, f)) {
        return NULL;
    }
    graph *g = behavior_asset_add_function(lib->asset, a->symbols[f->symbol].name, true);
    assert(g);
    lowering l = { all, lib, f, g, graph_node_by_id(g, g->entry_id) };
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
        int deleted = behavior_asset_delete_graph(lib->asset, g->graph_id);
        assert(!deleted);
        (void)deleted;
        return NULL;
    }
    lib->graphs[function] = g;
    return g;
}

static behavior_asset **graph_libraries(
    const script_catalog *catalog,
    const script_analysis *const *analyses,
    int count
)
{
    library *all = NULL;
    for (int i = 0; i < count; i++) {
        const script_analysis *a = analyses[i];
        assert(a->valid && a->kind == SCRIPT_PROGRAM_LIBRARY);
        library lib = { catalog,
                        a,
                        behavior_asset_new(a->name),
                        xcalloc((size_t)a->function_count + 1, sizeof(graph *)),
                        xcalloc((size_t)a->function_count + 1, sizeof(bool)) };
        lib.asset->domain = BEHAVIOR_DOMAIN_LIBRARY;
        lib.asset->transient_function_ids = true;
        apush(all, lib);
    }
    for (int i = 0; i < acount(all); i++) {
        for (int f = 0; f < all[i].analysis->function_count; f++) {
            lower_function(all, &all[i], f);
        }
    }
    behavior_asset **libraries = NULL;
    for (int i = 0; i < acount(all); i++) {
        free(all[i].graphs);
        free(all[i].visited);
        apush(libraries, all[i].asset);
    }
    afree(all);
    return libraries;
}

behavior_asset **authoring_source_graph_libraries(const authoring_sources *sources)
{
    const script_analysis **analyses = NULL;
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        if (source->valid && source->analysis->kind == SCRIPT_PROGRAM_LIBRARY) {
            apush(analyses, source->analysis);
        }
    }
    behavior_asset **libraries = graph_libraries(
        authoring_sources_catalog(sources),
        analyses,
        acount(analyses)
    );
    afree(analyses);
    return libraries;
}

bool authoring_source_graph_libraries_catalog(
    const source_catalog *sources,
    behavior_asset ***out,
    char *error,
    size_t size
)
{
    assert(sources && out && !*out && !source_catalog_diagnostic_count(sources));
    const script_catalog *catalog = source_catalog_view(sources);
    int request_count = 0;
    const script_specialization_request *requests = source_catalog_requests(sources, &request_count);
    script_analysis **analyses = NULL;
    bool valid = true;
    for (int i = 0; i < source_catalog_source_count(sources); i++) {
        const source_catalog_source *source = source_catalog_source_at(sources, i);
        if (source->declarations->kind != SCRIPT_PROGRAM_LIBRARY) {
            continue;
        }
        script_analysis *analysis = request_count ? script_analyze_document_requested(
                                                        source->syntax,
                                                        catalog,
                                                        source->identity,
                                                        requests,
                                                        request_count
                                                    )
                                                  : script_analyze_document(
                                                        source->syntax,
                                                        catalog,
                                                        source->identity
                                                    );
        apush(analyses, analysis);
        if (!analysis->valid) {
            assert(analysis->diagnostic_count);
            const script_diagnostic *diagnostic = &analysis->diagnostics[0];
            if (size) {
                snprintf(
                    error,
                    size,
                    "%s:%d:%d (byte %d): %s",
                    source->path,
                    diagnostic->range.line,
                    diagnostic->range.column,
                    diagnostic->range.start,
                    diagnostic->message
                );
            }
            valid = false;
            break;
        }
    }
    if (valid) {
        *out = graph_libraries(catalog, (const script_analysis *const *)analyses, acount(analyses));
    }
    for (int i = 0; i < acount(analyses); i++) {
        script_analysis_free(analyses[i]);
    }
    afree(analyses);
    return valid;
}

void authoring_source_graph_libraries_free(behavior_asset **libraries)
{
    for (int i = 0; i < acount(libraries); i++) {
        behavior_asset_free(libraries[i]);
    }
    afree(libraries);
}
