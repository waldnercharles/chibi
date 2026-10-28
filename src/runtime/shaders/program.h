#ifndef CHIBI_RUNTIME_SHADER_PROGRAM_H
#define CHIBI_RUNTIME_SHADER_PROGRAM_H
#include <stdbool.h>
#include <stdint.h>
#define SHADER_PARAMETER_MAX 32
#define SHADER_NODE_MAX 1024
#define SHADER_OUTPUT_MAX 9

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
    /* Appended so prepared operations keep their values. */
    SHADER_STAR_FIELD,
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
/* Which way a normal image's green tilts. Images put (0, 0) at their top-left,
 * so DirectX's +green tilts toward +v, down the image, and OpenGL's toward -v,
 * up the image. Materials save the value. */
typedef enum
{
    SHADER_NORMAL_DIRECTX,
    SHADER_NORMAL_OPENGL,
    /* OpenGL image channels in a unit, orthogonal tangent frame, as glTF
     * requires. UV density does not change the normal's direction. */
    SHADER_NORMAL_OPENGL_TANGENT
} shader_normal_convention;
enum
{
    SHADER_IMAGE_INPUT_UV,
    SHADER_IMAGE_INPUT_IMAGE,
    SHADER_IMAGE_INPUT_COLOR_SPACE,
    SHADER_IMAGE_INPUT_FILTER,
    SHADER_IMAGE_INPUT_WRAP,
    SHADER_IMAGE_INPUT_COUNT
};
/* Surface Output inputs. The first SHADER_OUTPUT_MAX are a material's IR
 * outputs; lowering folds the alpha settings into Opacity. */
enum
{
    SHADER_SURFACE_BASE_COLOR,
    SHADER_SURFACE_EMISSION,
    SHADER_SURFACE_ROUGHNESS,
    SHADER_SURFACE_METALLIC,
    SHADER_SURFACE_NORMAL,
    SHADER_SURFACE_OPACITY,
    SHADER_SURFACE_UNLIT,
    SHADER_SURFACE_HEIGHT,
    SHADER_SURFACE_OCCLUSION,
    SHADER_SURFACE_ALPHA_MODE,
    SHADER_SURFACE_ALPHA_CUTOFF,
    SHADER_SURFACE_INPUT_COUNT
};
_Static_assert(SHADER_SURFACE_ALPHA_MODE == SHADER_OUTPUT_MAX, "surface outputs");
/* Blend mixes by Opacity, Opaque ignores it, and Mask keeps whole the pixels
 * whose Opacity reaches the Alpha Cutoff and drops the rest. */
typedef enum
{
    SHADER_ALPHA_BLEND,
    SHADER_ALPHA_OPAQUE,
    SHADER_ALPHA_MASK
} shader_alpha_mode;
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
    /* Optional caller-owned derivative workspace in float[4] values. Each
     * derivative evaluation consumes acount(graph->nodes) entries and lends
     * the remainder to children. A validated graph permits at most two levels
     * (Bump sampling Triplanar Texture). Smaller capacity uses the allocating
     * fallback. Borrowed for this call; never aliases primary values. */
    float (*derivative_scratch)[4];
    int derivative_capacity;
    float time;
    shader_image_resolver resolve_image; /* Returns resident pixels; does no file IO. */
    void *image_userdata;
    shader_influence_resolver resolve_influence; /* Optional scene activity; absent means zero. */
    void *influence_userdata;
} shader_sample_context;
bool shader_ramp_valid(const shader_ramp_stop *stops, int count);
void shader_graph_init(shader_graph *g);
void shader_graph_free(shader_graph *g);
/* Validated owning copy; initialized destination stays unchanged on failure. */
bool shader_graph_clone(const shader_graph *source, shader_graph *result, shader_error *);
int shader_graph_add(shader_graph *g, shader_op op, int width);
/* Copies the key and reuses an existing identical image/settings entry. */
int shader_graph_add_image(shader_graph *g, const shader_image_source *source);
const char *shader_op_name(shader_op op);
int shader_op_inputs(shader_op op);
bool shader_graph_validate(const shader_graph *g, shader_error *error);
/* Particle functions return Position:vec3, Size:float, Color:vec4 and use only
 * numeric operations plus time. Slots 0..2 are Index, Count and Seed. */
bool shader_graph_particle_validate(const shader_graph *, shader_error *);
bool shader_particle_parameters_validate(
    char *const names[SHADER_PARAMETER_MAX],
    const int widths[SHADER_PARAMETER_MAX],
    shader_error *
);
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
 * void PREFIX(SGSample sg_sample,vec4 sg_params[32],out vec4 sg_out[SHADER_OUTPUT_MAX])
 * Caller defines vec4 PREFIX_image_N(vec2 uv) for every entry in g->images. */
char *shader_graph_glsl_named(const shader_graph *g, const char *prefix, shader_error *error);
/* malloc-owned portable GLSL for output[0]'s object-space gradient. The emitted
 * function accepts a nonnegative object-space singularity tolerance as its
 * third argument; a two-argument overload uses exact singularity checks.
 * Unsupported reachable operations return NULL without an error;
 * singular/non-differentiable samples return NaN for finite-difference fallback. */
char *shader_graph_glsl_gradient_named(const shader_graph *g, const char *prefix, shader_error *error);
/* Generates graph GLSL that reads ramp stops through sg_ramp(i), which the
 * containing program defines. The offset is the first vec4 reserved for this
 * graph in that program's ramp table. */
char *shader_graph_glsl_named_ramps(
    const shader_graph *g,
    const char *prefix,
    int ramp_offset,
    shader_error *error
);
int shader_graph_ramp_vec4_count(const shader_graph *g);
void shader_graph_ramp_payload(const shader_graph *g, float (*payload)[4]);

#endif
