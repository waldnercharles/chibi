#include "render/scene3d.h"

#include "platform/cute.h"
#include "render/material.h"
#include "render/scene3d_geometry.h"
#include "render/scene3d_influence.h"
#include "render/scene3d_shadow.h"
#include "render/scene3d_trail.h"
#include "render/scene3d_visual.h"
#include "render/shader_catalog.h"
#include "render/shader_program.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
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
} scene3d_draw;

typedef struct
{
    CF_V4 *pixels;
    scene3d_draw *draws;
    render_shader_parameter_cache *cache;
    char binding_error[256];
    unsigned worker;
} draw_buffer;

struct render_scene3d
{
    render_programs *programs;
    render_influences *influences;
    scene3d_trails *trails;
    game_particle_emitters *emitters;
    CF_Mesh prism, octahedron, tetrahedron;
    CF_Mesh particle_quad;
    CF_Mesh polygons[4];
    CF_V4 rhythm;
    /* Direction toward the sun, sun radiance, and hemisphere ambient radiance. */
    CF_V4 sun_direction, sun_color, ambient_sky, ambient_ground;
    bool sun_shadows;
    render_scene3d_shadow *shadow;
    CF_M4x4 *shadow_boxes;
    CF_Texture parameters;
    int texture_height;
    draw_buffer buffer, *chunks;
    ecs_entity *entities;
    game_task_pool *pool;
    bool parallel_disabled;
    render_scene3d_work work;
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

render_scene3d *render_scene3d_new()
{
    render_scene3d *scene3d = xcalloc(1, sizeof *scene3d);
    scene3d->pool = game_task_pool_acquire();
    scene3d->programs = render_programs_new(RENDER_PROGRAM_SCENE3D);
    scene3d->influences = render_influences_new();
    scene3d->shadow = render_scene3d_shadow_new();
    scene3d->trails = render_scene3d_trails_new();
    scene3d->emitters = game_particle_emitters_new(NULL);
    scene3d->prism = render_scene3d_prism_mesh();
    scene3d->octahedron = render_scene3d_octahedron_mesh();
    scene3d->tetrahedron = render_scene3d_tetrahedron_mesh();
    scene3d->particle_quad = particle_quad();
    for (int i = 0; i < 4; i++) {
        scene3d->polygons[i] = render_scene3d_polygon_mesh(
            GAME_PRIMITIVE_CHEVRON_PYRAMID + i
        );
    }
    return scene3d;
}

const char *render_scene3d_error(const render_scene3d *scene3d)
{
    return scene3d->binding_error[0] ? scene3d->binding_error
                                     : render_programs_error(scene3d->programs);
}

int render_scene3d_particle_count(const render_scene3d *scene3d)
{
    int heads;
    game_particle_emitters_heads(scene3d->emitters, &heads);
    return scene3d->simulation_particles + heads +
           game_particles_count(game_particle_emitters_particles(scene3d->emitters));
}

void render_scene3d_reset(render_scene3d *scene3d)
{
    scene3d->simulation_particles = 0;
    render_influences_reset(scene3d->influences);
    render_scene3d_trails_reset(scene3d->trails);
    game_particle_emitters_reset(scene3d->emitters);
}

void render_scene3d_free(render_scene3d *scene3d)
{
    if (!scene3d) {
        return;
    }
    render_programs_free(scene3d->programs);
    render_influences_free(scene3d->influences);
    render_scene3d_shadow_free(scene3d->shadow);
    afree(scene3d->shadow_boxes);
    render_scene3d_trails_free(scene3d->trails);
    game_particle_emitters_free(scene3d->emitters);
    cf_destroy_mesh(scene3d->prism);
    cf_destroy_mesh(scene3d->octahedron);
    cf_destroy_mesh(scene3d->tetrahedron);
    cf_destroy_mesh(scene3d->particle_quad);
    for (int i = 0; i < 4; i++) {
        cf_destroy_mesh(scene3d->polygons[i]);
    }
    if (scene3d->parameters.id) {
        cf_destroy_texture(scene3d->parameters);
    }
    game_task_pool_release(scene3d->pool);
    for (int i = 0; i < acount(scene3d->chunks); i++) {
        afree(scene3d->chunks[i].pixels);
        afree(scene3d->chunks[i].draws);
        render_shader_parameter_cache_free(scene3d->chunks[i].cache);
    }
    afree(scene3d->chunks);
    afree(scene3d->entities);
    render_shader_parameter_cache_free(scene3d->buffer.cache);
    afree(scene3d->buffer.pixels);
    afree(scene3d->buffer.draws);
    free(scene3d);
}

static void draw_shape(const render_scene3d *scene3d, const GameGeometry3D *shape)
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
        case GAME_PRIMITIVE_TRIANGLE_PRISM:
            cf_draw3d_mesh(scene3d->prism);
            break;
        case GAME_PRIMITIVE_OCTAHEDRON:
            cf_draw3d_mesh(scene3d->octahedron);
            break;
        case GAME_PRIMITIVE_TETRAHEDRON:
            cf_draw3d_mesh(scene3d->tetrahedron);
            break;
        case GAME_PRIMITIVE_CHEVRON_PYRAMID:
        case GAME_PRIMITIVE_BOX_FRAME:
        case GAME_PRIMITIVE_TRIANGLE_OUTLINE:
        case GAME_PRIMITIVE_TRIANGLE:
            cf_draw3d_mesh(scene3d->polygons[shape->primitive - GAME_PRIMITIVE_CHEVRON_PYRAMID]);
            break;
    }
}

static void append_draw(
    draw_buffer *buffer,
    scene3d_draw draw,
    const game_material_coefficients *color,
    const float parameters[SHADER_PARAMETER_MAX][4],
    const render_scene3d_camera *camera
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
    const render_scene3d_camera *camera,
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
    scene3d_visual visuals[SCENE3D_PART_LIMIT];
    char error[256] = { 0 };
    int count = render_scene3d_visuals(world, entity, &position, visuals, error, sizeof error);
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
        const scene3d_visual *visual = &visuals[i];
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
        if (!render_scene3d_visual_visible(visual, view_projection)) {
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
        scene3d_draw draw = { .frame = visual->frame,
                              .shape = { .primitive = visual->part.primitive,
                                         .width = visual->part.dimensions.x,
                                         .height = visual->part.dimensions.y,
                                         .depth = visual->part.dimensions.z },
                              .material = id };
        append_draw(buffer, draw, &color, parameters, camera);
    }
}

static void collect_particles(
    render_scene3d *scene3d,
    const game_particle *items,
    int count,
    const render_scene3d_camera *camera
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
        scene3d_draw draw = {
            .frame = frame,
            .particle_shape = particle->shape + 1,
            .particle_age = age,
            .shape = { .width = radius * 2 * stretch, .height = radius * 2, .depth = 1 },
        };
        append_draw(&scene3d->buffer, draw, &color, NULL, camera);
    }
}

static void collect_lights(
    render_scene3d *scene3d,
    ecs_world *world,
    const render_scene3d_camera *camera,
    const engine *runtime
)
{
    scene3d->light_count = 0;
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
        int slot = scene3d->light_count;
        candidates++;
        if (slot == POINT_LIGHT_LIMIT) {
            slot--;
            if (score <= scene3d->light_scores[slot]) {
                continue;
            }
        } else {
            scene3d->light_count++;
        }
        while (slot > 0 && score > scene3d->light_scores[slot - 1]) {
            scene3d->light_scores[slot] = scene3d->light_scores[slot - 1];
            scene3d->light_positions[slot] = scene3d->light_positions[slot - 1];
            scene3d->light_colors[slot] = scene3d->light_colors[slot - 1];
            slot--;
        }
        scene3d->light_scores[slot] = score;
        scene3d->light_positions[slot] = (CF_V4){ position.x,
                                                  light->elevation,
                                                  position.y,
                                                  light->radius };
        /* Packed component colors use the same linear channel convention as Material. */
        scene3d->light_colors[slot] = (CF_V4){ ((light->color >> 24) & 255u) / 255.0f,
                                               ((light->color >> 16) & 255u) / 255.0f,
                                               ((light->color >> 8) & 255u) / 255.0f,
                                               light->intensity };
    }
    ecs_query_end(&query);
    int dropped = candidates - scene3d->light_count;
    if ((dropped > 0) != (scene3d->lights_dropped > 0)) {
        fprintf(
            stderr,
            "3D point lights: %d active, %d omitted by the %d-light camera "
            "influence budget.\n",
            scene3d->light_count,
            dropped,
            POINT_LIGHT_LIMIT
        );
    }
    scene3d->lights_dropped = dropped;
}

static CF_V4 radiance(unsigned color, float intensity)
{
    return (CF_V4){ ((color >> 24) & 255u) / 255.0f * intensity,
                    ((color >> 16) & 255u) / 255.0f * intensity,
                    ((color >> 8) & 255u) / 255.0f * intensity,
                    0 };
}

static void set_sun(render_scene3d *scene3d, const GameDirectionalLight *light)
{
    CF_V3 direction = light->direction;
    float length = cf_len_v3(direction);
    scene3d->sun_color = (CF_V4){ 0 };
    scene3d->sun_shadows = false;
    if (!isfinite(length) || length <= 0 || !isfinite(light->intensity) ||
        light->intensity <= 0) {
        return;
    }
    direction = cf_div_v3_f(direction, -length);
    scene3d->sun_direction = (CF_V4){ direction.x, direction.y, direction.z, 0 };
    scene3d->sun_color = radiance(light->color, light->intensity);
    scene3d->sun_shadows = light->shadows;
}

static void set_ambient(render_scene3d *scene3d, const GameEnvironment *environment)
{
    float intensity = isfinite(environment->ambient_intensity)
                        ? fmaxf(environment->ambient_intensity, 0)
                        : 0;
    scene3d->ambient_sky = radiance(environment->ambient_sky, intensity);
    scene3d->ambient_ground = radiance(environment->ambient_ground, intensity);
}

/* At most one visible directional light shades the scene. */
static bool collect_sun(render_scene3d *scene3d, ecs_world *world, const engine *runtime)
{
    scene3d->sun_color = (CF_V4){ 0 };
    scene3d->sun_shadows = false;
    ecs_component_id required[] = { GAME_COMPONENT_GameDirectionalLight.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 1) != ECS_OK) {
        return true;
    }
    int count = 0;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        if (count++) {
            break;
        }
        if (engine_entity_visible(runtime, world, entity)) {
            set_sun(scene3d, GAME_GameDirectionalLight_get_const(world, entity));
        }
    }
    ecs_query_end(&query);
    if (count > 1) {
        snprintf(scene3d->binding_error, sizeof scene3d->binding_error, "A 3D scene can have only one Directional Light.");
        return false;
    }
    return true;
}

static int compare_draws(const void *left, const void *right)
{
    const scene3d_draw *a = left, *b = right;
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

/* Fits the sun's shadow map around every solid the camera may see. */
static bool fit_shadow(render_scene3d *scene3d, const render_scene3d_camera *camera)
{
    if (!scene3d->sun_shadows) {
        return false;
    }
    aclear(scene3d->shadow_boxes);
    for (int i = 0; i < acount(scene3d->buffer.draws); i++) {
        const scene3d_draw *draw = &scene3d->buffer.draws[i];
        if (!draw->particle_shape) {
            CF_V3 scale = { draw->shape.width, draw->shape.height, draw->shape.depth };
            apush(scene3d->shadow_boxes, cf_mul_m4(draw->frame, cf_m4_scale(scale)));
        }
    }
    CF_V3 toward = { scene3d->sun_direction.x,
                     scene3d->sun_direction.y,
                     scene3d->sun_direction.z };
    return render_scene3d_shadow_fit(
        scene3d->shadow,
        toward,
        scene3d->shadow_boxes,
        acount(scene3d->shadow_boxes),
        camera
    );
}

/* Opaque solids, which sort first, write their depth from the sun. */
static bool draw_shadow(render_scene3d *scene3d)
{
    if (!render_scene3d_shadow_begin(
            scene3d->shadow,
            scene3d->binding_error,
            sizeof scene3d->binding_error
        )) {
        return false;
    }
    for (int i = 0; i < acount(scene3d->buffer.draws); i++) {
        const scene3d_draw *draw = &scene3d->buffer.draws[i];
        if (draw->transparent) {
            break;
        }
        cf_draw3d_push();
        cf_draw3d_transform(draw->frame);
        draw_shape(scene3d, &draw->shape);
        cf_draw3d_pop();
    }
    render_scene3d_shadow_end(scene3d->shadow);
    return true;
}

static bool submit(render_scene3d *scene3d, const render_scene3d_camera *camera, float time)
{
    if (!acount(scene3d->buffer.draws)) {
        render_scene3d_trails_draw(scene3d->trails, camera);
        return true;
    }
    for (int i = 0; i < acount(scene3d->buffer.draws); i++) {
        scene3d_draw *draw = &scene3d->buffer.draws[i];
        render_shader_set set = { 0 };
        shader_error error = { 0 };
        if (!render_shader_set_add(&set, draw->material, &error)) {
            snprintf(
                scene3d->binding_error,
                sizeof scene3d->binding_error,
                "%s",
                error.message
            );
            return false;
        }
        draw->program = render_programs_prepare(scene3d->programs, &set);
        if (render_programs_error(scene3d->programs)[0]) {
            snprintf(
                scene3d->binding_error,
                sizeof scene3d->binding_error,
                "%s",
                render_programs_error(scene3d->programs)
            );
        }
        if (!draw->program) {
            return false;
        }
    }
    int cells = acount(scene3d->buffer.pixels);
    int height = 1;
    while (height * PARAMETER_TEXTURE_WIDTH < cells) {
        height *= 2;
    }
    if (height > 2048) {
        snprintf(scene3d->binding_error, sizeof scene3d->binding_error, "3D material instance capacity exceeded.");
        return false;
    }
    if (height > scene3d->texture_height) {
        if (scene3d->parameters.id) {
            cf_destroy_texture(scene3d->parameters);
        }
        CF_TextureParams params = cf_texture_defaults(PARAMETER_TEXTURE_WIDTH, height);
        params.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
        params.filter = CF_FILTER_NEAREST;
        params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT;
        params.stream = true;
        scene3d->parameters = cf_make_texture(params);
        scene3d->texture_height = height;
    }
    asetlen(scene3d->buffer.pixels, scene3d->texture_height * PARAMETER_TEXTURE_WIDTH);
    memset(
        scene3d->buffer.pixels + cells,
        0,
        (size_t)(acount(scene3d->buffer.pixels) - cells) *
            sizeof *scene3d->buffer.pixels
    );
    cf_texture_update(
        scene3d->parameters,
        scene3d->buffer.pixels,
        acount(scene3d->buffer.pixels) * (int)sizeof *scene3d->buffer.pixels
    );
    qsort(
        scene3d->buffer.draws,
        (size_t)acount(scene3d->buffer.draws),
        sizeof *scene3d->buffer.draws,
        compare_draws
    );
    bool shadowed = fit_shadow(scene3d, camera);
    if (shadowed && !draw_shadow(scene3d)) {
        return false;
    }
    CF_M4x4 inverse = cf_m4_invert(camera->view);
    CF_V4 eye = { inverse.elements[12], inverse.elements[13], inverse.elements[14], time };
    cf_draw3d_set_uniform("scene_camera_time", &eye, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_rhythm", &scene3d->rhythm, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_sun_direction", &scene3d->sun_direction, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_sun_color", &scene3d->sun_color, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_ambient_sky", &scene3d->ambient_sky, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_ambient_ground", &scene3d->ambient_ground, CF_UNIFORM_TYPE_FLOAT4, 1);
    CF_V4 light_count = { .x = (float)scene3d->light_count };
    cf_draw3d_set_uniform("scene_light_count", &light_count, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_light_positions", scene3d->light_positions, CF_UNIFORM_TYPE_FLOAT4, POINT_LIGHT_LIMIT);
    cf_draw3d_set_uniform("scene_light_colors", scene3d->light_colors, CF_UNIFORM_TYPE_FLOAT4, POINT_LIGHT_LIMIT);
    render_influences_bind(scene3d->influences);
    render_scene3d_shadow_bind(scene3d->shadow, shadowed);
    cf_draw3d_set_texture("scene_parameters", scene3d->parameters);
    cf_draw3d_push_projection(camera->projection);
    cf_draw3d_push_view(camera->view);
    int program = 0;
    CF_RenderState state = cf_render_state_3d_defaults();
    cf_draw3d_push_render_state(state);
    bool transparent = false;
    for (int i = 0; i < acount(scene3d->buffer.draws); i++) {
        const scene3d_draw *draw = &scene3d->buffer.draws[i];
        if (program != draw->program) {
            if (program) {
                render_programs_unbind3d(scene3d->programs, program);
            }
            program = draw->program;
            render_programs_bind3d(scene3d->programs, program);
        }
        if (draw->transparent && !transparent) {
            render_scene3d_trails_draw(scene3d->trails, camera);
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
            cf_draw3d_mesh(scene3d->particle_quad);
        } else {
            draw_shape(scene3d, &draw->shape);
        }
        cf_draw3d_pop();
        cf_draw3d_pop_mesh_attributes2();
        cf_draw3d_pop_mesh_attributes();
    }
    cf_draw3d_pop_render_state();
    render_programs_unbind3d(scene3d->programs, program);
    /* Later meshes would otherwise keep the parameters bound past their resize or free. */
    cf_draw3d_set_texture("scene_parameters", (CF_Texture){ 0 });
    render_scene3d_shadow_unbind();
    cf_draw3d_pop_view();
    cf_draw3d_pop_projection();
    if (!transparent) {
        render_scene3d_trails_draw(scene3d->trails, camera);
    }
    return true;
}

enum
{
    DRAW_CHUNK = 512
};
typedef struct
{
    render_scene3d *scene3d;
    ecs_world *world;
    const render_scene3d_camera *camera;
    const engine *runtime;
} collect_context;
static void collect_range(int start, int end, uint32_t worker, void *user)
{
    collect_context *ctx = user;
    render_scene3d *scene3d = ctx->scene3d;
    for (int c = start; c < end; c++) {
        draw_buffer *buffer = &scene3d->chunks[c];
        aclear(buffer->pixels);
        aclear(buffer->draws);
        buffer->binding_error[0] = 0;
        buffer->worker = worker;
        int limit = (c + 1) * DRAW_CHUNK;
        if (limit > acount(scene3d->entities)) {
            limit = acount(scene3d->entities);
        }
        for (int i = c * DRAW_CHUNK; i < limit; i++) {
            collect_entity(buffer, ctx->world, scene3d->entities[i], ctx->camera, ctx->runtime);
        }
    }
}
static void collect_geometry(
    render_scene3d *scene3d,
    ecs_world *world,
    const render_scene3d_camera *camera,
    const engine *runtime,
    render_skip skip
)
{
    aclear(scene3d->entities);
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameGeometry3D.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            if (!render_skip_has(skip, entity)) {
                apush(scene3d->entities, entity);
            }
        }
        ecs_query_end(&query);
    }
    int count = acount(scene3d->entities);
    scene3d->work = (render_scene3d_work){ .entities = count, .workers = 1 };
    const game_task_system *tasks = game_task_pool_tasks(scene3d->pool);
    if (scene3d->parallel_disabled || count < DRAW_CHUNK * 2 || tasks->workers == 1) {
        scene3d->buffer.binding_error[0] = 0;
        for (int i = 0; i < count; i++) {
            collect_entity(&scene3d->buffer, world, scene3d->entities[i], camera, runtime);
        }
        snprintf(
            scene3d->binding_error,
            sizeof scene3d->binding_error,
            "%s",
            scene3d->buffer.binding_error
        );
        return;
    }
    int chunks = (count + DRAW_CHUNK - 1) / DRAW_CHUNK;
    while (acount(scene3d->chunks) < chunks) {
        apush(scene3d->chunks, ((draw_buffer){ 0 }));
    }
    collect_context ctx = { scene3d, world, camera, runtime };
    game_task_pool_begin(scene3d->pool, true);
    void *task = tasks->enqueue(collect_range, chunks, 1, &ctx, tasks->user);
    tasks->finish(task, tasks->user);
    game_task_pool_end(scene3d->pool);
    unsigned workers = 0;
    for (int c = 0; c < chunks; c++) {
        draw_buffer *buffer = &scene3d->chunks[c];
        workers |= 1u << buffer->worker;
        if (buffer->binding_error[0] && !scene3d->binding_error[0]) {
            snprintf(
                scene3d->binding_error,
                sizeof scene3d->binding_error,
                "%s",
                buffer->binding_error
            );
        }
        int rows = acount(scene3d->buffer.draws),
            pixels = acount(scene3d->buffer.pixels);
        for (int i = 0; i < acount(buffer->draws); i++) {
            scene3d_draw draw = buffer->draws[i];
            draw.row += rows;
            apush(scene3d->buffer.draws, draw);
        }
        int size = acount(buffer->pixels);
        if (size) {
            asetlen(scene3d->buffer.pixels, pixels + size);
            memcpy(
                scene3d->buffer.pixels + pixels,
                buffer->pixels,
                (size_t)size * sizeof *buffer->pixels
            );
        }
    }
    scene3d->work.tasks = chunks;
    scene3d->work.workers = __builtin_popcount(workers);
}
void render_scene3d_set_parallel(render_scene3d *scene3d, bool enabled)
{
    scene3d->parallel_disabled = !enabled;
}
render_scene3d_work render_scene3d_last_work(const render_scene3d *scene3d)
{
    return scene3d->work;
}

bool render_scene3d_draw(
    render_scene3d *scene3d,
    ecs_world *world,
    const game_particles *particles,
    const render_scene3d_camera *camera,
    float time,
    const struct engine *runtime,
    const GameEnvironment *environment,
    render_skip skip
)
{
    scene3d->simulation_particles = game_particles_count(particles);
    scene3d->binding_error[0] = 0;
    aclear(scene3d->buffer.pixels);
    aclear(scene3d->buffer.draws);
    /* Rhythm is an optional material input; without a valid clock it rests at zero. */
    game_beat beat = { 0 };
    game_rhythm_sample(world, time, &beat);
    scene3d->rhythm = (CF_V4){ (float)beat.phase,
                               (float)beat.pulse,
                               (float)beat.period,
                               (float)beat.duration };
    set_ambient(scene3d, environment);
    if (!collect_sun(scene3d, world, runtime)) {
        return false;
    }
    collect_lights(scene3d, world, camera, runtime);
    render_influences_collect(scene3d->influences, world, runtime, time);
    render_scene3d_trails_collect(scene3d->trails, world, runtime, time);
    if (!runtime && !game_particle_emitters_collect(
                        scene3d->emitters,
                        world,
                        runtime,
                        time,
                        scene3d->binding_error,
                        sizeof scene3d->binding_error
                    )) {
        return false;
    }
    collect_geometry(scene3d, world, camera, runtime, skip);
    collect_particles(scene3d, game_particles_data(particles), game_particles_count(particles), camera);
    if (!runtime) {
        const game_particles *emitted = game_particle_emitters_particles(scene3d->emitters);
        collect_particles(scene3d, game_particles_data(emitted), game_particles_count(emitted), camera);
        int head_count;
        const game_particle *heads = game_particle_emitters_heads(scene3d->emitters, &head_count);
        collect_particles(scene3d, heads, head_count, camera);
    }
    if (scene3d->binding_error[0]) {
        return false;
    }
    if (!render_scene3d_trails_prepare(
            scene3d->trails,
            camera,
            scene3d->binding_error,
            sizeof scene3d->binding_error
        )) {
        return false;
    }
    return submit(scene3d, camera, time);
}

void render_scene3d_set_error(render_scene3d *scene3d, const char *message)
{
    snprintf(scene3d->binding_error, sizeof scene3d->binding_error, "%s", message);
}

bool render_scene3d_preview_material(
    render_scene3d *scene3d,
    int material,
    const render_scene3d_camera *camera,
    const char *parameters_json,
    float time,
    const render_scene3d_activity *activity
)
{
    scene3d->simulation_particles = 0;
    game_particle_emitters_reset(scene3d->emitters);
    scene3d->binding_error[0] = 0;
    aclear(scene3d->buffer.pixels);
    aclear(scene3d->buffer.draws);
    scene3d->light_count = 0;
    scene3d->rhythm = (CF_V4){ .x = activity->beat_phase, .y = activity->beat_pulse };
    GameDirectionalLight sun = game_directional_light_defaults();
    sun.shadows = false;
    GameEnvironment environment = game_environment_defaults();
    set_sun(scene3d, &sun);
    set_ambient(scene3d, &environment);
    render_scene3d_reset(scene3d);
    render_influences_preview(scene3d->influences, activity->influence, RENDER_SCENE3D_PREVIEW_RADIUS);
    if (!material || render_shader_is_shape(material)) {
        return false;
    }
    float parameters[SHADER_PARAMETER_MAX][4];
    render_shader_parameters(material, NULL, (ecs_entity){ 0 }, parameters_json, parameters);
    game_material_coefficients color = { .scene = { 1, 1, 1, 1 } };
    scene3d_draw draw = { .frame = cf_m4_identity(),
                          .shape = { .primitive = GAME_PRIMITIVE_SPHERE,
                                     .width = RENDER_SCENE3D_PREVIEW_RADIUS * 2,
                                     .height = RENDER_SCENE3D_PREVIEW_RADIUS * 2,
                                     .depth = RENDER_SCENE3D_PREVIEW_RADIUS * 2 },
                          .material = material };
    append_draw(&scene3d->buffer, draw, &color, parameters, camera);
    return submit(scene3d, camera, time);
}
