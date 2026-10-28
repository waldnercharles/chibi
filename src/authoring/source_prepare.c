#include "authoring/source_prepare.h"

#include "authoring/source_prepare_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void performance_diagnostics(authoring_source *source)
{
    static const char *const names[] = { "allocation", "copy", "escape" };
    for (int i = 0; i < execution_module_function_count(source->module); i++) {
        execution_module_function_info
            function = execution_module_function_at(source->module, i);
        if (!function.body) {
            continue;
        }
        execution_cost_summary costs = behavior_function_costs(function.body);
        authoring_function_costs facts = { .function = function.id,
                                           .entry = function.entry ? function.entry
                                                                   : function.event_name,
                                           .summary = costs };
        for (int at = 0; at < behavior_function_source_count(function.body); at++) {
            const behavior_function_source
                *origin = behavior_function_source_at(function.body, at);
            if (origin->asset_name && origin->asset_name[0] && origin->line > 0) {
                facts.origin = *origin;
                if (!strcmp(origin->asset_name, source->analysis->name)) {
                    facts.origin.asset_name = source->path;
                }
                break;
            }
        }
        apush(source->costs, facts);
        for (int possible = 0; possible < 2; possible++) {
            unsigned flags = possible ? costs.possible : costs.proven;
            const execution_cost_site *sites = possible ? costs.possible_sites
                                                        : costs.proven_sites;
            for (int bit = 0; bit < EXECUTION_COST_KINDS; bit++) {
                if (!(flags & (1u << bit))) {
                    continue;
                }
                authoring_performance_diagnostic d = { .function = function.id,
                                                       .cost = 1u << bit,
                                                       .possible = possible != 0 };
                const behavior_function_source *origin = sites[bit].function
                                                           ? behavior_function_instruction_source(
                                                                 sites[bit].function,
                                                                 sites[bit].instruction
                                                             )
                                                           : NULL;
                if (origin) {
                    d.origin = *origin;
                }
                snprintf(
                    d.message,
                    sizeof d.message,
                    "%s %s operation: %s",
                    possible ? "Possible" : "Proven",
                    names[bit],
                    sites[bit].reason
                );
                apush(source->performance, d);
            }
        }
    }
    source->performance_count = acount(source->performance);
    source->cost_count = acount(source->costs);
}

authoring_source *authoring_source_analyze_requested(
    const char *path,
    const char *key,
    const script_syntax *syntax,
    const script_catalog *catalog,
    const script_specialization_request *requests,
    int request_count
)
{
    assert(path && syntax && catalog);
    authoring_source *source = xcalloc(1, sizeof *source);
    source->references = 1;
    source->path = xstrdup(path);
    source->syntax = xcalloc(1, sizeof *source->syntax);
    source->syntax->text = xstrdup(syntax->text);
    source->syntax->tree = ts_tree_copy(syntax->tree);
    for (int i = 0; i < syntax->diagnostic_count; i++) {
        apush(source->syntax->diagnostics, syntax->diagnostics[i]);
    }
    source->syntax->diagnostic_count = acount(source->syntax->diagnostics);
    source->analysis = request_count
                         ? script_analyze_document_requested(source->syntax, catalog, key, requests, request_count)
                     : key ? script_analyze_document(source->syntax, catalog, key)
                           : script_analyze(source->syntax, catalog);
    for (int i = 0; i < source->analysis->diagnostic_count; i++) {
        apush(source->diagnostics, source->analysis->diagnostics[i]);
    }
    source->diagnostic_count = acount(source->diagnostics);
    return source;
}

authoring_source *authoring_source_analyze(
    const char *path,
    const char *key,
    const script_syntax *syntax,
    const script_catalog *catalog
)
{
    return authoring_source_analyze_requested(path, key, syntax, catalog, NULL, 0);
}

void authoring_source_lower(
    authoring_source *source,
    const script_catalog *catalog,
    const script_lower_bindings *bindings,
    behavior_backend backend
)
{
    assert(source && !source->module && catalog && bindings);
    asetlen(source->diagnostics, 0);
    execution_module_plan *plan = script_lower(source->analysis, catalog, bindings);
    for (int i = 0; i < plan->diagnostic_count; i++) {
        const execution_module_diagnostic *diagnostic = &plan->diagnostics[i];
        script_diagnostic copy = { .range = { diagnostic->range.start,
                                              diagnostic->range.end,
                                              diagnostic->range.line,
                                              diagnostic->range.column,
                                              diagnostic->range.document } };
        memcpy(copy.message, diagnostic->message, sizeof copy.message);
        apush(source->diagnostics, copy);
    }
    if (plan->valid) {
        char error[192] = { 0 };
        source->module = execution_module_finish(&plan, backend, error, sizeof error);
        if (!source->module) {
            script_diagnostic diagnostic = { .range = { .line = 1, .column = 1 } };
            snprintf(diagnostic.message, sizeof diagnostic.message, "%s", error);
            apush(source->diagnostics, diagnostic);
        }
    }
    execution_module_plan_free(plan);
    source->diagnostic_count = acount(source->diagnostics);
    source->valid = source->module && !source->diagnostic_count;
    if (source->valid) {
        performance_diagnostics(source);
    }
}

authoring_source *authoring_source_prepare(
    const char *path,
    const char *text,
    const script_catalog *catalog,
    const script_lower_bindings *bindings,
    behavior_backend backend
)
{
    return authoring_source_prepare_document(path, NULL, text, catalog, bindings, backend);
}
authoring_source *authoring_source_prepare_document(
    const char *path,
    const char *key,
    const char *text,
    const script_catalog *catalog,
    const script_lower_bindings *bindings,
    behavior_backend backend
)
{
    script_syntax *syntax = script_parse(text);
    authoring_source *source = authoring_source_analyze(path, key, syntax, catalog);
    script_syntax_free(syntax);
    authoring_source_lower(source, catalog, bindings, backend);
    return source;
}

authoring_source *authoring_source_retain(authoring_source *source)
{
    assert(source && source->references > 0 && source->references < INT_MAX);
    source->references++;
    return source;
}
void authoring_source_free(authoring_source *source)
{
    if (!source) {
        return;
    }
    assert(source->references > 0);
    if (--source->references) {
        return;
    }
    execution_module_free(source->module);
    script_analysis_free(source->analysis);
    script_syntax_free(source->syntax);
    afree(source->diagnostics);
    afree(source->performance);
    afree(source->costs);
    free(source->path);
    free(source);
}
