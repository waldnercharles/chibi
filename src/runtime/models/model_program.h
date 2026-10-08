#ifndef CHIBI_MODEL_PROGRAM_H
#define CHIBI_MODEL_PROGRAM_H

#include "runtime/shaders/program.h"
#include "types/value.h"

typedef struct
{
    float min[3], max[3];
    bool empty;
} model_bounds;

/* Prepared programs own construction rows and isolated part shader snapshots.
 * Bounds metadata does not borrow the shader payload, which may be moved into
 * the shared shape catalog. */
typedef struct
{
    uint64_t node;
    char *name, *path, *asset;
    int distance, position; /* Derived IR roots, -1 for unused parts. */
    int material; /* Material slot index, -1 for the entity material. */
} model_part;
typedef struct
{
    uint64_t id;
    const char *name, *type_name;
    pin_type type;
    int width;
    float defaults[4], minimum[4], maximum[4];
} model_parameter_decl;
typedef struct
{
    int slot, width;
    uint64_t node;
    char *asset, *name, *type_name,
        *scope; /* Stable invocation path, empty for shared defaults. */
    pin_type type;
    float defaults[4], minimum[4], maximum[4];
    bool hidden; /* Derived payload, never an editable public override. */
} model_parameter;
#define MODEL_MATERIAL_SLOT_MAX 32
typedef struct
{
    uint64_t id;
    const char *name, *material;
} model_material_decl;
typedef struct
{
    int slot;
    uint64_t node;
    char *asset, *name, *scope, *material;
} model_material_slot;
typedef struct
{
    model_material_slot *slots;
    int *part_slots;
    char **dependencies;
} model_material_bindings;
typedef enum
{
    MODEL_BINDINGS_INVALID,
    MODEL_BINDINGS_GEOMETRY_CHANGED,
    MODEL_BINDINGS_READY
} model_bindings_result;
typedef struct
{
    int part, material;
    float position[3];
    float part_from_object[3][3];
} model_hit;
typedef struct
{
    uint64_t id;
    const char *name;
} model_anchor_decl;
typedef struct
{
    uint64_t node;
    char *asset, *name, *scope;
    int position, angles;
} model_anchor;
struct model_bounds_program;
typedef struct
{
    shader_graph shader,
        hit_shader; /* Hit outputs: owner part index, part-local point. */
    shader_image_resolver resolve_image;
    void *image_userdata;
    model_bounds bounds;
    char **dependencies, **material_dependencies;
    model_part *parts;
    model_anchor *anchors;
    model_material_slot *material_slots;
    model_parameter *parameters; /* Array index is the emitted uniform slot. */
    struct model_bounds_program *bounds_program;
} model_program;

void model_material_bindings_init(model_material_bindings *bindings);
void model_material_bindings_free(model_material_bindings *bindings);
void model_program_init(model_program *program);
void model_program_free(model_program *program);
/* NULL incoming selects declared defaults. Invalid input preserves both
 * outputs; the successful payload includes private derived uniforms. */
bool model_program_prepare(
    const model_program *program,
    const float incoming[SHADER_PARAMETER_MAX][4],
    float prepared[SHADER_PARAMETER_MAX][4],
    model_bounds *bounds,
    shader_error *error
);
bool model_program_parameters(
    const model_program *program,
    float parameters[SHADER_PARAMETER_MAX][4],
    shader_error *error
);
bool model_program_bounds(
    const model_program *program,
    const float parameters[SHADER_PARAMETER_MAX][4],
    model_bounds *bounds,
    shader_error *error
);
/* Inclusive selected subtree bounds, before outer CSG clipping. */
bool model_program_part_bounds(
    const model_program *,
    const float prepared[32][4],
    int part,
    model_bounds *,
    shader_error *
);
bool model_program_anchor(
    const model_program *,
    int index,
    const float prepared[SHADER_PARAMETER_MAX][4],
    float position[3],
    float angles[3],
    shader_error *
);
/* Uses immutable owned resolver metadata even if shader payloads were moved. */
bool model_program_hit(
    const model_program *program,
    const float prepared[SHADER_PARAMETER_MAX][4],
    const float object_position[3],
    model_hit *hit,
    shader_error *error
);
/* Immutable isolated subtree snapshots; distance remains in Part units.
 * Both outputs must be initialized and distinct; failures preserve them. */
bool model_program_part_shaders(
    const model_program *program,
    int part,
    shader_graph *distance,
    shader_graph *hit,
    shader_error *error
);
/* Analytic selected-part coordinate Jacobian; no distance/owner resampling. */
bool model_program_part_point(
    const model_program *program,
    const float prepared[SHADER_PARAMETER_MAX][4],
    int part,
    const float point[3],
    float local[3],
    float rows[3][3],
    shader_error *error
);
bool model_program_frame(
    const model_program *program,
    int part,
    const float prepared[SHADER_PARAMETER_MAX][4],
    float rows[3][3],
    shader_error *error
);
char *model_program_frame_glsl(const model_program *program, const char *prefix, shader_error *error);

#endif
