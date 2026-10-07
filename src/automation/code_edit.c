#include "automation/code_edit.h"

#include "assets/code_source.h"
#include "assets/code_source_diagnostics.h"
#include "assets/code_source_intelligence.h"
#include "automation/controller.h"
#include "graph/document/problems.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/asset_json.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int invalid(char *error, size_t size, const char *message)
{
    snprintf(error, size, "%s", message);
    return CHIBI_AUTOMATION_INVALID_REQUEST;
}

int automation_code_analyze(
    yyjson_val *request,
    bool compile,
    const code_source_context *context,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t size
)
{
    yyjson_val *source_value = yyjson_obj_get(request, "source");
    const char *text = yyjson_get_str(source_value);
    if (!text || strlen(text) != yyjson_get_len(source_value) || strlen(text) > INT_MAX) {
        return invalid(error, size, "source must be a string without embedded NUL and with byte length representable as int");
    }
    yyjson_val *position = yyjson_obj_get(request, "offset");
    if (position &&
        (!yyjson_is_uint(position) || yyjson_get_uint(position) > strlen(text))) {
        return invalid(error, size, "offset must be a byte position within source");
    }
    yyjson_val *rename = yyjson_obj_get(request, "rename");
    if (rename && (!position || !yyjson_is_str(rename) ||
                   yyjson_get_len(rename) != strlen(yyjson_get_str(rename)))) {
        return invalid(error, size, "rename requires offset and an identifier string without embedded NUL");
    }
    code_source *source = code_source_parse_context(text, NULL, context);
    yyjson_val *metadata_value = yyjson_obj_get(request, "metadata");
    const char *metadata_text = yyjson_get_str(metadata_value);
    if (metadata_text && strlen(metadata_text) == yyjson_get_len(metadata_value)) {
        code_source_metadata_restore(source, metadata_text);
    }
    behavior_asset *asset = code_source_build_behavior(source);
    if (compile && asset) {
        const behavior_asset **catalog = NULL;
        for (int i = 0; context && i < context->library_count; i++) {
            apush(catalog, context->libraries[i]);
        }
        for (int i = 0; context && i < context->event_count; i++) {
            apush(catalog, context->events[i]);
        }
        compile_report report;
        compile_report_init(&report);
        behavior_asset *linked = behavior_asset_validate(asset, catalog, acount(catalog), &report);
        code_source_compile_diagnostics(source, linked, &report);
        bool blocked = !linked || compile_report_blocked(&report);
        behavior_asset_free(linked);
        compile_report_free(&report);
        afree(catalog);
        if (blocked) {
            behavior_asset_free(asset);
            asset = NULL;
        }
    }
    yyjson_mut_obj_add_bool(doc, result, "valid", asset != NULL);
    yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(doc, result, "diagnostics");
    for (int i = 0; i < acount(source->diagnostics); i++) {
        const code_source_diagnostic *d = &source->diagnostics[i];
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, diagnostics);
        yyjson_mut_obj_add_strcpy(doc, item, "message", d->message);
        yyjson_mut_obj_add_int(doc, item, "start", d->start);
        yyjson_mut_obj_add_int(doc, item, "end", d->end);
        yyjson_mut_obj_add_int(doc, item, "line", d->line);
        yyjson_mut_obj_add_int(doc, item, "column", d->column);
        yyjson_mut_obj_add_int(doc, item, "severity", d->severity);
        yyjson_mut_obj_add_int(doc, item, "rule", d->rule);
        yyjson_mut_obj_add_uint(doc, item, "graph_id", d->graph_id);
        yyjson_mut_obj_add_uint(doc, item, "node_id", d->node_id);
        yyjson_mut_obj_add_uint(doc, item, "pin_id", d->pin_id);
        if (d->asset_name) {
            yyjson_mut_obj_add_strcpy(doc, item, "asset", d->asset_name);
        }
        if (d->graph_name) {
            yyjson_mut_obj_add_strcpy(doc, item, "graph", d->graph_name);
        }
    }
    yyjson_mut_val *spans = yyjson_mut_obj_add_arr(doc, result, "spans");
    for (int i = 0; i < acount(source->spans); i++) {
        const asset_source_span *s = &source->spans[i];
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, spans);
        yyjson_mut_obj_add_int(doc, item, "start", s->start);
        yyjson_mut_obj_add_int(doc, item, "end", s->end);
        yyjson_mut_obj_add_strcpy(doc, item, "class", s->cls);
    }
    asset_source_span *semantic = code_intelligence_highlights(source);
    yyjson_mut_val *semantic_spans = yyjson_mut_obj_add_arr(doc, result, "semantic_spans");
    for (int i = 0; i < acount(semantic); i++) {
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, semantic_spans);
        yyjson_mut_obj_add_int(doc, item, "start", semantic[i].start);
        yyjson_mut_obj_add_int(doc, item, "end", semantic[i].end);
        yyjson_mut_obj_add_strcpy(doc, item, "class", semantic[i].cls);
    }
    afree(semantic);
    if (source->document) {
        yyjson_mut_obj_add_val(
            doc,
            result,
            "source_document",
            av_to_json(doc, av_doc_root(source->document))
        );
    }
    if (asset) {
        yyjson_mut_obj_add_strcpy(doc, result, "kind", code_source_kind(source));
        yyjson_mut_obj_add_strcpy(doc, result, "name", asset->name);
        char *metadata = NULL;
        char *formatted = code_source_is_structured(text)
                            ? code_source_persist(source)
                            : code_source_format_with_metadata(asset, &metadata);
        if (code_source_is_structured(text)) {
            metadata = code_source_metadata_export_source(source);
        }
        code_source *reopened_source = formatted
                                         ? code_source_parse_context(formatted, NULL, context)
                                         : NULL;
        if (reopened_source && metadata) {
            code_source_metadata_restore(reopened_source, metadata);
        }
        behavior_asset *reopened = reopened_source
                                     ? code_source_build_behavior(reopened_source)
                                     : NULL;
        if (reopened && behavior_asset_equal(asset, reopened)) {
            yyjson_mut_obj_add_strcpy(doc, result, "formatted", formatted);
            if (metadata) {
                yyjson_mut_obj_add_strcpy(doc, result, "metadata", metadata);
            }
        } else {
            yyjson_mut_obj_put(result, yyjson_mut_str(doc, "valid"), yyjson_mut_bool(doc, false));
            yyjson_mut_val *failure = yyjson_mut_arr_add_obj(doc, diagnostics);
            yyjson_mut_obj_add_str(doc, failure, "message", "Persisted source does not preserve the complete native asset");
            yyjson_mut_obj_add_int(doc, failure, "start", 0);
            yyjson_mut_obj_add_int(doc, failure, "end", (int)strlen(text));
            yyjson_mut_obj_add_int(doc, failure, "line", 1);
            yyjson_mut_obj_add_int(doc, failure, "column", 1);
            yyjson_mut_obj_add_int(doc, failure, "severity", 1);
            yyjson_mut_obj_add_int(doc, failure, "rule", 0);
        }
        free(metadata);
        behavior_asset_free(reopened);
        code_source_free(reopened_source);
        free(formatted);
        av_doc *graph_document = av_doc_new();
        yyjson_mut_obj_add_val(
            doc,
            result,
            "document",
            av_to_json(doc, behavior_asset_to_value(graph_document, asset))
        );
        av_doc_free(graph_document);
    }
    if (position) {
        int offset = (int)yyjson_get_uint(position);
        asset_source_completions completions = { 0 };
        code_intelligence_complete(source, offset, &completions);
        yyjson_mut_val *items = yyjson_mut_obj_add_arr(doc, result, "completions");
        for (int i = 0; i < acount(completions.items); i++) {
            const asset_source_completion *c = &completions.items[i];
            yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, items);
            yyjson_mut_obj_add_strcpy(doc, item, "label", c->label);
            yyjson_mut_obj_add_strcpy(doc, item, "kind", code_completion_kind_name(c->kind));
            yyjson_mut_obj_add_strcpy(doc, item, "detail", c->detail ? c->detail : "");
            yyjson_mut_obj_add_strcpy(doc, item, "insert", c->insert ? c->insert : c->label);
            yyjson_mut_obj_add_int(doc, item, "start", completions.start);
            yyjson_mut_obj_add_int(doc, item, "end", completions.end);
        }
        char *hover = code_intelligence_hover(source, offset);
        if (hover) {
            yyjson_mut_obj_add_strcpy(doc, result, "hover", hover);
        }
        const code_semantic_symbol *selected = code_intelligence_symbol_at(source, offset);
        if (selected && selected->asset_name) {
            yyjson_mut_val *found = yyjson_mut_obj_add_obj(doc, result, "definition");
            yyjson_mut_obj_add_strcpy(doc, found, "asset", selected->asset_name);
            yyjson_mut_obj_add_uint(doc, found, "graph_id", selected->graph_id);
            yyjson_mut_obj_add_uint(doc, found, "node_id", selected->node_id);
            yyjson_mut_obj_add_uint(doc, found, "pin_id", selected->pin_id);
        }
        asset_source_span definition;
        if ((!selected || !selected->asset_name) &&
            code_intelligence_definition(source, offset, &definition)) {
            yyjson_mut_val *found = yyjson_mut_obj_add_obj(doc, result, "definition");
            yyjson_mut_obj_add_int(doc, found, "start", definition.start);
            yyjson_mut_obj_add_int(doc, found, "end", definition.end);
        }
        asset_source_span *references = code_intelligence_references(source, offset);
        yyjson_mut_val *ranges = yyjson_mut_obj_add_arr(doc, result, "references");
        for (int i = 0; i < acount(references); i++) {
            yyjson_mut_val *range = yyjson_mut_arr_add_obj(doc, ranges);
            yyjson_mut_obj_add_int(doc, range, "start", references[i].start);
            yyjson_mut_obj_add_int(doc, range, "end", references[i].end);
        }
        afree(references);
        free(hover);
        asset_source_completions_free(&completions);
        yyjson_mut_val *calls = yyjson_mut_obj_add_arr(doc, result, "calls");
        for (int direction = 0; direction < 2; direction++) {
            code_semantic_call *found = code_intelligence_calls(source, offset, direction == 0);
            for (int i = 0; i < acount(found); i++) {
                yyjson_mut_val *call = yyjson_mut_arr_add_obj(doc, calls);
                yyjson_mut_obj_add_str(doc, call, "direction", direction == 0 ? "caller" : "callee");
                yyjson_mut_obj_add_uint(doc, call, "caller_id", found[i].caller_id);
                yyjson_mut_obj_add_uint(doc, call, "target_id", found[i].target_id);
                const code_semantic_symbol
                    *caller = code_intelligence_symbol_by_id(source, found[i].caller_id);
                const code_semantic_symbol
                    *target = code_intelligence_symbol_by_id(source, found[i].target_id);
                if (caller) {
                    yyjson_mut_obj_add_strcpy(doc, call, "caller", caller->name);
                }
                if (target) {
                    yyjson_mut_obj_add_strcpy(doc, call, "target", target->name);
                    if (target->asset_name) {
                        yyjson_mut_obj_add_strcpy(
                            doc,
                            call,
                            "target_asset",
                            target->asset_name
                        );
                        yyjson_mut_obj_add_uint(
                            doc,
                            call,
                            "target_graph_id",
                            target->graph_id
                        );
                    }
                }
                yyjson_mut_obj_add_int(doc, call, "start", found[i].start);
                yyjson_mut_obj_add_int(doc, call, "end", found[i].end);
            }
            afree(found);
        }
        yyjson_val *rename = yyjson_obj_get(request, "rename");
        if (rename) {
            char failure[256] = "";
            char *metadata = NULL;
            char *renamed = code_intelligence_rename_with_metadata(
                source,
                offset,
                yyjson_get_str(rename),
                &metadata,
                failure,
                sizeof failure
            );
            if (renamed) {
                code_source *candidate = code_source_parse_context(renamed, NULL, context);
                bool restored = code_source_metadata_restore(candidate, metadata);
                behavior_asset *model = restored
                                          ? code_source_build_behavior(candidate)
                                          : NULL;
                if (model) {
                    yyjson_mut_obj_add_strcpy(doc, result, "renamed_source", renamed);
                    yyjson_mut_obj_add_strcpy(doc, result, "renamed_metadata", metadata);
                } else {
                    snprintf(
                        failure,
                        sizeof failure,
                        "Renamed source failed frontend validation%s%s",
                        acount(candidate->diagnostics) ? ": " : "",
                        acount(candidate->diagnostics)
                            ? candidate->diagnostics[0].message
                            : ""
                    );
                }
                behavior_asset_free(model);
                code_source_free(candidate);
                free(renamed);
            }
            free(metadata);
            if (*failure) {
                yyjson_mut_obj_add_strcpy(doc, result, "rename_error", failure);
            }
        }
    }
    yyjson_val *location = yyjson_obj_get(request, "location");
    if (location) {
        yyjson_val *graph = yyjson_obj_get(location, "graph_id");
        yyjson_val *node = yyjson_obj_get(location, "node_id");
        yyjson_val *pin = yyjson_obj_get(location, "pin_id");
        if (!yyjson_is_obj(location) || !yyjson_is_uint(graph) ||
            !yyjson_is_uint(node) || (pin && !yyjson_is_uint(pin))) {
            behavior_asset_free(asset);
            code_source_free(source);
            return invalid(error, size, "location requires unsigned graph_id, node_id and optional pin_id");
        }
        int start, end;
        if (code_source_location(
                source,
                yyjson_get_uint(graph),
                yyjson_get_uint(node),
                yyjson_get_uint(pin),
                &start,
                &end
            )) {
            int line, column;
            code_source_position(source, start, &line, &column);
            yyjson_mut_val *found = yyjson_mut_obj_add_obj(doc, result, "location");
            yyjson_mut_obj_add_int(doc, found, "start", start);
            yyjson_mut_obj_add_int(doc, found, "end", end);
            yyjson_mut_obj_add_int(doc, found, "line", line);
            yyjson_mut_obj_add_int(doc, found, "column", column);
        }
    }
    behavior_asset_free(asset);
    code_source_free(source);
    return CHIBI_AUTOMATION_OK;
}
