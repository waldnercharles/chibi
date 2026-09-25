#include "automation/graph_optimize.h"

#include "graph/authoring/layout.h"
#include "graph/authoring/optimize.h"
#include "graph/document/serialize.h"
#include "graph/model/behavior_asset.h"
#include "graph/nodes/behavior_expression.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/yyjson/yyjson.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    GRAPH_OPTIMIZE_MAX_FANOUT = 64,
};

typedef struct
{
    behavior_asset *asset;
    graph *graph;
    yyjson_mut_doc *doc;
    yyjson_mut_val *result;
    char *error;
    size_t error_size;
} optimize_context;

static void set_error(optimize_context *context, const char *format, ...)
{
    if (!context->error || !context->error_size) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(context->error, context->error_size, format, arguments);
    va_end(arguments);
}

static int fail(optimize_context *context, int status, const char *format, ...)
{
    if (context->error && context->error_size) {
        va_list arguments;
        va_start(arguments, format);
        char message[512];
        vsnprintf(message, sizeof message, format, arguments);
        va_end(arguments);
        snprintf(context->error, context->error_size, "%s", message);
    }
    yyjson_mut_obj_add_strcpy(
        context->doc,
        context->result,
        "error",
        context->error ? context->error : "graph optimization failed"
    );
    return status;
}

static bool request_bool(
    optimize_context *context,
    yyjson_val *request,
    const char *key,
    bool fallback,
    bool *out
)
{
    yyjson_val *value = yyjson_obj_get(request, key);
    if (!value) {
        *out = fallback;
        return true;
    }
    if (!yyjson_is_bool(value)) {
        set_error(context, "%s must be a boolean", key);
        return false;
    }
    *out = yyjson_get_bool(value);
    return true;
}

static bool request_int(
    optimize_context *context,
    yyjson_val *request,
    const char *key,
    int fallback,
    int minimum,
    int maximum,
    int *out
)
{
    yyjson_val *value = yyjson_obj_get(request, key);
    if (!value) {
        *out = fallback;
        return true;
    }
    if (!yyjson_is_int(value)) {
        set_error(context, "%s must be an integer", key);
        return false;
    }
    int64_t number = yyjson_get_sint(value);
    if (number < minimum || number > maximum) {
        set_error(context, "%s must be between %d and %d", key, minimum, maximum);
        return false;
    }
    *out = (int)number;
    return true;
}

static graph *select_graph(optimize_context *context, yyjson_val *request)
{
    yyjson_val *id_value = yyjson_obj_get(request, "graph_id");
    yyjson_val *name_value = yyjson_obj_get(request, "graph_name");
    if (id_value && name_value) {
        set_error(context, "use graph_id or graph_name, not both");
        return NULL;
    }
    if (id_value) {
        if (!yyjson_is_uint(id_value) &&
            (!yyjson_is_sint(id_value) || yyjson_get_sint(id_value) < 0)) {
            set_error(context, "graph_id must be a non-negative integer");
            return NULL;
        }
        uint64_t id = yyjson_is_uint(id_value)
                        ? yyjson_get_uint(id_value)
                        : (uint64_t)yyjson_get_sint(id_value);
        graph *selected = behavior_asset_find_graph(context->asset, id);
        if (!selected) {
            set_error(context, "graph id %llu does not exist", (unsigned long long)id);
        }
        return selected;
    }
    if (name_value) {
        if (!yyjson_is_str(name_value) || !yyjson_get_len(name_value)) {
            set_error(context, "graph_name must be a non-empty string");
            return NULL;
        }
        graph *selected = behavior_asset_find_graph_by_name(context->asset, yyjson_get_str(name_value));
        if (!selected) {
            set_error(context, "graph '%s' does not exist", yyjson_get_str(name_value));
        }
        return selected;
    }
    return context->asset->graph;
}

static int add_report(
    optimize_context *context,
    const graph_optimize_report *report,
    int before_nodes,
    int before_edges
)
{
    const int changed = report->folded_inputs || report->removed_nodes ||
                        report->created_aliases || report->changed_edges;
    if (changed) {
        graph_layout(context->graph);
    }
    yyjson_mut_obj_add_int(context->doc, context->result, "before_nodes", before_nodes);
    yyjson_mut_obj_add_int(context->doc, context->result, "before_edges", before_edges);
    yyjson_mut_obj_add_int(
        context->doc,
        context->result,
        "after_nodes",
        acount(context->graph->nodes)
    );
    yyjson_mut_obj_add_int(
        context->doc,
        context->result,
        "after_edges",
        acount(context->graph->edges)
    );
    yyjson_mut_obj_add_int(
        context->doc,
        context->result,
        "folded_inputs",
        report->folded_inputs
    );
    yyjson_mut_obj_add_int(
        context->doc,
        context->result,
        "removed_nodes",
        report->removed_nodes
    );
    yyjson_mut_obj_add_int(
        context->doc,
        context->result,
        "created_aliases",
        report->created_aliases
    );
    yyjson_mut_obj_add_int(
        context->doc,
        context->result,
        "changed_edges",
        report->changed_edges
    );
    yyjson_mut_obj_add_int(
        context->doc,
        context->result,
        "optimized",
        report->removed_nodes + report->created_aliases
            ? report->removed_nodes + report->created_aliases
            : changed
    );
    yyjson_mut_val *skipped = yyjson_mut_obj_add_arr(context->doc, context->result, "skipped");
    for (int i = 0; i < acount(report->skipped); i++) {
        const graph_optimize_skip *item = &report->skipped[i];
        yyjson_mut_val *value = yyjson_mut_arr_add_obj(context->doc, skipped);
        yyjson_mut_obj_add_uint(context->doc, value, "node_id", item->node_id);
        yyjson_mut_obj_add_uint(context->doc, value, "pin_id", item->pin_id);
        yyjson_mut_obj_add_strcpy(context->doc, value, "reason", graph_optimize_skip_name(item->reason));
    }
    return changed;
}

int automation_graph_optimize(
    const char *source_json,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char **edited_json,
    char *error,
    size_t error_size
)
{
    if (edited_json) {
        *edited_json = NULL;
    }
    if (!source_json || !request || !yyjson_is_obj(request) || !doc ||
        !result || !edited_json) {
        if (error && error_size) {
            snprintf(error, error_size, "graph optimization requires a source and request object");
        }
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    behavior_asset *asset = behavior_asset_load_json(source_json);
    if (!asset) {
        if (error && error_size) {
            snprintf(error, error_size, "could not load asset: %s", graph_last_error());
        }
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    optimize_context context = {
        .asset = asset,
        .doc = doc,
        .result = result,
        .error = error,
        .error_size = error_size,
    };
    context.graph = select_graph(&context, request);
    if (!context.graph) {
        int status = fail(&context, CHIBI_AUTOMATION_INVALID_REQUEST, "%s", error);
        behavior_asset_free(asset);
        return status;
    }
    if (acount(context.graph->nodes) > 512 || acount(context.graph->edges) > 4096) {
        behavior_asset_free(asset);
        return fail(&context, CHIBI_AUTOMATION_INVALID_REQUEST, "optimizer is bounded to 512 nodes and 4096 edges");
    }
    int fanout = 0;
    bool preserve_debug = true;
    bool local_aliases = true;
    bool math_expressions = false;
    bool dry_run = true;
    if (!request_int(
            &context,
            request,
            "fanout_threshold",
            graph_optimize_defaults().fanout_threshold,
            2,
            GRAPH_OPTIMIZE_MAX_FANOUT,
            &fanout
        ) ||
        !request_bool(&context, request, "preserve_debug", true, &preserve_debug) ||
        !request_bool(&context, request, "local_aliases", true, &local_aliases) ||
        !request_bool(&context, request, "math_expressions", false, &math_expressions) ||
        !request_bool(&context, request, "dry_run", true, &dry_run)) {
        int status = fail(
            &context,
            CHIBI_AUTOMATION_INVALID_REQUEST,
            "%s",
            error && error[0] ? error : "invalid graph optimization options"
        );
        behavior_asset_free(asset);
        return status;
    }
    (void)dry_run;
    yyjson_mut_obj_add_uint(doc, result, "graph_id", context.graph->graph_id);
    const int before_nodes = acount(context.graph->nodes);
    const int before_edges = acount(context.graph->edges);
    graph_optimize_options options = graph_optimize_defaults();
    options.fanout_threshold = fanout;
    options.preserve_debug = preserve_debug;
    options.local_aliases = local_aliases;
    /* The asset was loaded into a private clone, so applying the optimizer here
     * gives dry runs and publishes the same exact transformed graph. The caller
     * decides whether the returned text is published. */
    options.dry_run = false;
    /* Math becomes expressions first, so values used three or more times
       leave the expressions and become named locals. */
    int expressions = 0;
    if (math_expressions) {
        behavior_expression_error expression_error;
        expressions = behavior_expression_collapse_math(asset, context.graph, 3, &expression_error);
        if (expressions < 0) {
            int status = fail(
                &context,
                CHIBI_AUTOMATION_VALIDATION_ERROR,
                "math could not become expressions: %s",
                expression_error.message
            );
            behavior_asset_free(asset);
            return status;
        }
    }
    graph_optimize_report report = { 0 };
    if (graph_optimize(context.graph, &options, &report) < 0) {
        graph_optimize_report_free(&report);
        int status = fail(&context, CHIBI_AUTOMATION_VALIDATION_ERROR, "graph optimization could not be applied");
        behavior_asset_free(asset);
        return status;
    }
    report.changed_edges += expressions;
    yyjson_mut_obj_add_int(doc, result, "created_expressions", expressions);
    bool changed = add_report(&context, &report, before_nodes, before_edges);
    /* Use the source bytes for a no-op, keeping a dry run side-effect free and
     * avoiding a formatting-only publish. */
    char *serialized = changed ? behavior_asset_save_json(asset)
                               : xstrdup(source_json);
    graph_optimize_report_free(&report);
    behavior_asset_free(asset);
    if (!serialized) {
        if (error && error_size) {
            snprintf(error, error_size, "could not serialize optimized asset");
        }
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    *edited_json = serialized;
    return CHIBI_AUTOMATION_OK;
}
