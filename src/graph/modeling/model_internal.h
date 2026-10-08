#ifndef CHIBI_MODEL_INTERNAL_H
#define CHIBI_MODEL_INTERNAL_H
#include "graph/modeling/model_expand.h"
#include "runtime/models/model_bounds.h"
#include "runtime/models/model_program_internal.h"

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
bool model_build_hit(model_program *program, const graph *, shader_error *error);
bool model_build_part_shaders(model_program *, const graph *, shader_error *);
model_bounds_plan *model_prepare_bounds(const graph *, const model_numeric_input *, int, shader_error *);
void model_material_slot_free(model_material_slot *slot);
bool model_profile_operation(int op);
bool model_constructive_validate(const node *n, shader_error *error);
int model_lower_extrusion(compiler *c, const node *n, int p);
int model_lower_constructive(compiler *c, const node *n, int p);
int model_lower(compiler *c, const node *n, int p);
bool model_profile_validate(const node *n, shader_error *error);
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
