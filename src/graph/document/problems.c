#include "graph/document/problems.h"

#include "graph/document/library.h"
#include "graph/modeling/model_compile.h"
#include "support/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

behavior_asset *behavior_asset_validate(
    const behavior_asset *asset,
    const behavior_asset *const *catalog,
    int count,
    compile_report *report
)
{
    char message[256];
    behavior_asset *linked = behavior_asset_link_libraries(asset, catalog, count, message, sizeof message);
    if (linked) {
        if (linked->domain == BEHAVIOR_DOMAIN_MODEL) {
            model_program program;
            model_program_init(&program);
            shader_error error = { 0 };
            if (!model_compile_with_catalog(linked, catalog, count, &program, &error)) {
                compile_diagnostic diagnostic = { .severity = CMPL_ERROR,
                                                  .rule = CMPL_RULE_OPERATOR,
                                                  .graph_id = linked->graph->graph_id,
                                                  .node_id = error.node };
                snprintf(
                    diagnostic.message,
                    sizeof diagnostic.message,
                    "%s",
                    error.message
                );
                apush(report->items, diagnostic);
                report->error_count++;
            }
            model_program_free(&program);
        } else {
            graph_asset_compile(linked, report);
        }
        return linked;
    }
    compile_diagnostic diagnostic = { .severity = CMPL_ERROR,
                                      .rule = CMPL_RULE_MISSING_REFERENCE };
    snprintf(diagnostic.message, sizeof diagnostic.message, "%s", message[0] ? message : "Could not link project definitions.");
    apush(report->items, diagnostic);
    report->error_count++;
    return NULL;
}

/* A problem with the catalog position of its asset, and the order found. */
typedef struct
{
    catalog_problem problem;
    int order, sequence;
} found_problem;

static bool same_problem(const catalog_problem *a, const catalog_problem *b)
{
    return a->asset == b->asset && a->graph_id == b->graph_id &&
           a->node_id == b->node_id && a->severity == b->severity &&
           !strcmp(a->message, b->message);
}

static void add_problem(found_problem **found, found_problem problem)
{
    for (int i = 0; i < acount(*found); i++) {
        if (same_problem(&(*found)[i].problem, &problem.problem)) {
            return;
        }
    }
    problem.sequence = acount(*found);
    apush(*found, problem);
}

/* The catalog position of the asset a snapshot graph was copied from:
   `self` for its own graphs and macros, else the library by name. */
static int source_index(const behavior_asset *const *catalog, int count, int self, const char *source)
{
    if (!source || !strcmp(source, catalog[self]->name)) {
        return self;
    }
    for (int i = 0; i < count; i++) {
        if (!strcmp(catalog[i]->name, source)) {
            return i;
        }
    }
    return -1;
}

/* Where a diagnostic of catalog[self]'s linked snapshot was authored. */
static found_problem locate(
    const behavior_asset *const *catalog,
    int count,
    int self,
    const behavior_asset *linked,
    const compile_diagnostic *d
)
{
    found_problem found = { .problem = { .asset = catalog[self],
                                         .graph_id = d->graph_id,
                                         .node_id = d->node_id,
                                         .severity = d->severity },
                            .order = self };
    snprintf(found.problem.message, sizeof found.problem.message, "%s", d->message);
    const graph *g = linked ? behavior_asset_find_graph(linked, d->graph_id) : NULL;
    graph_node_source source = g ? graph_node_authored(g, d->node_id)
                                 : (graph_node_source){ 0 };
    int owner = g ? source_index(catalog, count, self, source.asset) : -1;
    if (owner < 0) {
        return found;
    }
    found.order = owner;
    found.problem.asset = catalog[owner];
    found.problem.graph_id = source.graph_id;
    found.problem.node_id = d->node_id ? source.node_id : 0;
    return found;
}

static int compare_found(const void *a, const void *b)
{
    const found_problem *x = a, *y = b;
    return x->order != y->order ? x->order - y->order : x->sequence - y->sequence;
}

catalog_problem *behavior_catalog_problems(const behavior_asset *const *catalog, int count)
{
    found_problem *found = NULL;
    for (int a = 0; a < count; a++) {
        compile_report report;
        compile_report_init(&report);
        behavior_asset *linked = behavior_asset_validate(catalog[a], catalog, count, &report);
        for (int i = 0; i < acount(report.items); i++) {
            const compile_diagnostic *d = &report.items[i];
            if (d->severity == CMPL_ERROR || d->severity == CMPL_WARNING) {
                add_problem(&found, locate(catalog, count, a, linked, d));
            }
        }
        compile_report_free(&report);
        behavior_asset_free(linked);
    }
    if (acount(found) > 1) {
        asort(found, compare_found);
    }
    catalog_problem *problems = NULL;
    for (int i = 0; i < acount(found); i++) {
        apush(problems, found[i].problem);
    }
    afree(found);
    return problems;
}
