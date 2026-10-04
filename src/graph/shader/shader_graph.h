#ifndef CHIBI_SHADER_GRAPH_H
#define CHIBI_SHADER_GRAPH_H
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
    SHADER_VECTOR3,
    SHADER_NORMALIZE,
    SHADER_CLAMP,
    SHADER_SATURATE,
    SHADER_STEP,
    SHADER_SMOOTHSTEP,
    SHADER_NOISE,
    SHADER_UV,
    SHADER_OBJECT_POSITION,
    SHADER_NORMAL,
    SHADER_VIEW_DIRECTION,
    SHADER_MAPPING,
    SHADER_MAPPING3D,
    SHADER_FRESNEL,
    SHADER_REMAP,
    SHADER_GRADIENT,
    SHADER_FRACTAL_NOISE,
    SHADER_VORONOI,
    SHADER_VORONOI_COLOR,
    SHADER_COLOR_RAMP,
    SHADER_COLOR_RAMP_ALPHA,
    SHADER_IMAGE_TEXTURE,
    SHADER_IMAGE_TEXTURE_ALPHA,
    SHADER_SURFACE_INFLUENCE,
    SHADER_BEAT_PHASE,
    SHADER_BEAT_PULSE,
    SHADER_FLOOR,
    SHADER_SQRT,
    SHADER_SIGN,
    SHADER_PART_POSITION,
    SHADER_PLANAR_MAPPING,
    SHADER_CYLINDRICAL_MAPPING,
    SHADER_NORMAL_MAP,
    SHADER_BUMP,
    SHADER_TRIPLANAR_TEXTURE,
    SHADER_TRIPLANAR_TEXTURE_ALPHA,
    SHADER_DECAL_MASK,
    SHADER_SAMPLED_SDF,
    SHADER_OP_COUNT
} shader_op;
enum
{
    SHADER_RAMP_MAX = 16
};
typedef struct
{
    float position;
    float color[4];
} shader_ramp_stop;
typedef enum
{
    SHADER_IMAGE_SRGB,
    SHADER_IMAGE_LINEAR
} shader_image_color_space;
typedef enum
{
    SHADER_IMAGE_NEAREST,
    SHADER_IMAGE_BILINEAR
} shader_image_filter;
typedef enum
{
    SHADER_IMAGE_REPEAT,
    SHADER_IMAGE_CLAMP
} shader_image_wrap;
enum
{
    SHADER_IMAGE_INPUT_UV,
    SHADER_IMAGE_INPUT_IMAGE,
    SHADER_IMAGE_INPUT_COLOR_SPACE,
    SHADER_IMAGE_INPUT_FILTER,
    SHADER_IMAGE_INPUT_WRAP,
    SHADER_IMAGE_INPUT_COUNT
};
typedef struct
{
    char *key; /* Owned asset key, never a GPU handle or embedded image bytes. */
    shader_image_color_space color_space;
    shader_image_filter filter;
    shader_image_wrap wrap;
} shader_image_source;
typedef struct
{
    const unsigned char *pixels; /* Borrowed straight-alpha RGBA8, rows top to bottom. */
    int width, height;
} shader_image_view;
typedef bool (*shader_image_resolver)(
    void *userdata,
    const shader_image_source *source,
    shader_image_view *view
);
typedef void (*shader_influence_resolver)(void *userdata, const float position[3], float color[3]);
typedef struct
{
    shader_op op;
    int width;
    int input[4];
    float value[4];
    int parameter;
    uint64_t source_id;
    shader_ramp_stop *ramp; /* Owned stop array for Color Ramp operations. */
    float volume_range,
        volume_lipschitz; /* Sampled-SDF metadata; value.xyz is half size, parameter is resolution. */
    int image; /* Index into the graph's image sources. */
} shader_node;
/* Compiled, topologically ordered GPU expression IR; authored graphs use the ordinary graph model. */
typedef struct
{
    shader_node *nodes;
    shader_image_source *images;
    int output[SHADER_OUTPUT_MAX];
    int output_count;
} shader_graph;
typedef struct
{
    uint64_t node;
    char message[128];
} shader_error;
typedef struct
{
    float position[2]; /* Local 2D shape coordinates, in authored units. */
    float uv[2];
    float object_position[3];
    float part_position[3];
    /* Coordinate gradients in world axes; zero rows mean unavailable. */
    float object_from_world[3][3], part_from_world[3][3], uv_from_world[2][3];
    float world_position[3];
    float normal[3];
    float view_direction[3];
    float time;
    float beat_phase, beat_pulse;
    shader_image_resolver resolve_image; /* Returns resident pixels; does no file IO. */
    void *image_userdata;
    shader_influence_resolver resolve_influence; /* Optional scene activity; absent means zero. */
    void *influence_userdata;
} shader_sample_context;
void shader_graph_init(shader_graph *g);
void shader_graph_free(shader_graph *g);
int shader_graph_add(shader_graph *g, shader_op op, int width);
/* Copies the key and reuses an existing identical image/settings entry. */
int shader_graph_add_image(shader_graph *g, const shader_image_source *source);
const char *shader_op_name(shader_op op);
int shader_op_inputs(shader_op op);
bool shader_graph_validate(const shader_graph *g, shader_error *error);
/* Evaluates once into caller-owned storage for acount(g->nodes) values.
 * Bounds programs consume numeric intermediate values without resampling geometry. */
bool shader_graph_eval_values(
    const shader_graph *g,
    const shader_sample_context *sample,
    const float (*parameters)[4],
    float (*values)[4],
    shader_error *error
);
/* Offline samplers and derivative contexts validate the graph once, then reuse
 * caller-owned acount(g->nodes) scratch values. No allocation or validation. */
bool shader_graph_eval_values_validated(
    const shader_graph *,
    const shader_sample_context *,
    const float (*parameters)[4],
    float (*values)[4],
    shader_error *
);
/* Evaluates only the root's ancestors, never unrelated/downstream nodes.
 * Requires a validated graph and a valid root index. */
bool shader_graph_eval_node_scratch(
    const shader_graph *,
    const shader_sample_context *,
    const float (*parameters)[4],
    int root,
    float (*values)[4],
    float result[4],
    shader_error *
);
bool shader_graph_eval_outputs(
    const shader_graph *g,
    const shader_sample_context *sample,
    const float (*parameters)[4],
    float result[SHADER_OUTPUT_MAX][4],
    shader_error *error
);
/* malloc-owned portable GLSL; caller supplies all 32 parameter values including defaults.
 * void PREFIX(SGSample sg_sample,vec4 sg_params[32],out vec4 sg_out[8])
 * Caller defines vec4 PREFIX_image_N(vec2 uv) for every entry in g->images. */
char *shader_graph_glsl_named(const shader_graph *g, const char *prefix, shader_error *error);
/* malloc-owned vec3 PREFIX(SGSample,vec4[32]) for output[0]'s object-space
 * gradient. Unsupported reachable operations return NULL without an error;
 * singular/non-differentiable samples return NaN for finite-difference fallback. */
char *shader_graph_glsl_gradient_named(const shader_graph *g, const char *prefix, shader_error *error);
/* Generates graph GLSL that reads ramp stops from sg_ramp_data. The offset is
 * the first vec4 reserved for this graph in the containing program. */
char *shader_graph_glsl_named_ramps(
    const shader_graph *g,
    const char *prefix,
    int ramp_offset,
    shader_error *error
);
int shader_graph_ramp_vec4_count(const shader_graph *g);
void shader_graph_ramp_payload(const shader_graph *g, float (*payload)[4]);
/* result must be initialized; failure preserves its last valid program. */
bool shader_graph_from_graph(const graph *source, shader_graph *result, shader_error *error);
/* Validates a pure shader function without requiring a material or shape output. */
bool shader_graph_validate_function(const graph *source, shader_error *error);
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
bool shader_ramp_valid(const shader_ramp_stop *stops, int count);
#endif
