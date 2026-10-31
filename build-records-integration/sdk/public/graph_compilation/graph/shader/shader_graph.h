#ifndef CHIBI_SHADER_GRAPH_H
#define CHIBI_SHADER_GRAPH_H
#include "graph/model/graph.h"
#include "graph/model/registry.h"
#include "runtime/shaders/program.h"

/* result must be initialized; failure preserves its last valid program. */
bool shader_graph_from_graph(const graph *source, shader_graph *result, shader_error *error);
/* The Alpha Mode of a graph that shader_graph_from_graph lowers; Blend for a
 * shape. The mode decides how a renderer draws the material; its effect on
 * Opacity is already in the lowered outputs. */
shader_alpha_mode shader_graph_alpha_mode(const graph *source);
/* Validates a pure shader function without requiring a material or shape output. */
bool shader_graph_validate_function(const graph *source, shader_error *error);
/* Preserves signature argument slots, defaults and source IDs. Initialized
 * result remains unchanged on failure, as with shader_graph_from_graph. */
bool shader_graph_from_function(const graph *source, shader_graph *result, shader_error *error);
/* Available input fallback, independent of wiring or an explicit default value.
 * Returns a shader_op, or -1 when the input has no sample context fallback. */
int shader_node_input_context(const node *n, int input, const char **label);
extern const registry_pack SHADER_PACK;
node *shader_graph_add_output(graph *g, bool shape);
/* Initializes editable defaults and assigns new Shader Parameters an unused slot. */
bool shader_graph_init_node(graph *g, node *n);
/* Stops are graph-owned typed values; edits use the ordinary graph snapshot. */
int shader_node_ramp_get(const node *n, shader_ramp_stop out[SHADER_RAMP_MAX]);
bool shader_node_ramp_set(graph *g, node *n, const shader_ramp_stop *stops, int count);
#endif
