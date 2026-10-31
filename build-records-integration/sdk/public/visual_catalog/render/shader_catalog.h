#ifndef CHIBI_SHADER_CATALOG_H
#define CHIBI_SHADER_CATALOG_H

#define SHADER_VALUES_PREPARED_VERSION 1
#include "ecs/ecs.h"
#include "runtime/components.h"
#include "runtime/models/model_inspection.h"
#include "runtime/models/model_program.h"
#include "runtime/shaders/inspection.h"
#include "runtime/shaders/program.h"

/* Validate and publish a complete proposed model dependency generation. */

float render_shader_distance_lipschitz(int index);

int render_shader_find(const char *name);
/* The name of the shader render_shader_find returned an id for. */
const char *render_shader_name(int id);
const char *render_shader_parameter_name(int index, int slot);
int render_shader_parameter_width(int index, int slot);
/* Copies applied public metadata. Strings are borrowed until the next model
   catalog publication or clear, including parameter-only revisions. */
bool render_shader_model_parameter(int index, int slot, model_parameter *out);
/* Borrowed applied report; expires on catalog publication or clear. */
const model_inspection *render_shader_model_inspection(int index);
bool render_shader_material_inspection(int index, shader_inspection *out);
bool render_shader_model_anchor(int index, int slot, model_anchor *out);
bool render_shader_model_anchor_values(
    int index,
    int slot,
    const float prepared[SHADER_PARAMETER_MAX][4],
    float position[3],
    float angles[3],
    shader_error *error
);
bool render_shader_model_part_bounds(
    int index,
    const float prepared[SHADER_PARAMETER_MAX][4],
    int part,
    model_bounds *out,
    shader_error *error
);
bool render_shader_model_material_slot(int index, int slot, model_material_slot *out);
bool render_shader_model_preview_snapshot(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *parameters,
    int part,
    model_bounds *bounds,
    shader_error *error
);
bool render_shader_model_preview_factor(
    int index,
    const float prepared[32][4],
    int part,
    float *factor,
    shader_error *error
);
char *render_shader_model_preview_glsl(int index, int part, shader_error *error);
bool render_shader_model_preview_bounds(
    int index,
    const float prepared[32][4],
    int part,
    model_bounds *bounds,
    shader_error *error
);
bool render_shader_model_hit(
    int index,
    const float prepared[32][4],
    const float object_position[3],
    model_hit *out,
    shader_error *error
);
bool render_shader_binding_parameters_validate(const Shader *binding, const char *text, shader_error *error);

typedef struct
{
    /* Successful CPU source lowerings published. Runtime setters do not compile. */
    int compilations;
    /* Cached render snapshot evaluations; setter validation is separate. */
    int parameter_updates, bounds_updates, cache_hits;
    int setter_validations;
    unsigned program_revision, parameter_revision;
} render_model_stats;
render_model_stats render_shader_model_stats();
void render_shader_model_stats_reset();
bool render_shader_model_snapshot(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *text,
    float prepared[32][4],
    float min[3],
    float max[3],
    bool *empty,
    shader_error *error
);
bool render_shader_is_shape(int index);
/* Validated material IR, borrowed until catalog publication or clear. */
const shader_graph *render_shader_material_graph(int index);
/* Exact centered sphere with a constant or scalar parameter radius. */
bool render_shader_sphere_radius(int index, const float parameters[32][4], float *radius);
bool render_shader_is_model(int index);
bool render_shader_is_particle(int index);
/* malloc-owned portable function named sg_particle; rejected unless the catalog
 * record satisfies the complete particle signature and operation contract. */
char *render_shader_particle_glsl(int index, shader_error *);
/* Model bounds use the same validated parameter snapshot as distance evaluation. */
bool render_shader_model_bounds(
    int index,
    const float parameters[32][4],
    float min[3],
    float max[3],
    bool *empty,
    shader_error *error
);
bool render_shader_may_be_transparent(int index);
/* The Alpha Mode of loaded shader `index`. */
shader_alpha_mode render_shader_alpha_mode(int index);
bool render_shader_sample(
    int index,
    const shader_sample_context *sample,
    const float parameters[SHADER_PARAMETER_MAX][4],
    float result[SHADER_OUTPUT_MAX][4],
    shader_error *error
);
unsigned render_shader_revision();
unsigned render_shader_ramp_revision();
/* Leaves room within GLES3's 16 fragment units for Cute's atlas, the ramp
 * table, the scene's maps or the raster's storage textures. Later raster
 * batches also sample the previous batch's five targets, so on GLES the raster
 * splits them sooner; see render_program_samplers. */
enum
{
    RENDER_SHADER_IMAGE_MAX = 8,
    RENDER_SHADER_RAMP_VEC4_MAX = 512
};
typedef struct
{
    int images[RENDER_SHADER_IMAGE_MAX];
    int count;
    /* Nonzero shape selects only this SDF shape/material pair. */
    int shape, material;
} render_shader_set;
bool render_shader_set_add(render_shader_set *set, int asset, shader_error *error);
/* Adds one image variant, such as a sprite's, for sg_image_sample. */
bool render_shader_set_add_image(render_shader_set *set, int image, shader_error *error);
/* Transactional slot bindings and one shared material parameter payload.
   Entry 0 is the entity fallback; compiled slot i uses entry i+1. */
bool render_shader_model_material_snapshot(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *text,
    int fallback,
    float bindings[MODEL_MATERIAL_SLOT_MAX + 1][4],
    float parameters[32][4],
    render_shader_set *resources,
    shader_error *error
);
int render_shader_ramp_vec4_count(const render_shader_set *set);
void render_shader_ramp_payload(const render_shader_set *set, float (*payload)[4]);
char *render_shader_glsl(const render_shader_set *set, int sampler_set, int first_binding);
void render_shader_parameters(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *text,
    float values[SHADER_PARAMETER_MAX][4]
);
bool render_shader_bounds(int index, float bounds[2]);
bool render_shader_contains(
    const Shader *binding,
    ecs_world *world,
    ecs_entity entity,
    const float local[2],
    const float world_position[2],
    float time
);
/* Same validated transient override path as Set Shape Parameter. NULL means
   success; returned error text is borrowed until the next call on this thread. */
const char *render_shader_shape_parameter_set(
    ecs_world *world,
    ecs_entity entity,
    const char *name,
    pin_value value
);
void render_shader_clear_world(ecs_world *world);
/* Shape and material overrides on this world's entities, including shader
   identity, hashed for scenario state inspection. */
uint64_t render_shader_overrides_hash(const ecs_world *world);
void render_shader_catalog_clear();

#endif

/* Caller-owned cache. Catalog and overrides remain immutable during collection. */
typedef struct render_shader_parameter_cache render_shader_parameter_cache;
void render_shader_parameter_cache_free(render_shader_parameter_cache *cache);
void render_shader_parameters_cached(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *text,
    float values[32][4],
    render_shader_parameter_cache **cache
);

/* Baked model groups retain the compiled material slot name. Applies selected
 * material.Slot.Name / Slot.Name and unqualified material values; ignores
 * geometry/other slots. Invalid selected input preserves values and cache.
 * Runtime world/entity material overrides remain authoritative. */
bool render_shader_material_parameters_scoped_cached(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *text,
    const char *scope,
    float values[32][4],
    render_shader_parameter_cache **cache,
    shader_error *error
);
