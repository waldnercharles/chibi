#include "render/arena3d.h"

#include "platform/cute.h"
#include "render/arena_geometry.h"
#include "render/arena_influence.h"
#include "render/arena_trail.h"
#include "render/arena_visual.h"
#include "render/material.h"
#include "render/shader_catalog.h"
#include "render/shader_program.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/particle_emitters.h"
#include "runtime/rhythm.h"
#include "runtime/task_pool.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    PARAMETER_TEXTURE_WIDTH = 2048,
    MATERIAL_TEXELS = 38,
    POINT_LIGHT_LIMIT = 32
};

typedef struct
{
    CF_M4x4 frame;
    GameGeometry3D shape;
    float depth;
    int material, row, program;
    int particle_shape;
    float particle_age;
    bool transparent;
} arena_draw;

typedef struct
{
    CF_V4 *pixels;
    arena_draw *draws;
    render_shader_parameter_cache *cache;
    char binding_error[256];
    unsigned worker;
} draw_buffer;

struct render_arena
{
    render_programs *programs;
    render_influences *influences;
    arena_trails *trails;
    game_particle_emitters *emitters;
    CF_Mesh prism, octahedron, tetrahedron;
    CF_Mesh particle_quad;
    CF_Mesh polygons[4];
    CF_V4 rhythm;
    CF_Texture parameters;
    int texture_height;
    draw_buffer buffer, *chunks;
    ecs_entity *entities;
    game_task_pool *pool;
    bool parallel_disabled;
    render_arena_work work;
    CF_V4 light_positions[POINT_LIGHT_LIMIT];
    CF_V4 light_colors[POINT_LIGHT_LIMIT];
    float light_scores[POINT_LIGHT_LIMIT];
    int light_count, lights_dropped;
    int simulation_particles;
    char binding_error[256];
};

static CF_Mesh particle_quad()
{
    typedef struct
    {
        CF_V3 position, normal;
    } vertex;
    vertex vertices[] = {
        { { -.5f, -.5f, 0 }, { 0, 0, 1 } }, { { .5f, -.5f, 0 }, { 0, 0, 1 } },
        { { .5f, .5f, 0 }, { 0, 0, 1 } },   { { -.5f, -.5f, 0 }, { 0, 0, 1 } },
        { { .5f, .5f, 0 }, { 0, 0, 1 } },   { { -.5f, .5f, 0 }, { 0, 0, 1 } },
    };
    CF_VertexAttribute attributes[] = {
        { .name = "in_pos",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(vertex, position) },
        { .name = "in_normal",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(vertex, normal) },
    };
    CF_Mesh mesh = cf_make_mesh(sizeof vertices, attributes, 2, sizeof(vertex));
    cf_mesh_update_vertex_data(mesh, vertices, 6);
    return mesh;
}

render_arena *render_arena_new()
{
    render_arena *arena = xcalloc(1, sizeof *arena);
    arena->pool = game_task_pool_acquire();
    arena->programs = render_programs_new(RENDER_PROGRAM_ARENA);
    arena->influences = render_influences_new();
    arena->trails = render_arena_trails_new();
    arena->emitters = game_particle_emitters_new(NULL);
    arena->prism = render_arena_prism_mesh();
    arena->octahedron = render_arena_octahedron_mesh();
    arena->tetrahedron = render_arena_tetrahedron_mesh();
    arena->particle_quad = particle_quad();
    for (int i = 0; i < 4; i++) {
        arena->polygons[i] = render_arena_polygon_mesh(GAME_PRIMITIVE_CHEVRON_PYRAMID + i);
    }
    return arena;
}

const char *render_arena_error(const render_arena *arena)
{
    return arena->binding_error[0] ? arena->binding_error
                                   : render_programs_error(arena->programs);
}

int render_arena_particle_count(const render_arena *arena)
{
    int heads;
    game_particle_emitters_heads(arena->emitters, &heads);
    return arena->simulation_particles + heads +
           game_particles_count(game_particle_emitters_particles(arena->emitters));
}

void render_arena_reset(render_arena *arena)
{
    arena->simulation_particles = 0;
    render_influences_reset(arena->influences);
    render_arena_trails_reset(arena->trails);
    game_particle_emitters_reset(arena->emitters);
}

void render_arena_free(render_arena *arena)
{
    if (!arena) {
        return;
    }
    render_programs_free(arena->programs);
    render_influences_free(arena->influences);
    render_arena_trails_free(arena->trails);
    game_particle_emitters_free(arena->emitters);
    cf_destroy_mesh(arena->prism);
    cf_destroy_mesh(arena->octahedron);
    cf_destroy_mesh(arena->tetrahedron);
    cf_destroy_mesh(arena->particle_quad);
    for (int i = 0; i < 4; i++) {
        cf_destroy_mesh(arena->polygons[i]);
    }
    if (arena->parameters.id) {
        cf_destroy_texture(arena->parameters);
    }
    game_task_pool_release(arena->pool);
    for (int i = 0; i < acount(arena->chunks); i++) {
        afree(arena->chunks[i].pixels);
        afree(arena->chunks[i].draws);
        render_shader_parameter_cache_free(arena->chunks[i].cache);
    }
    afree(arena->chunks);
    afree(arena->entities);
    render_shader_parameter_cache_free(arena->buffer.cache);
    afree(arena->buffer.pixels);
    afree(arena->buffer.draws);
    free(arena);
}

static void draw_shape(const render_arena *arena, const GameGeometry3D *shape)
{
    cf_draw3d_scale((CF_V3){ shape->width, shape->height, shape->depth });
    switch (shape->primitive) {
        case GAME_PRIMITIVE_BOX:
            cf_draw3d_cube((CF_V3){ 0 }, (CF_V3){ 0.5f, 0.5f, 0.5f });
            break;
        case GAME_PRIMITIVE_SPHERE: cf_draw3d_sphere((CF_V3){ 0 }, 0.5f); break;
        case GAME_PRIMITIVE_CAPSULE:
            cf_draw3d_scale((CF_V3){ 2, 1, 2 });
            cf_draw3d_capsule((CF_V3){ 0, -0.25f, 0 }, (CF_V3){ 0, 0.25f, 0 }, 0.25f);
            break;
        case GAME_PRIMITIVE_CONE:
            cf_draw3d_cone((CF_V3){ 0, -0.5f, 0 }, (CF_V3){ 0, 0.5f, 0 }, 0.5f);
            break;
        case GAME_PRIMITIVE_TRIANGLE_PRISM: cf_draw3d_mesh(arena->prism); break;
        case GAME_PRIMITIVE_OCTAHEDRON:
            cf_draw3d_mesh(arena->octahedron);
            break;
        case GAME_PRIMITIVE_TETRAHEDRON:
            cf_draw3d_mesh(arena->tetrahedron);
            break;
        case GAME_PRIMITIVE_CHEVRON_PYRAMID:
        case GAME_PRIMITIVE_BOX_FRAME:
        case GAME_PRIMITIVE_TRIANGLE_OUTLINE:
        case GAME_PRIMITIVE_TRIANGLE:
            cf_draw3d_mesh(arena->polygons[shape->primitive - GAME_PRIMITIVE_CHEVRON_PYRAMID]);
            break;
    }
}

static void append_draw(
    draw_buffer *buffer,
    arena_draw draw,
    const game_material_coefficients *color,
    const float parameters[SHADER_PARAMETER_MAX][4],
    const render_arena_camera *camera
)
{
    draw.row = acount(buffer->draws);
    draw.transparent = draw.particle_shape ||
                       (draw.material ? render_shader_may_be_transparent(draw.material)
                                      : color->scene[3] < 1);
    CF_V4 origin = { draw.frame.elements[12],
                     draw.frame.elements[13],
                     draw.frame.elements[14],
                     1 };
    draw.depth = -cf_mul_m4_v4(camera->view, origin).z;
    int start = acount(buffer->pixels);
    asetlen(buffer->pixels, start + MATERIAL_TEXELS);
    CF_V4 *row = &buffer->pixels[start];
    memset(row, 0, MATERIAL_TEXELS * sizeof *row);
    memcpy(&row[0], color->scene, sizeof *row);
    memcpy(&row[1], color->emission, sizeof *row);
    if (parameters) {
        memcpy(&row[2], parameters, SHADER_PARAMETER_MAX * sizeof *row);
    }
    if (draw.particle_shape) {
        row[2].x = draw.particle_age;
    }
    CF_M4x4 inverse = cf_m4_invert(draw.frame);
    for (int i = 0; i < 3; i++) {
        row[34 + i] = (CF_V4){ inverse.elements[i],
                               inverse.elements[4 + i],
                               inverse.elements[8 + i],
                               inverse.elements[12 + i] };
    }
    row[37] = (CF_V4){ draw.shape.width, draw.shape.height, draw.shape.depth, 0 };
    apush(buffer->draws, draw);
}

static void collect_entity(
    draw_buffer *buffer,
    ecs_world *world,
    ecs_entity entity,
    const render_arena_camera *camera,
    const engine *runtime
)
{
    GameTransform position = *GAME_GameTransform_get_const(world, entity);
    if (!engine_entity_visible(runtime, world, entity)) {
        return;
    }
    if (runtime) {
        engine_render_position(runtime, entity, &position);
    }
    arena_visual visuals[ARENA_PART_LIMIT];
    char error[256] = { 0 };
    int count = render_arena_visuals(world, entity, &position, visuals, error, sizeof error);
    if (count <= 0) {
        if (count < 0 && !buffer->binding_error[0]) {
            snprintf(buffer->binding_error, sizeof buffer->binding_error, "%s", error);
        }
        return;
    }
    const GameAppearance *appearance = GAME_GameAppearance_get_const(world, entity);
    const GameMaterial *material = GAME_GameMaterial_get_const(world, entity);
    GameMaterial fallback = { .scene = appearance ? appearance->color : 0xffffffffu };
    game_material_coefficients color;
    if (!game_material_extract(material ? material : &fallback, &color)) {
        return;
    }
    const GameShader *binding = GAME_GameShader_get_const(world, entity);
    CF_M4x4 view_projection = cf_mul_m4(camera->projection, camera->view);
    for (int i = 0; i < count; i++) {
        const arena_visual *visual = &visuals[i];
        const char *name = visual->part.material[0] ? visual->part.material
                         : binding                  ? binding->material
                                                    : NULL;
        int id = render_shader_find(name);
        if (name && *name && (!id || render_shader_is_shape(id))) {
            if (!buffer->binding_error[0]) {
                snprintf(buffer->binding_error, sizeof buffer->binding_error, "Unknown surface material: %s", name);
            }
            return;
        }
        if (!render_arena_visual_visible(visual, view_projection)) {
            continue;
        }
        float parameters[SHADER_PARAMETER_MAX][4];
        render_shader_parameters_cached(
            id,
            world,
            entity,
            binding ? binding->parameters : NULL,
            parameters,
            &buffer->cache
        );
        arena_draw draw = { .frame = visual->frame,
                            .shape = { .primitive = visual->part.primitive,
                                       .width = visual->part.dimensions.x,
                                       .height = visual->part.dimensions.y,
                                       .depth = visual->part.dimensions.z },
                            .material = id };
        append_draw(buffer, draw, &color, parameters, camera);
    }
}

static void collect_particles(
    render_arena *arena,
    const game_particle *items,
    int count,
    const render_arena_camera *camera
)
{
    CF_M4x4 camera_frame = cf_m4_invert(camera->view);
    CF_V3 right = { camera_frame.elements[0],
                    camera_frame.elements[1],
                    camera_frame.elements[2] };
    CF_V3 up = { camera_frame.elements[4],
                 camera_frame.elements[5],
                 camera_frame.elements[6] };
    for (int i = 0; i < count; i++) {
        const game_particle *particle = &items[i];
        float life = fmaxf(0, 1 - particle->age / particle->lifetime);
        float age = 1 - life;
        float radius = particle->size > 0 ? game_particle_curve_value(
                                                particle->size,
                                                particle->size_end,
                                                particle->size_exponent,
                                                age
                                            )
                                          : 1 + life;
        if (particle->shape == GAME_PARTICLE_RING) {
            radius = particle->size * (.45f + .55f * age);
        }
        if (radius <= 0) {
            continue;
        }
        float tint[4];
        for (int c = 0; c < 4; c++) {
            int shift = 24 - c * 8;
            float start = (float)((particle->color >> shift) & 255u) / 255;
            float end = (float)((particle->end_color >> shift) & 255u) / 255;
            tint[c] = particle->size > 0 ? start + (end - start) * age : start;
        }
        if (particle->size <= 0) {
            tint[3] *= life;
        }
        float intensity = game_particle_curve_value(
            particle->emission,
            particle->emission_end,
            particle->emission_exponent,
            age
        );
        game_material_coefficients color = {
            .scene = { tint[0], tint[1], tint[2], tint[3] },
            .emission = { tint[0] * intensity, tint[1] * intensity, tint[2] * intensity, 1 },
        };
        CF_V3 direction = { cosf(particle->angle), 0, sinf(particle->angle) };
        float angle = atan2f(cf_dot_v3(direction, up), cf_dot_v3(direction, right));
        CF_V3 x = cf_add_v3(cf_mul_v3_f(right, cosf(angle)), cf_mul_v3_f(up, sinf(angle)));
        CF_V3 y = cf_sub_v3(cf_mul_v3_f(up, cosf(angle)), cf_mul_v3_f(right, sinf(angle)));
        CF_M4x4 frame = camera_frame;
        frame.elements[0] = x.x;
        frame.elements[1] = x.y;
        frame.elements[2] = x.z;
        frame.elements[4] = y.x;
        frame.elements[5] = y.y;
        frame.elements[6] = y.z;
        frame.elements[12] = particle->position.x;
        frame.elements[13] = particle->elevation;
        frame.elements[14] = particle->position.y;
        float stretch = particle->shape == GAME_PARTICLE_STREAK ? 2.8f : 1;
        arena_draw draw = {
            .frame = frame,
            .particle_shape = particle->shape + 1,
            .particle_age = age,
            .shape = { .width = radius * 2 * stretch, .height = radius * 2, .depth = 1 },
        };
        append_draw(&arena->buffer, draw, &color, NULL, camera);
    }
}

static void collect_lights(
    render_arena *arena,
    ecs_world *world,
    const render_arena_camera *camera,
    const engine *runtime
)
{
    arena->light_count = 0;
    int candidates = 0;
    CF_M4x4 inverse = cf_m4_invert(camera->view);
    CF_V3 eye = { inverse.elements[12], inverse.elements[13], inverse.elements[14] };
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GamePointLight.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) != ECS_OK) {
        return;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        if (!engine_entity_visible(runtime, world, entity)) {
            continue;
        }
        const GamePointLight *light = GAME_GamePointLight_get_const(world, entity);
        GameTransform position = *GAME_GameTransform_get_const(world, entity);
        if (runtime) {
            engine_render_position(runtime, entity, &position);
        }
        if (!isfinite(position.x) || !isfinite(position.y) ||
            !isfinite(light->intensity) || !isfinite(light->radius) ||
            !isfinite(light->elevation) || light->intensity <= 0 ||
            light->radius <= 0 || !(light->color >> 8)) {
            continue;
        }
        double dx = position.x - (double)eye.x,
               dy = light->elevation - (double)eye.y,
               dz = position.y - (double)eye.z,
               radius2 = (double)light->radius * light->radius;
        unsigned red = (light->color >> 24) & 255u,
                 green = (light->color >> 16) & 255u,
                 blue = (light->color >> 8) & 255u;
        float peak = fmaxf((float)red, fmaxf((float)green, (float)blue)) / 255;
        float score = (float)(light->intensity * peak * radius2 /
                              (dx * dx + dy * dy + dz * dz + radius2));
        int slot = arena->light_count;
        candidates++;
        if (slot == POINT_LIGHT_LIMIT) {
            slot--;
            if (score <= arena->light_scores[slot]) {
                continue;
            }
        } else {
            arena->light_count++;
        }
        while (slot > 0 && score > arena->light_scores[slot - 1]) {
            arena->light_scores[slot] = arena->light_scores[slot - 1];
            arena->light_positions[slot] = arena->light_positions[slot - 1];
            arena->light_colors[slot] = arena->light_colors[slot - 1];
            slot--;
        }
        arena->light_scores[slot] = score;
        arena->light_positions[slot] = (CF_V4){ position.x,
                                                light->elevation,
                                                position.y,
                                                light->radius };
        /* Packed component colors use the same linear channel convention as Material. */
        arena->light_colors[slot] = (CF_V4){ ((light->color >> 24) & 255u) / 255.0f,
                                             ((light->color >> 16) & 255u) / 255.0f,
                                             ((light->color >> 8) & 255u) / 255.0f,
                                             light->intensity };
    }
    ecs_query_end(&query);
    int dropped = candidates - arena->light_count;
    if ((dropped > 0) != (arena->lights_dropped > 0)) {
        fprintf(
            stderr,
            "Arena point lights: %d active, %d omitted by the %d-light camera "
            "influence budget.\n",
            arena->light_count,
            dropped,
            POINT_LIGHT_LIMIT
        );
    }
    arena->lights_dropped = dropped;
}

static int compare_draws(const void *left, const void *right)
{
    const arena_draw *a = left, *b = right;
    if (a->transparent != b->transparent) {
        return a->transparent ? 1 : -1;
    }
    if (a->transparent && a->depth != b->depth) {
        return a->depth > b->depth ? -1 : 1;
    }
    if (!a->transparent && a->program != b->program) {
        return a->program < b->program ? -1 : 1;
    }
    if (!a->transparent && a->shape.primitive != b->shape.primitive) {
        return a->shape.primitive < b->shape.primitive ? -1 : 1;
    }
    return (a->row > b->row) - (a->row < b->row);
}

static bool submit(render_arena *arena, const render_arena_camera *camera, float time)
{
    if (!acount(arena->buffer.draws)) {
        render_arena_trails_draw(arena->trails, camera);
        return true;
    }
    for (int i = 0; i < acount(arena->buffer.draws); i++) {
        arena_draw *draw = &arena->buffer.draws[i];
        render_shader_set set = { 0 };
        shader_error error = { 0 };
        if (!render_shader_set_add(&set, draw->material, &error)) {
            snprintf(
                arena->binding_error,
                sizeof arena->binding_error,
                "%s",
                error.message
            );
            return false;
        }
        draw->program = render_programs_prepare(arena->programs, &set);
        if (render_programs_error(arena->programs)[0]) {
            snprintf(
                arena->binding_error,
                sizeof arena->binding_error,
                "%s",
                render_programs_error(arena->programs)
            );
        }
        if (!draw->program) {
            return false;
        }
    }
    int cells = acount(arena->buffer.pixels);
    int height = 1;
    while (height * PARAMETER_TEXTURE_WIDTH < cells) {
        height *= 2;
    }
    if (height > 2048) {
        snprintf(arena->binding_error, sizeof arena->binding_error, "Arena material instance capacity exceeded.");
        return false;
    }
    if (height > arena->texture_height) {
        if (arena->parameters.id) {
            cf_destroy_texture(arena->parameters);
        }
        CF_TextureParams params = cf_texture_defaults(PARAMETER_TEXTURE_WIDTH, height);
        params.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
        params.filter = CF_FILTER_NEAREST;
        params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT;
        params.stream = true;
        arena->parameters = cf_make_texture(params);
        arena->texture_height = height;
    }
    asetlen(arena->buffer.pixels, arena->texture_height * PARAMETER_TEXTURE_WIDTH);
    memset(
        arena->buffer.pixels + cells,
        0,
        (size_t)(acount(arena->buffer.pixels) - cells) * sizeof *arena->buffer.pixels
    );
    cf_texture_update(
        arena->parameters,
        arena->buffer.pixels,
        acount(arena->buffer.pixels) * (int)sizeof *arena->buffer.pixels
    );
    qsort(
        arena->buffer.draws,
        (size_t)acount(arena->buffer.draws),
        sizeof *arena->buffer.draws,
        compare_draws
    );
    CF_M4x4 inverse = cf_m4_invert(camera->view);
    CF_V4 eye = { inverse.elements[12], inverse.elements[13], inverse.elements[14], time };
    cf_draw3d_set_uniform("arena_camera_time", &eye, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("arena_rhythm", &arena->rhythm, CF_UNIFORM_TYPE_FLOAT4, 1);
    CF_V4 light_count = { (float)arena->light_count };
    cf_draw3d_set_uniform("arena_light_count", &light_count, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("arena_light_positions", arena->light_positions, CF_UNIFORM_TYPE_FLOAT4, POINT_LIGHT_LIMIT);
    cf_draw3d_set_uniform("arena_light_colors", arena->light_colors, CF_UNIFORM_TYPE_FLOAT4, POINT_LIGHT_LIMIT);
    render_influences_bind(arena->influences);
    cf_draw3d_set_texture("arena_parameters", arena->parameters);
    cf_draw3d_push_projection(camera->projection);
    cf_draw3d_push_view(camera->view);
    int program = 0;
    CF_RenderState state = cf_render_state_3d_defaults();
    cf_draw3d_push_render_state(state);
    bool transparent = false;
    for (int i = 0; i < acount(arena->buffer.draws); i++) {
        const arena_draw *draw = &arena->buffer.draws[i];
        if (program != draw->program) {
            if (program) {
                render_programs_unbind3d(arena->programs, program);
            }
            program = draw->program;
            render_programs_bind3d(arena->programs, program);
        }
        if (draw->transparent && !transparent) {
            render_arena_trails_draw(arena->trails, camera);
            transparent = true;
            cf_draw3d_pop_render_state();
            state.depth_write_enabled = false;
            state.blend.enabled = true;
            state.blend.rgb_src_blend_factor = CF_BLENDFACTOR_SRC_ALPHA;
            state.blend.rgb_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            state.blend.alpha_src_blend_factor = CF_BLENDFACTOR_ONE;
            state.blend.alpha_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            cf_draw3d_push_render_state(state);
        }
        CF_V4 material = { (float)draw->row,
                           (float)draw->material,
                           (float)draw->shape.primitive,
                           (float)draw->particle_shape };
        /* Cute solids send the first attributes to in_uv_rect; custom meshes send the second. */
        cf_draw3d_push_mesh_attributes(material);
        cf_draw3d_push_mesh_attributes2(material);
        cf_draw3d_push();
        cf_draw3d_transform(draw->frame);
        if (draw->particle_shape) {
            cf_draw3d_scale((CF_V3){ draw->shape.width, draw->shape.height, 1 });
            cf_draw3d_mesh(arena->particle_quad);
        } else {
            draw_shape(arena, &draw->shape);
        }
        cf_draw3d_pop();
        cf_draw3d_pop_mesh_attributes2();
        cf_draw3d_pop_mesh_attributes();
    }
    cf_draw3d_pop_render_state();
    render_programs_unbind3d(arena->programs, program);
    /* Later meshes would otherwise keep the parameters bound past their resize or free. */
    cf_draw3d_set_texture("arena_parameters", (CF_Texture){ 0 });
    cf_draw3d_pop_view();
    cf_draw3d_pop_projection();
    if (!transparent) {
        render_arena_trails_draw(arena->trails, camera);
    }
    return true;
}

enum
{
    DRAW_CHUNK = 512
};
typedef struct
{
    render_arena *arena;
    ecs_world *world;
    const render_arena_camera *camera;
    const engine *runtime;
} collect_context;
static void collect_range(int start, int end, uint32_t worker, void *user)
{
    collect_context *ctx = user;
    render_arena *arena = ctx->arena;
    for (int c = start; c < end; c++) {
        draw_buffer *buffer = &arena->chunks[c];
        aclear(buffer->pixels);
        aclear(buffer->draws);
        buffer->binding_error[0] = 0;
        buffer->worker = worker;
        int limit = (c + 1) * DRAW_CHUNK;
        if (limit > acount(arena->entities)) {
            limit = acount(arena->entities);
        }
        for (int i = c * DRAW_CHUNK; i < limit; i++) {
            collect_entity(buffer, ctx->world, arena->entities[i], ctx->camera, ctx->runtime);
        }
    }
}
static void collect_geometry(
    render_arena *arena,
    ecs_world *world,
    const render_arena_camera *camera,
    const engine *runtime
)
{
    aclear(arena->entities);
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameGeometry3D.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            apush(arena->entities, entity);
        }
        ecs_query_end(&query);
    }
    int count = acount(arena->entities);
    arena->work = (render_arena_work){ .entities = count, .workers = 1 };
    const game_task_system *tasks = game_task_pool_tasks(arena->pool);
    if (arena->parallel_disabled || count < DRAW_CHUNK * 2 || tasks->workers == 1) {
        arena->buffer.binding_error[0] = 0;
        for (int i = 0; i < count; i++) {
            collect_entity(&arena->buffer, world, arena->entities[i], camera, runtime);
        }
        snprintf(
            arena->binding_error,
            sizeof arena->binding_error,
            "%s",
            arena->buffer.binding_error
        );
        return;
    }
    int chunks = (count + DRAW_CHUNK - 1) / DRAW_CHUNK;
    while (acount(arena->chunks) < chunks) {
        apush(arena->chunks, ((draw_buffer){ 0 }));
    }
    collect_context ctx = { arena, world, camera, runtime };
    game_task_pool_begin(arena->pool, true);
    void *task = tasks->enqueue(collect_range, chunks, 1, &ctx, tasks->user);
    tasks->finish(task, tasks->user);
    game_task_pool_end(arena->pool);
    unsigned workers = 0;
    for (int c = 0; c < chunks; c++) {
        draw_buffer *buffer = &arena->chunks[c];
        workers |= 1u << buffer->worker;
        if (buffer->binding_error[0] && !arena->binding_error[0]) {
            snprintf(
                arena->binding_error,
                sizeof arena->binding_error,
                "%s",
                buffer->binding_error
            );
        }
        int rows = acount(arena->buffer.draws), pixels = acount(arena->buffer.pixels);
        for (int i = 0; i < acount(buffer->draws); i++) {
            arena_draw draw = buffer->draws[i];
            draw.row += rows;
            apush(arena->buffer.draws, draw);
        }
        int size = acount(buffer->pixels);
        if (size) {
            asetlen(arena->buffer.pixels, pixels + size);
            memcpy(
                arena->buffer.pixels + pixels,
                buffer->pixels,
                (size_t)size * sizeof *buffer->pixels
            );
        }
    }
    arena->work.tasks = chunks;
    arena->work.workers = __builtin_popcount(workers);
}
void render_arena_set_parallel(render_arena *arena, bool enabled)
{
    arena->parallel_disabled = !enabled;
}
render_arena_work render_arena_last_work(const render_arena *arena)
{
    return arena->work;
}

bool render_arena_draw(
    render_arena *arena,
    ecs_world *world,
    const game_particles *particles,
    const render_arena_camera *camera,
    float time,
    const struct engine *runtime
)
{
    arena->simulation_particles = game_particles_count(particles);
    arena->binding_error[0] = 0;
    aclear(arena->buffer.pixels);
    aclear(arena->buffer.draws);
    game_beat beat;
    if (!game_rhythm_sample(world, time, &beat)) {
        snprintf(arena->binding_error, sizeof arena->binding_error, "Scene needs one valid rhythm clock.");
        return false;
    }
    arena->rhythm = (CF_V4){ (float)beat.phase,
                             (float)beat.pulse,
                             (float)beat.period,
                             (float)beat.duration };
    collect_lights(arena, world, camera, runtime);
    render_influences_collect(arena->influences, world, runtime, time);
    render_arena_trails_collect(arena->trails, world, runtime, time);
    if (!runtime && !game_particle_emitters_collect(
                        arena->emitters,
                        world,
                        runtime,
                        time,
                        arena->binding_error,
                        sizeof arena->binding_error
                    )) {
        return false;
    }
    collect_geometry(arena, world, camera, runtime);
    collect_particles(arena, game_particles_data(particles), game_particles_count(particles), camera);
    if (!runtime) {
        const game_particles *emitted = game_particle_emitters_particles(arena->emitters);
        collect_particles(arena, game_particles_data(emitted), game_particles_count(emitted), camera);
        int head_count;
        const game_particle *heads = game_particle_emitters_heads(arena->emitters, &head_count);
        collect_particles(arena, heads, head_count, camera);
    }
    if (arena->binding_error[0]) {
        return false;
    }
    if (!render_arena_trails_prepare(
            arena->trails,
            camera,
            arena->binding_error,
            sizeof arena->binding_error
        )) {
        return false;
    }
    return submit(arena, camera, time);
}

bool render_arena_preview(
    render_arena *arena,
    const behavior_asset *asset,
    const render_arena_camera *camera,
    const char *parameters_json,
    float time,
    const render_arena_activity *activity
)
{
    shader_error error = { 0 };
    bool registered = render_shader_upsert(asset, &error);
    bool drawn = render_arena_preview_material(
        arena,
        render_shader_find(asset->name),
        camera,
        parameters_json,
        time,
        activity
    );
    if (!registered) {
        snprintf(arena->binding_error, sizeof arena->binding_error, "%s", error.message);
    }
    return drawn;
}

bool render_arena_preview_material(
    render_arena *arena,
    int material,
    const render_arena_camera *camera,
    const char *parameters_json,
    float time,
    const render_arena_activity *activity
)
{
    arena->simulation_particles = 0;
    game_particle_emitters_reset(arena->emitters);
    arena->binding_error[0] = 0;
    aclear(arena->buffer.pixels);
    aclear(arena->buffer.draws);
    arena->light_count = 0;
    arena->rhythm = (CF_V4){ activity->beat_phase, activity->beat_pulse };
    render_arena_reset(arena);
    render_influences_preview(arena->influences, activity->influence, RENDER_ARENA_PREVIEW_RADIUS);
    if (!material || render_shader_is_shape(material)) {
        return false;
    }
    float parameters[SHADER_PARAMETER_MAX][4];
    render_shader_parameters(material, NULL, (ecs_entity){ 0 }, parameters_json, parameters);
    game_material_coefficients color = { .scene = { 1, 1, 1, 1 } };
    arena_draw draw = { .frame = cf_m4_identity(),
                        .shape = { .primitive = GAME_PRIMITIVE_SPHERE,
                                   .width = RENDER_ARENA_PREVIEW_RADIUS * 2,
                                   .height = RENDER_ARENA_PREVIEW_RADIUS * 2,
                                   .depth = RENDER_ARENA_PREVIEW_RADIUS * 2 },
                        .material = material };
    append_draw(&arena->buffer, draw, &color, parameters, camera);
    return submit(arena, camera, time);
}
