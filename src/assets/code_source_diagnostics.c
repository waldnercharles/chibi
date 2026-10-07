#include "assets/code_source_diagnostics.h"

#include "assets/code_source_intelligence.h"
#include "graph/document/library.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool import_range(
    const code_source *source,
    const char *asset,
    uint64_t graph_id,
    int *start,
    int *end
)
{
    int count;
    const code_semantic_symbol *symbols = code_intelligence_symbols(source, &count);
    for (int match = 0; match < 3; match++) {
        for (int i = 0; i < count; i++) {
            const code_semantic_symbol *symbol = &symbols[i];
            if (!symbol->asset_name) {
                continue;
            }
            bool same_asset = asset && !strcmp(asset, symbol->asset_name);
            if ((match == 0 && (!same_asset || symbol->graph_id != graph_id)) ||
                (match == 1 && !same_asset)) {
                continue;
            }
            asset_source_span
                *ranges = code_intelligence_references_for(source, symbol->id);
            bool found = acount(ranges) > 0;
            if (found) {
                *start = ranges[0].start;
                *end = ranges[0].end;
            }
            afree(ranges);
            if (found) {
                return true;
            }
        }
    }
    return false;
}
static const graph *authored_graph(const code_source *source, graph_node_source origin)
{
    if (source->asset && !strcmp(source->asset->name, origin.asset)) {
        return behavior_asset_find_graph(source->asset, origin.graph_id);
    }
    for (int i = 0; i < source->context.library_count; i++) {
        const behavior_asset *library = source->context.libraries[i];
        if (!strcmp(library->name, origin.asset)) {
            return behavior_asset_find_graph(library, origin.graph_id);
        }
    }
    return NULL;
}
static bool same_origin(const char *a, const char *b)
{
    return a && b ? !strcmp(a, b) : a == b;
}
static bool already_mapped(const code_source *source, const code_source_diagnostic *item)
{
    for (int i = 0; i < acount(source->diagnostics); i++) {
        const code_source_diagnostic *previous = &source->diagnostics[i];
        if (previous->severity == item->severity &&
            previous->rule == item->rule && previous->graph_id == item->graph_id &&
            previous->node_id == item->node_id && previous->pin_id == item->pin_id &&
            previous->start == item->start && previous->end == item->end &&
            same_origin(previous->asset_name, item->asset_name) &&
            same_origin(previous->graph_name, item->graph_name) &&
            !strcmp(previous->message, item->message)) {
            return true;
        }
    }
    return false;
}
void code_source_compile_diagnostics(
    code_source *source,
    const behavior_asset *linked,
    const compile_report *report
)
{
    for (int i = 0; i < acount(report->items); i++) {
        const compile_diagnostic *item = &report->items[i];
        code_source_diagnostic diagnostic = { .severity = item->severity,
                                              .rule = item->rule,
                                              .graph_id = item->graph_id,
                                              .node_id = item->node_id,
                                              .pin_id = item->pin_id };
        const graph *g = linked ? behavior_asset_find_graph(linked, item->graph_id)
                                : NULL;
        graph_node_source origin = g ? graph_node_authored(g, item->node_id)
                                     : (graph_node_source){ 0 };
        if (origin.asset) {
            diagnostic.graph_id = origin.graph_id;
            diagnostic.node_id = item->node_id ? origin.node_id : 0;
            uint64_t offset = g->graph_id - g->source_graph_id;
            diagnostic.pin_id = item->pin_id ? item->pin_id - offset : 0;
        }
        bool foreign = origin.asset &&
                       (!source->asset || strcmp(origin.asset, source->asset->name));
        bool located = false;
        if (foreign) {
            diagnostic.asset_name = xstrdup(origin.asset);
            const graph *authored = authored_graph(source, origin);
            diagnostic.graph_name = xstrdup(authored ? authored->name : g->name);
            located = import_range(
                source,
                origin.asset,
                origin.graph_id,
                &diagnostic.start,
                &diagnostic.end
            );
            snprintf(
                diagnostic.message,
                sizeof diagnostic.message,
                "%s.%s: %s",
                diagnostic.asset_name,
                diagnostic.graph_name,
                item->message
            );
        } else {
            if (!located) {
                located = code_source_location(
                    source,
                    diagnostic.graph_id,
                    diagnostic.node_id,
                    diagnostic.pin_id,
                    &diagnostic.start,
                    &diagnostic.end
                );
            }
            if (!located) {
                located = import_range(
                    source,
                    NULL,
                    0,
                    &diagnostic.start,
                    &diagnostic.end
                );
            }
            snprintf(diagnostic.message, sizeof diagnostic.message, "%s", item->message);
        }
        if (!located) {
            diagnostic.start = 0;
            diagnostic.end = (int)strlen(source->text);
        }
        code_source_position(
            source,
            diagnostic.start,
            &diagnostic.line,
            &diagnostic.column
        );
        if (already_mapped(source, &diagnostic)) {
            free(diagnostic.asset_name);
            free(diagnostic.graph_name);
            continue;
        }
        apush(source->diagnostics, diagnostic);
    }
}
