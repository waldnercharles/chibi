#ifndef CHIBI_MODEL_PROGRAM_INTERNAL_H
#define CHIBI_MODEL_PROGRAM_INTERNAL_H
#include "runtime/models/model_program.h"
#include "support/array.h"
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
    MODEL_TUBE_SWEEP,
    MODEL_SAMPLED_SDF
} model_op;
typedef struct
{
    uint64_t node;
    int pin, expression;
} model_numeric_input;
typedef struct
{
    uint64_t node;
    int expression, width;
    float minimum[4], maximum[4];
    char *asset;
} model_numeric_constraint;
typedef struct
{
    shader_graph graph;
    model_parameter *parameters;
    model_numeric_constraint *constraints;
} model_numeric;
typedef struct
{
    uint64_t generated, authored;
    char *asset, *path;
} model_source;
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
    struct model_bounds_plan *plan;
    shader_graph *part_distance, *part_hit;
    model_numeric numeric;
    model_numeric_input *inputs;
    model_source *sources;
    model_rotation_uniform *rotations;
    shader_graph proof, hit_proof;
    shader_graph *part_frames;
    model_hit_node *hit_nodes;
    int root_position, hit_numeric_count;
};

void model_parameter_free(model_parameter *);
void model_numeric_init(model_numeric *);
void model_numeric_free(model_numeric *);
void model_material_slot_free(model_material_slot *);
void model_bounds_program_free(struct model_bounds_program *);
bool model_fail(shader_error *, uint64_t, const char *);
void model_implemented_rotation(const float[3], double[3][3], double[3][3], float *);
bool model_profile_operation(int);
bool model_snapshot_bounds(
    const struct model_bounds_program *,
    const float (*)[4],
    model_bounds *,
    shader_error *
);
bool model_finite_program(
    const shader_graph *,
    const model_bounds *,
    const float[32][4],
    const float (*)[4],
    int,
    shader_error *
);
#endif
