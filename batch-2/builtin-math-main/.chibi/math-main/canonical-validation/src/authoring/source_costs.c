#include "authoring/source_costs.h"

#include <stdio.h>
#include <string.h>

#ifndef __EMSCRIPTEN__
#include "authoring/asset_compiler_host_internal.h"

static void source_fields(av_doc *doc, av_value *row, const behavior_function_source *source)
{
    av_obj_add_strcpy(doc, row, "path", source->asset_name ? source->asset_name : "");
    av_obj_add_int(doc, row, "start", source->start);
    av_obj_add_int(doc, row, "end", source->end);
    av_obj_add_int(doc, row, "line", source->line);
    av_obj_add_int(doc, row, "column", source->column);
}

/* Single-document lowering can retain the declaration name as its origin.
 * Resolve that name against this exact prepared collection, not the disk. */
static behavior_function_source source_origin(
    const authoring_sources *sources,
    const behavior_function_source *origin
)
{
    behavior_function_source result = origin ? *origin
                                             : (behavior_function_source){ 0 };
    if (!result.asset_name) {
        return result;
    }
    const char *path = NULL;
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        if (!strcmp(result.asset_name, source->analysis->name)) {
            if (path) {
                result.asset_name = NULL;
                return result;
            }
            path = source->path;
        }
    }
    if (path) {
        result.asset_name = path;
    }
    return result;
}

static void cost_rows(
    av_doc *doc,
    av_value *rows,
    const authoring_sources *sources,
    int function,
    const execution_cost_summary *summary,
    bool possible
)
{
    unsigned flags = possible ? summary->possible : summary->proven;
    const execution_cost_site *sites = possible ? summary->possible_sites
                                                : summary->proven_sites;
    for (int bit = 0; bit < EXECUTION_COST_KINDS; bit++) {
        if (!(flags & (1u << bit))) {
            continue;
        }
        av_value *row = av_arr_add_obj(doc, rows);
        av_obj_add_int(doc, row, "function", function);
        av_obj_add_int(doc, row, "kind", 1u << bit);
        av_obj_add_bool(doc, row, "possible", possible);
        av_obj_add_uint(doc, row, "capability", sites[bit].capability);
        av_obj_add_strcpy(doc, row, "reason", sites[bit].reason ? sites[bit].reason : "");
        const behavior_function_source *origin = sites[bit].function &&
                                                         sites[bit].instruction >= 0
                                                   ? behavior_function_instruction_source(
                                                         sites[bit].function,
                                                         sites[bit].instruction
                                                     )
                                                   : NULL;
        behavior_function_source resolved = source_origin(sources, origin);
        source_fields(doc, row, &resolved);
    }
}
#endif

char *authoring_sources_cost_report(const authoring_sources *sources, bool json, char *error, size_t size)
{
    if (!sources || !authoring_sources_valid(sources)) {
        snprintf(error, size, "Cost reporting requires a valid prepared source collection");
        return NULL;
    }
#ifdef __EMSCRIPTEN__
    (void)json;
    snprintf(error, size, "Cost report formatting requires a native authoring host");
    return NULL;
#else
    av_doc *doc = av_doc_new();
    av_value *args = av_arr(doc), *functions = av_arr(doc), *costs = av_arr(doc);
    av_doc_set_root(doc, args);
    av_arr_append(args, functions);
    av_arr_append(args, costs);
    av_arr_append(args, av_bool(doc, json));
    int count = 0;
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        for (int j = 0; j < source->cost_count; j++) {
            const authoring_function_costs *facts = &source->costs[j];
            av_value *row = av_arr_add_obj(doc, functions);
            av_obj_add_uint(doc, row, "id", facts->function);
            av_obj_add_strcpy(doc, row, "name", facts->entry ? facts->entry : "");
            av_obj_add_bool(doc, row, "complete", facts->summary.complete);
            source_fields(doc, row, &facts->origin);
            cost_rows(doc, costs, sources, count, &facts->summary, false);
            cost_rows(doc, costs, sources, count, &facts->summary, true);
            count++;
        }
    }
    asset_compiler_options options = *asset_source_default_options();
    options.cache_directory = "";
    asset_compiler_diagnostic diagnostic;
    char *report = asset_source_invoke_bytes(
        "project:asset_compiler:1:ExplainCosts:callable",
        args,
        &options,
        &diagnostic
    );
    av_doc_free(doc);
    if (!report) {
        snprintf(error, size, "Cost reporter failed: %s", diagnostic.message);
    }
    return report;
#endif
}
