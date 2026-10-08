#include "authoring/source_prepare.h"

#include "authoring/source_prepare_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

authoring_source *authoring_source_analyze(
    const char *path,
    const char *key,
    const script_syntax *syntax,
    const script_catalog *catalog
)
{
    assert(path && syntax && catalog);
    authoring_source *source = xcalloc(1, sizeof *source);
    source->path = xstrdup(path);
    source->syntax = xcalloc(1, sizeof *source->syntax);
    source->syntax->text = xstrdup(syntax->text);
    source->syntax->tree = ts_tree_copy(syntax->tree);
    for (int i = 0; i < syntax->diagnostic_count; i++) {
        apush(source->syntax->diagnostics, syntax->diagnostics[i]);
    }
    source->syntax->diagnostic_count = acount(source->syntax->diagnostics);
    source->analysis = key ? script_analyze_document(source->syntax, catalog, key)
                           : script_analyze(source->syntax, catalog);
    for (int i = 0; i < source->analysis->diagnostic_count; i++) {
        apush(source->diagnostics, source->analysis->diagnostics[i]);
    }
    source->diagnostic_count = acount(source->diagnostics);
    return source;
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
                                              diagnostic->range.column } };
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

void authoring_source_free(authoring_source *source)
{
    if (!source) {
        return;
    }
    execution_module_free(source->module);
    script_analysis_free(source->analysis);
    script_syntax_free(source->syntax);
    afree(source->diagnostics);
    free(source->path);
    free(source);
}
