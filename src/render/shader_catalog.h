#ifndef CBLUEPRINT_SHADER_CATALOG_H
#define CBLUEPRINT_SHADER_CATALOG_H
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
unsigned render_shader_revision();
char *render_shader_glsl(); /* malloc-owned function definitions and dispatch */
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
