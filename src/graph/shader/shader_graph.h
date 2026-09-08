#ifndef CBLUEPRINT_SHADER_GRAPH_H
#define CBLUEPRINT_SHADER_GRAPH_H
#include "graph/model/graph.h"
#include "graph/model/registry.h"

#include <stdbool.h>
#define SHADER_PARAMETER_MAX 32
#define SHADER_NODE_MAX 1024
#define SHADER_OUTPUT_MAX 8

typedef enum
{
    SHADER_CONSTANT,
    SHADER_PARAMETER,
    SHADER_POSITION,
    SHADER_TIME,
    SHADER_ADD,
    SHADER_SUBTRACT,
    SHADER_MULTIPLY,
    SHADER_DIVIDE,
    SHADER_MIN,
    SHADER_MAX,
    SHADER_ABS,
    SHADER_SIN,
    SHADER_COS,
    SHADER_FRACT,
    SHADER_LENGTH,
    SHADER_DOT,
    SHADER_MIX,
    SHADER_SELECT,
    SHADER_CIRCLE,
    SHADER_BOX,
    SHADER_UNION,
    SHADER_INTERSECTION,
    SHADER_DIFFERENCE,
    SHADER_SMOOTH_UNION,
    SHADER_VECTOR2,
    SHADER_VECTOR4,
    SHADER_X,
    SHADER_Y,
    SHADER_Z,
    SHADER_W,
    SHADER_WORLD_POSITION,
    SHADER_OP_COUNT
} shader_op;
typedef struct
{
    shader_op op;
    int width;
    int input[4];
    float value[4];
    int parameter;
    uint64_t source_id;
} shader_node;
/* Compiled, topologically ordered GPU expression IR; authored graphs use the ordinary graph model. */
typedef struct
{
    shader_node *nodes;
    int output[SHADER_OUTPUT_MAX];
    int output_count;
} shader_graph;
typedef struct
{
    uint64_t node;
    char message[128];
} shader_error;
void shader_graph_init(shader_graph *g);
void shader_graph_free(shader_graph *g);
int shader_graph_add(shader_graph *g, shader_op op, int width);
const char *shader_op_name(shader_op op);
int shader_op_inputs(shader_op op);
bool shader_graph_validate(const shader_graph *g, shader_error *error);
bool shader_graph_eval_outputs(
    const shader_graph *g,
    const float position[2],
    const float world_position[2],
    float time,
    const float (*parameters)[4],
    float result[SHADER_OUTPUT_MAX][4],
    shader_error *error
);
/* malloc-owned portable GLSL; caller supplies all 32 parameter values including defaults.
 * void PREFIX(vec2 sg_position,vec2 sg_world_position,float sg_time,vec4 sg_params[32],out vec4 sg_out[8]) */
char *shader_graph_glsl_named(const shader_graph *g, const char *prefix, shader_error *error);
/* result must be initialized; failure preserves its last valid program. */
bool shader_graph_from_graph(const graph *source, shader_graph *result, shader_error *error);
extern const registry_pack SHADER_PACK;
node *shader_graph_add_output(graph *g, bool shape);
/* Assigns an unused asset-wide slot to a newly spawned Shader Parameter. */
bool shader_graph_init_parameter(graph *g, node *n);
#endif
