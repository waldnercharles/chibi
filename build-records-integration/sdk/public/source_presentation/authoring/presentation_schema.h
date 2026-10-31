#ifndef CHIBI_AUTHORING_PRESENTATION_SCHEMA_H
#define CHIBI_AUTHORING_PRESENTATION_SCHEMA_H
#include "capabilities/catalog.h"
#include "graph/model/graph.h"
/* Names and descriptor/pin arrays are copied into the graph. Managed default
 * payloads remain borrowed; their owner must outlive the descriptor. */
pin_spec authoring_presentation_pin(graph *, const script_catalog *, const char *, script_type);
node_desc *authoring_presentation_descriptor(
    graph *,
    const char *,
    node_display_class,
    const char *category,
    const pin_spec *inputs,
    int input_count,
    const pin_spec *outputs,
    int output_count
);
#endif
