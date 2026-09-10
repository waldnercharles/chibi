#ifndef CHIBI_SHADER_CATALOG_H
#define CHIBI_SHADER_CATALOG_H
#include "ecs/ecs.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "runtime/components.h"

bool render_shader_upsert(const behavior_asset *asset, shader_error *error);
int render_shader_load(const char *content_root);
int render_shader_find(const char *name);
const char *render_shader_name(int index);
int render_shader_count();
const char *render_shader_parameter_name(int index, int slot);
int render_shader_parameter_width(int index, int slot);
void render_shader_parameter_default(int index, int slot, float value[4]);
bool render_shader_is_shape(int index);
bool render_shader_may_be_transparent(int index);
bool render_shader_sample(
    int index,
    const shader_sample_context *sample,
    const float parameters[SHADER_PARAMETER_MAX][4],
    float result[SHADER_OUTPUT_MAX][4],
    shader_error *error
);
unsigned render_shader_revision();
unsigned render_shader_ramp_revision();
/* Leaves room for Cute's atlas/instance texture within GLES3's 16 fragment units. */
enum
{
    RENDER_SHADER_IMAGE_MAX = 8,
    RENDER_SHADER_RAMP_VEC4_MAX = 512
};
typedef struct
{
    int images[RENDER_SHADER_IMAGE_MAX];
    int count;
} render_shader_set;
bool render_shader_set_add(render_shader_set *set, int asset, shader_error *error);
bool render_shader_set_contains(const render_shader_set *set, int asset);
int render_shader_ramp_vec4_count(const render_shader_set *set);
void render_shader_ramp_payload(const render_shader_set *set, float (*payload)[4]);
char *render_shader_glsl(const render_shader_set *set, int sampler_set, int first_binding);
void render_shader_parameters(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *json,
    float values[SHADER_PARAMETER_MAX][4]
);
bool render_shader_bounds(int index, float bounds[2]);
bool render_shader_contains(
    const GameShader *binding,
    ecs_world *world,
    ecs_entity entity,
    const float local[2],
    const float world_position[2],
    float time
);
void render_shader_clear_world(ecs_world *world);
void render_shader_catalog_clear();
extern const registry_pack SHADER_BINDING_PACK;
#endif
