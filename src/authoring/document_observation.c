#include "authoring/document_observation.h"

#include "language/function_identity.h"

#include <stdio.h>
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
bool document_observation_query(
    const document_semantics *snapshot,
    uint64_t graph,
    uint64_t node,
    uint64_t pin,
    bool output,
    document_observation *out,
    char *error,
    size_t size
)
{
    *out = (document_observation){ 0 };
    const authoring_source_presentation *presentation = document_semantics_presentation(snapshot);
    if (!presentation) {
        return fail(error, size, "Captured source keys require an authored source graph");
    }
    const source_presentation_origin
        *origin = authoring_source_presentation_origin(presentation, graph, node, pin);
    if (!origin || !origin->pin || origin->flow || origin->output != output) {
        return fail(error, size, "Data port identity does not belong to this source snapshot");
    }
    const script_analysis *analysis = document_semantics_analysis(snapshot);
    int ordinal = output ? origin->result_index : origin->parameter;
    if (ordinal < 0) {
        ordinal = origin->port_index;
    }
    *out = (document_observation){
        .module = script_catalog_id("module", authoring_source_presentation_identity(presentation)),
        .function = origin->function >= 0
                      ? script_function_identity(analysis, origin->function)
                      : 0,
        .occurrence = origin->node_occurrence,
        .index = ordinal,
        .output = output,
        .fallback_occurrence = output ? 0 : origin->occurrence,
        .fallback_index = 0,
        .fallback_output = true,
        .has_fallback = !output && origin->occurrence != 0
    };
    if (size) {
        *error = 0;
    }
    return true;
}

bool document_execution_query(
    const document_semantics *snapshot,
    uint64_t graph,
    uint64_t node,
    document_execution *out,
    char *error,
    size_t size
)
{
    *out = (document_execution){ 0 };
    const authoring_source_presentation *presentation = document_semantics_presentation(snapshot);
    if (!presentation) {
        return fail(error, size, "Execution keys require an authored source graph");
    }
    const source_presentation_origin
        *origin = authoring_source_presentation_origin(presentation, graph, node, 0);
    const script_analysis *analysis = document_semantics_analysis(snapshot);
    if (!origin || !origin->node_occurrence || origin->function < 0 ||
        origin->function >= analysis->function_count) {
        return fail(error, size, "Node has no canonical source execution identity");
    }
    *out = (document_execution){
        .module = script_catalog_id("module", authoring_source_presentation_identity(presentation)),
        .function = script_function_identity(analysis, origin->function),
        .occurrence = origin->node_occurrence
    };
    if (size) {
        *error = 0;
    }
    return true;
}
