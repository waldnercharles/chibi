#include "graph/compiler/validation.h"

#include "graph/compiler/field_program.h"
#include "graph/compiler/structure_internal.h"
#include "graph/document/serialize.h"
#include "graph/modeling/model_compile.h"
#include "graph/nodes/component_nodes.h"
#include "graph/shader/shader_graph.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>

static void error(compile_report *report, uint64_t graph, uint64_t node, int rule, const char *message)
{
    compile_diagnostic diagnostic = {
        .severity = CMPL_ERROR,
        .rule = rule,
        .graph_id = graph,
        .node_id = node,
    };
    snprintf(diagnostic.message, sizeof diagnostic.message, "%s", message);
    apush(report->items, diagnostic);
    report->error_count++;
}
int graph_asset_compile_with_models(
    const behavior_asset *asset,
    const behavior_asset *const *models,
    int count,
    compile_report *out
)
{
    assert(asset && out);
    compile_report_free(out);
    compile_report_init(out);
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *g = asset->graphs[i];
        for (int j = 0; j < acount(g->nodes); j++) {
            if (g->nodes[j]->desc->metadata_only) {
                error(out, g->graph_id, g->nodes[j]->id, CMPL_RULE_MISSING_REFERENCE, "Source declaration metadata cannot execute as a graph node");
            }
        }
    }
    if (out->error_count) {
        return out->error_count;
    }
    if (asset->domain != BEHAVIOR_DOMAIN_MODEL &&
        (asset->domain == BEHAVIOR_DOMAIN_FIELD ||
         asset->domain == BEHAVIOR_DOMAIN_MATERIAL || asset->domain == BEHAVIOR_DOMAIN_SHAPE ||
         asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) &&
        behavior_asset_has_component_nodes(asset)) {
        behavior_asset *lowered = behavior_asset_clone(asset);
        char message[256] = "";
        if (!lowered ||
            behavior_asset_lower_component_nodes(lowered, message, sizeof message)) {
            error(
                out,
                asset->graph->graph_id,
                0,
                CMPL_RULE_MISSING_REFERENCE,
                lowered ? message : graph_last_error()
            );
            behavior_asset_free(lowered);
            return out->error_count;
        }
        int errors = graph_asset_compile_with_models(lowered, models, count, out);
        behavior_asset_free(lowered);
        return errors;
    }
    shader_error diagnostic = { 0 };
    switch (asset->domain) {
        case BEHAVIOR_DOMAIN_MODEL: {
            model_program program;
            model_program_init(&program);
            bool valid = model_compile_with_catalog(asset, models, count, &program, &diagnostic);
            model_program_free(&program);
            if (!valid) {
                error(
                    out,
                    asset->graph->graph_id,
                    diagnostic.node,
                    CMPL_RULE_OPERATOR,
                    diagnostic.message
                );
            }
            break;
        }
        case BEHAVIOR_DOMAIN_FIELD: {
            char message[256];
            field_program *program = field_program_compile(asset, message, sizeof message);
            if (!program) {
                error(out, asset->graph->graph_id, 0, CMPL_RULE_BACKEND, message);
            }
            field_program_free(program);
            break;
        }
        case BEHAVIOR_DOMAIN_MATERIAL:
        case BEHAVIOR_DOMAIN_SHAPE: {
            shader_graph shader;
            shader_graph_init(&shader);
            bool valid = shader_graph_from_graph(asset->graph, &shader, &diagnostic);
            shader_graph_free(&shader);
            if (!valid) {
                error(
                    out,
                    asset->graph->graph_id,
                    diagnostic.node,
                    CMPL_RULE_OPERATOR,
                    diagnostic.message
                );
            }
            break;
        }
        case BEHAVIOR_DOMAIN_EVENT_DEFINITION:
        case BEHAVIOR_DOMAIN_SHADER_LIBRARY:
            return graph_structure_compile(asset, out);
        default:
            error(out, asset->graph->graph_id, 0, CMPL_RULE_BACKEND, "Executable behaviors, systems and libraries require canonical source preparation.");
            break;
    }
    return out->error_count;
}

int graph_asset_compile(const behavior_asset *asset, compile_report *out)
{
    return graph_asset_compile_with_models(asset, NULL, 0, out);
}
