#ifndef CHIBI_MODEL_INTERNAL_H
#define CHIBI_MODEL_INTERNAL_H
#include "graph/modeling/model_expand.h"
#define MODEL_DEPTH_MAX 128
#define MODEL_BOUND_WORK_MAX (SHADER_NODE_MAX * 8)

typedef enum
{
    MODEL_SPHERE,
    MODEL_BOX,
    MODEL_ROUNDED_BOX,
    MODEL_CYLINDER,
    MODEL_CAPSULE,
    MODEL_TORUS,
    MODEL_TRANSLATE,
    MODEL_ROTATE,
    MODEL_SCALE,
    MODEL_REFLECT,
    MODEL_UNION,
    MODEL_INTERSECTION,
    MODEL_DIFFERENCE,
    MODEL_SMOOTH_UNION,
    MODEL_OUTPUT,
    MODEL_PART,
    MODEL_PROFILE_CIRCLE,
    MODEL_PROFILE_RECTANGLE,
    MODEL_PROFILE_POLYGON,
    MODEL_EXTRUDE,
    MODEL_REVOLVE,
    MODEL_PROFILE_OFFSET,
    MODEL_PROFILE_UNION,
    MODEL_PROFILE_INTERSECTION,
    MODEL_PROFILE_DIFFERENCE,
    MODEL_ROUNDED_EXTRUDE,
    MODEL_BEVELED_EXTRUDE,
    MODEL_OFFSET,
    MODEL_SHELL,
    MODEL_TUBE_SWEEP
} model_op;
typedef struct
{
    double min[3], max[3];
    bool empty;
} bound;
typedef struct
{
    const graph *source;
    const node *stack[MODEL_DEPTH_MAX];
    int depth;
    shader_graph *shader;
    model_program *program;
    shader_error *error;
    bool failed;
    int bound_work;
    struct
    {
        const node *source;
        double threshold;
        bound value;
    } cache[SHADER_NODE_MAX];
    int cache_count;
} compiler;

typedef struct
{
    uint64_t node;
    int expression, slot;
} model_rotation_uniform;
typedef struct
{
    uint64_t node;
    int distance, position;
} model_hit_node;
struct model_bounds_program
{
    behavior_asset *asset;
    model_numeric numeric;
    model_numeric_input *inputs;
    model_source *sources;
    model_rotation_uniform *rotations;
    shader_graph proof, hit_proof;
    shader_graph *part_frames;
    model_hit_node *hit_nodes;
    int root_position, hit_numeric_count;
};
bool model_build_frames(model_program *program, shader_error *error);
int model_operation(const node *n);
bool model_fail(shader_error *error, uint64_t id, const char *message);
float model_number(const node *n, int pin);
void model_vector(const node *n, int pin, float out[3]);
bool model_validate_node(const graph *g, const node *n, shader_error *error);
const node *model_child(const graph *g, const node *n, int pin);
void model_implemented_rotation(
    const float angles[3],
    double inverse[3][3],
    double forward[3][3],
    float *factor
);
bool model_enter(compiler *c, const node *n);
bool model_bounds_visit(compiler *c, const node *n, double threshold, bound *out);
bool model_finite_program(
    const shader_graph *g,
    const model_bounds *bounds,
    const float parameters[32][4],
    const float (*numeric)[4],
    int numeric_count,
    shader_error *error
);
bool model_construction_bounds(const graph *source, model_bounds *out, shader_error *error);
bool model_snapshot_bounds(
    const struct model_bounds_program *p,
    const float (*values)[4],
    model_bounds *out,
    shader_error *error
);
bool model_build_hit(model_program *program, shader_error *error);
void model_material_slot_free(model_material_slot *slot);
bool model_profile_operation(int op);
bool model_constructive_validate(const node *n, shader_error *error);
bool model_constructive_bounds(compiler *c, const node *n, double threshold, bound *out);
int model_lower_extrusion(compiler *c, const node *n, int p);
int model_lower_constructive(compiler *c, const node *n, int p);
int model_lower(compiler *c, const node *n, int p);
bool model_profile_validate(const node *n, shader_error *error);
bool model_profile_bounds(compiler *c, const node *n, double threshold, bound *out);
int model_lower_profile(compiler *c, const node *n, int p, bool revolve);
int model_emit(compiler *c, const node *n, shader_op op, int width, int a, int b, int d);
int model_constant(compiler *c, const node *n, int width, const float *values);
int model_scalar(compiler *c, const node *n, float value);
int model_unary(compiler *c, const node *n, shader_op op, int width, int a);
int model_binary(compiler *c, const node *n, shader_op op, int width, int a, int b);
int model_coordinate(compiler *c, const node *n, int p, int axis);
int model_field(compiler *c, const node *n, int pin);
void model_bounds_program_free(struct model_bounds_program *program);
#endif
