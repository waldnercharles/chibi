#include "render/scene3d.h"

#include "platform/cute.h"
#include "render/material.h"
#include "render/scene3d_geometry.h"
#include "render/scene3d_influence.h"
#include "render/scene3d_light_shadow.h"
#include "render/scene3d_mesh_cache.h"
#include "render/scene3d_shadow.h"
#include "render/scene3d_sky.h"
#include "render/scene3d_trail.h"
#include "render/scene3d_visual.h"
#include "render/shader_catalog.h"
#include "render/shader_program.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "runtime/particle_emitters.h"
#include "runtime/rhythm.h"
#include "runtime/task_pool.h"
#include "runtime/transform_propagation.h"
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
    MATERIAL_TEXELS = 39,
    LIGHT_LIMIT = 32
};

/* A point or spot light kept for the lit pass. */
typedef struct
{
    CF_V4 position; /* World center and radius. */
    CF_V4 color;    /* Linear color and intensity. */
    /* The cone's axis over its fade width, and its outer edge's offset, so
     * that the lit pass's falloff is dot(direction, axis) + offset. A point
     * light's has no axis and an offset of one. */
    CF_V4 cone;
    float score; /* Estimated influence on the camera. */
    bool shadows;
} scene3d_light;

/* A built-in shape, whose frame excludes its dimensions, or a run of an
 * imported mesh's triangles, whose frame places the mesh's own coordinates. */
typedef struct
{
    CF_M4x4 frame;
    GameGeometry3D shape;
    const render_mesh_asset *mesh;
    int first, count; /* The mesh's indices. */
} scene3d_solid;

typedef struct
{
    scene3d_solid solid;
    float depth;
    int material, row, program;
    /* A masked draw's program in shadow passes, which drops what it cuts. */
    int shadow_program;
    int particle_shape;
    float particle_age;
    bool transparent, masked;
} scene3d_draw;

typedef struct
{
    CF_V4 *pixels;
    scene3d_draw *draws;
    /* Opaque solids outside the view, which may still cast a shadow into it,
     * with a material row only when masked. Kept only while a light casts
     * shadows. */
    scene3d_draw *casters;
    render_shader_parameter_cache *cache;
    char binding_error[256];
    unsigned worker;
    bool shadows;
} draw_buffer;

struct render_scene3d
{
    render_programs *programs, *mesh_programs;
    render_programs *shadow_programs, *mesh_shadow_programs;
    render_scene3d_mesh_cache *meshes;
    render_influences *influences;
    scene3d_trails *trails;
    render_scene3d_sky *sky;
    game_particle_emitters *emitters;
    CF_Mesh prism, octahedron, tetrahedron;
    CF_Mesh particle_quad;
    CF_Mesh polygons[4];
    CF_V4 rhythm;
    /* Direction toward the sun, sun radiance, and hemisphere ambient radiance. */
    CF_V4 sun_direction, sun_color, ambient_sky, ambient_ground;
    bool sun_shadows;
    float sun_spread; /* Tangent of half the sun's angular diameter. */
    render_scene3d_shadow *shadow;
    render_scene3d_light_shadow *light_shadow;
    CF_M4x4 *shadow_boxes;
    int *near_boxes; /* Boxes that can cast into the current light. */
    CF_Texture parameters;
    int texture_height;
    draw_buffer buffer, *chunks;
    ecs_entity *entities;
    game_task_pool *pool;
    bool parallel_disabled;
    render_scene3d_work work;
    scene3d_light lights[LIGHT_LIMIT];
    /* The kept lights as the lit pass reads them. */
    CF_V4 light_positions[LIGHT_LIMIT], light_colors[LIGHT_LIMIT],
        light_cones[LIGHT_LIMIT];
    int light_count, lights_dropped;
    int shadowed_lights; /* The first lights, which cast shadows. */
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
    scene3d->mesh_programs = render_programs_new(RENDER_PROGRAM_SCENE3D_MESH);
    scene3d->shadow_programs = render_programs_new(RENDER_PROGRAM_SCENE3D_SHADOW);
    scene3d->mesh_shadow_programs = render_programs_new(RENDER_PROGRAM_SCENE3D_MESH_SHADOW);
    scene3d->meshes = render_scene3d_mesh_cache_new();
    scene3d->influences = render_influences_new();
    scene3d->shadow = render_scene3d_shadow_new();
    scene3d->light_shadow = render_scene3d_light_shadow_new();
    scene3d->trails = render_scene3d_trails_new();
    scene3d->sky = render_scene3d_sky_new();
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
    render_programs_free(scene3d->mesh_programs);
    render_programs_free(scene3d->shadow_programs);
    render_programs_free(scene3d->mesh_shadow_programs);
    render_scene3d_mesh_cache_free(scene3d->meshes);
    render_influences_free(scene3d->influences);
    render_scene3d_shadow_free(scene3d->shadow);
    render_scene3d_light_shadow_free(scene3d->light_shadow);
    afree(scene3d->shadow_boxes);
    afree(scene3d->near_boxes);
    render_scene3d_trails_free(scene3d->trails);
    render_scene3d_sky_free(scene3d->sky);
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
        afree(scene3d->chunks[i].casters);
        render_shader_parameter_cache_free(scene3d->chunks[i].cache);
    }
    afree(scene3d->chunks);
    afree(scene3d->entities);
    render_shader_parameter_cache_free(scene3d->buffer.cache);
    afree(scene3d->buffer.pixels);
    afree(scene3d->buffer.draws);
    afree(scene3d->buffer.casters);
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

/* The box around a solid, as a transform of the unit cube: a mesh's bounds,
 * or a shape's, whose triangles reach 2/3 along x. */
static CF_M4x4 solid_box(const scene3d_solid *solid)
{
    if (solid->mesh) {
        return render_scene3d_mesh_box(solid->mesh, solid->frame);
    }
    const GameGeometry3D *shape = &solid->shape;
    CF_V3 size = { shape->width, shape->height, shape->depth };
    if (shape->primitive == GAME_PRIMITIVE_TRIANGLE ||
        shape->primitive == GAME_PRIMITIVE_TRIANGLE_OUTLINE) {
        size.x *= 4.0f / 3;
    }
    return cf_mul_m4(solid->frame, cf_m4_scale(size));
}

static bool transparent_surface(int material, const game_material_coefficients *color)
{
    return material ? render_shader_may_be_transparent(material)
                    : color->scene[3] < 1;
}

/* Whether a light casts shadows that a solid with this material darkens. */
static bool casts(const draw_buffer *buffer, int material, const game_material_coefficients *color)
{
    return buffer->shadows && !transparent_surface(material, color);
}

/* Whether a material keeps or drops each pixel by its Alpha Cutoff. */
static bool masked_material(int material)
{
    return material && render_shader_alpha_mode(material) == SHADER_ALPHA_MASK;
}

/* Appends a draw's material row: its colors, parameters, inverse frame and
 * texture coordinate bounds. Returns the row's index. */
static int append_row(
    draw_buffer *buffer,
    const scene3d_draw *draw,
    const game_material_coefficients *color,
    const float parameters[SHADER_PARAMETER_MAX][4]
)
{
    int start = acount(buffer->pixels);
    asetlen(buffer->pixels, start + MATERIAL_TEXELS);
    CF_V4 *row = &buffer->pixels[start];
    memset(row, 0, MATERIAL_TEXELS * sizeof *row);
    memcpy(&row[0], color->scene, sizeof *row);
    memcpy(&row[1], color->emission, sizeof *row);
    if (parameters) {
        memcpy(&row[2], parameters, SHADER_PARAMETER_MAX * sizeof *row);
    }
    if (draw->particle_shape) {
        row[2].x = draw->particle_age;
    }
    const scene3d_solid *solid = &draw->solid;
    CF_M4x4 inverse = cf_m4_invert(solid->frame);
    for (int i = 0; i < 3; i++) {
        row[34 + i] = (CF_V4){ inverse.elements[i],
                               inverse.elements[4 + i],
                               inverse.elements[8 + i],
                               inverse.elements[12 + i] };
    }
    const render_mesh_asset *mesh = solid->mesh;
    if (mesh) {
        /* A mesh with source bounds projects its texture coordinates from them. */
        row[37] = (CF_V4){ mesh->uv_min.x, mesh->uv_min.y, mesh->uv_min.z, mesh->has_uv_bounds };
        row[38] = (CF_V4){ mesh->uv_max.x, mesh->uv_max.y, mesh->uv_max.z, 0 };
    } else {
        row[37] = (CF_V4){ solid->shape.width, solid->shape.height, solid->shape.depth, 0 };
    }
    return start / MATERIAL_TEXELS;
}

static void append_draw(
    draw_buffer *buffer,
    scene3d_draw draw,
    const game_material_coefficients *color,
    const float parameters[SHADER_PARAMETER_MAX][4],
    const render_scene3d_camera *camera
)
{
    draw.row = append_row(buffer, &draw, color, parameters);
    draw.transparent = draw.particle_shape ||
                       transparent_surface(draw.material, color);
    draw.masked = masked_material(draw.material);
    /* A mesh's vertices may lie away from its origin, so depth is its box's center. */
    CF_M4x4 box = solid_box(&draw.solid);
    CF_V4 center = { box.elements[12], box.elements[13], box.elements[14], 1 };
    draw.depth = -cf_mul_m4_v4(camera->view, center).z;
    apush(buffer->draws, draw);
}

/* Keeps a masked solid outside the view that may cast a shadow into it, with
 * the material row its shadow passes read. */
static void append_masked_caster(
    draw_buffer *buffer,
    scene3d_draw caster,
    const game_material_coefficients *color,
    const float parameters[SHADER_PARAMETER_MAX][4]
)
{
    caster.masked = true;
    caster.row = append_row(buffer, &caster, color, parameters);
    apush(buffer->casters, caster);
}

/* The surface material a visual draws with, zero for its color alone, or
 * false with a binding error for a name that is no surface material. */
static bool surface_material(draw_buffer *buffer, const char *name, int *id)
{
    *id = render_shader_find(name);
    if (!name || !*name || (*id && !render_shader_is_shape(*id))) {
        return true;
    }
    if (!buffer->binding_error[0]) {
        snprintf(buffer->binding_error, sizeof buffer->binding_error, "Unknown surface material: %s", name);
    }
    return false;
}

/* An imported mesh's material groups, each with its own material or else the
 * entity's, or the whole mesh with the entity's. */
static void collect_mesh(
    draw_buffer *buffer,
    ecs_world *world,
    ecs_entity entity,
    const scene3d_solid *whole,
    const game_material_coefficients *color,
    const render_scene3d_camera *camera
)
{
    const render_mesh_asset *mesh = whole->mesh;
    const GameShader *binding = GAME_GameShader_get_const(world, entity);
    CF_M4x4 view_projection = cf_mul_m4(camera->projection, camera->view);
    bool visible = render_scene3d_box_visible(
        render_scene3d_mesh_box(mesh, whole->frame),
        view_projection
    );
    int groups = acount(mesh->groups);
    for (int i = 0; i < (groups ? groups : 1); i++) {
        const render_mesh_group *group = groups ? &mesh->groups[i] : NULL;
        scene3d_solid solid = *whole;
        const char *name = binding ? binding->material : NULL;
        if (group) {
            solid.first = group->first;
            solid.count = group->count;
            name = group->material[0] ? group->material : name;
        }
        int id;
        if (!surface_material(buffer, name, &id)) {
            return;
        }
        scene3d_draw draw = { .solid = solid, .material = id };
        if (!visible && !casts(buffer, id, color)) {
            continue;
        }
        if (!visible && !masked_material(id)) {
            apush(buffer->casters, draw);
            continue;
        }
        float parameters[SHADER_PARAMETER_MAX][4];
        shader_error error = { 0 };
        if (!render_shader_material_parameters_scoped_cached(
                id,
                world,
                entity,
                binding ? binding->parameters : NULL,
                group ? group->material_scope : NULL,
                parameters,
                &buffer->cache,
                &error
            )) {
            if (!buffer->binding_error[0]) {
                snprintf(
                    buffer->binding_error,
                    sizeof buffer->binding_error,
                    "%s",
                    error.message
                );
            }
            return;
        }
        if (!visible) {
            append_masked_caster(buffer, draw, color, parameters);
            continue;
        }
        append_draw(buffer, draw, color, parameters, camera);
    }
}

static void collect_entity(
    draw_buffer *buffer,
    ecs_world *world,
    ecs_entity entity,
    const render_scene3d_camera *camera,
    const engine *runtime
)
{
    if (!engine_entity_visible(runtime, world, entity)) {
        return;
    }
    GameTransform position = engine_render_transform(runtime, world, entity).position;
    scene3d_visual visuals[SCENE3D_PART_LIMIT];
    const render_mesh_asset *mesh;
    char error[256] = { 0 };
    int count = render_scene3d_visuals(world, entity, &position, visuals, &mesh, error, sizeof error);
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
    if (mesh) {
        scene3d_solid whole = { .frame = render_scene3d_visual_model(&visuals[0]),
                                .mesh = mesh,
                                .count = acount(mesh->indices) };
        collect_mesh(buffer, world, entity, &whole, &color, camera);
        return;
    }
    const GameShader *binding = GAME_GameShader_get_const(world, entity);
    CF_M4x4 view_projection = cf_mul_m4(camera->projection, camera->view);
    for (int i = 0; i < count; i++) {
        const scene3d_visual *visual = &visuals[i];
        const char *name = visual->part.material[0] ? visual->part.material
                         : binding                  ? binding->material
                                                    : NULL;
        int id;
        if (!surface_material(buffer, name, &id)) {
            return;
        }
        scene3d_solid solid = { .frame = visual->frame,
                                .shape = { .primitive = visual->part.primitive,
                                           .width = visual->part.dimensions.x,
                                           .height = visual->part.dimensions.y,
                                           .depth = visual->part.dimensions.z } };
        scene3d_draw draw = { .solid = solid, .material = id };
        bool visible = render_scene3d_visual_visible(visual, view_projection);
        if (!visible && !casts(buffer, id, &color)) {
            continue;
        }
        if (!visible && !masked_material(id)) {
            apush(buffer->casters, draw);
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
        if (!visible) {
            append_masked_caster(buffer, draw, &color, parameters);
            continue;
        }
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
            .solid = { .frame = frame,
                       .shape = { .width = radius * 2 * stretch,
                                  .height = radius * 2,
                                  .depth = 1 } },
            .particle_shape = particle->shape + 1,
            .particle_age = age,
        };
        append_draw(&scene3d->buffer, draw, &color, NULL, camera);
    }
}

/* Moves the highest-ranked lights that cast shadows, up to the shadow limit,
 * ahead of the rest, which keep their order, and lays them all out for the
 * lit pass. */
static void order_lights(render_scene3d *scene3d)
{
    scene3d_light *lights = scene3d->lights;
    int shadowed = 0;
    for (int i = 0;
         i < scene3d->light_count && shadowed < RENDER_SCENE3D_LIGHT_SHADOW_LIMIT;
         i++) {
        if (!lights[i].shadows) {
            continue;
        }
        scene3d_light light = lights[i];
        memmove(&lights[shadowed + 1], &lights[shadowed], (size_t)(i - shadowed) * sizeof light);
        lights[shadowed++] = light;
    }
    scene3d->shadowed_lights = shadowed;
    for (int i = 0; i < scene3d->light_count; i++) {
        scene3d->light_positions[i] = lights[i].position;
        scene3d->light_colors[i] = lights[i].color;
        scene3d->light_cones[i] = lights[i].cone;
    }
}

/* Where a light entity shines from this frame, or false when it is hidden. */
static bool place_light(ecs_world *world, const engine *runtime, ecs_entity entity, GameTransform *position)
{
    if (!engine_entity_visible(runtime, world, entity)) {
        return false;
    }
    *position = engine_render_transform(runtime, world, entity).position;
    return true;
}

/* Keeps a light, a point light's fields with a cone, among the lights with the
 * most estimated influence on the camera, if it lights anything. Returns
 * whether it counted. */
static bool rank_light(
    render_scene3d *scene3d,
    CF_V3 eye,
    GameTransform position,
    const GamePointLight *light,
    CF_V4 cone
)
{
    if (!isfinite(position.x) || !isfinite(position.y) || !isfinite(light->intensity) ||
        !isfinite(light->radius) || !isfinite(light->elevation) ||
        light->intensity <= 0 || light->radius <= 0 || !(light->color >> 8)) {
        return false;
    }
    double dx = position.x - (double)eye.x,
           dy = light->elevation - (double)eye.y, dz = position.y - (double)eye.z,
           radius2 = (double)light->radius * light->radius;
    /* Packed component colors use the same linear channel convention as Material. */
    CF_V4 color = { ((light->color >> 24) & 255u) / 255.0f,
                    ((light->color >> 16) & 255u) / 255.0f,
                    ((light->color >> 8) & 255u) / 255.0f,
                    light->intensity };
    float peak = fmaxf(color.x, fmaxf(color.y, color.z));
    scene3d_light kept = {
        .position = { position.x, light->elevation, position.y, light->radius },
        .color = color,
        .cone = cone,
        .score = (float)(light->intensity * peak * radius2 /
                         (dx * dx + dy * dy + dz * dz + radius2)),
        .shadows = light->shadows,
    };
    int slot = scene3d->light_count;
    if (slot == LIGHT_LIMIT) {
        slot--;
        if (kept.score <= scene3d->lights[slot].score) {
            return true;
        }
    } else {
        scene3d->light_count++;
    }
    for (; slot > 0 && kept.score > scene3d->lights[slot - 1].score; slot--) {
        scene3d->lights[slot] = scene3d->lights[slot - 1];
    }
    scene3d->lights[slot] = kept;
    return true;
}

static void collect_point_lights(
    render_scene3d *scene3d,
    ecs_world *world,
    CF_V3 eye,
    const engine *runtime,
    int *candidates
)
{
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GamePointLight.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) != ECS_OK) {
        return;
    }
    ecs_entity entity;
    GameTransform position;
    while (ecs_query_next(&query, &entity)) {
        *candidates += place_light(world, runtime, entity, &position) &&
                       rank_light(
                           scene3d,
                           eye,
                           position,
                           GAME_GamePointLight_get_const(world, entity),
                           (CF_V4){ .w = 1 }
                       );
    }
    ecs_query_end(&query);
}

/* The narrowest fade across a spot light's edge, in cosine, which keeps a
 * hard edge's width finite when its angles are equal. */
static const float hardest_fade = 1e-4f;
/* Spot Light outer angles in degrees. A narrower cone would leave even its
 * axis inside the hardest fade, and a wider one would need a shadow face past
 * 180 degrees. */
static const float narrowest_spot = 2, widest_spot = 160;

/* A spot light's cone as the lit pass reads it, or false with a binding error
 * when its angles are out of range. A cone with no direction lights nothing. */
static bool spot_cone(render_scene3d *scene3d, const GameSpotLight *spot, CF_V4 *cone)
{
    if (!(spot->outer_angle >= narrowest_spot && spot->outer_angle <= widest_spot)) {
        snprintf(
            scene3d->binding_error,
            sizeof scene3d->binding_error,
            "Spot Light outer angle must be %g to %g degrees.",
            narrowest_spot,
            widest_spot
        );
        return false;
    }
    if (!(spot->inner_angle >= 0 && spot->inner_angle <= spot->outer_angle)) {
        snprintf(scene3d->binding_error, sizeof scene3d->binding_error, "Spot Light inner angle must be 0 to its outer angle.");
        return false;
    }
    float length = cf_len_v3(spot->direction);
    float outer = cosf(spot->outer_angle * CF_PI / 360),
          inner = cosf(spot->inner_angle * CF_PI / 360);
    float scale = 1 / fmaxf(inner - outer, hardest_fade);
    *cone = (CF_V4){ 0 };
    if (isfinite(length) && length > 0) {
        CF_V3 axis = cf_mul_v3_f(spot->direction, scale / length);
        *cone = (CF_V4){ axis.x, axis.y, axis.z, -outer * scale };
    }
    return true;
}

static bool collect_spot_lights(
    render_scene3d *scene3d,
    ecs_world *world,
    CF_V3 eye,
    const engine *runtime,
    int *candidates
)
{
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameSpotLight.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) != ECS_OK) {
        return true;
    }
    bool valid = true;
    ecs_entity entity;
    GameTransform position;
    while (valid && ecs_query_next(&query, &entity)) {
        const GameSpotLight *spot = GAME_GameSpotLight_get_const(world, entity);
        CF_V4 cone;
        valid = spot_cone(scene3d, spot, &cone);
        if (!valid || (!cone.x && !cone.y && !cone.z) ||
            !place_light(world, runtime, entity, &position)) {
            continue;
        }
        GamePointLight light = { .color = spot->color,
                                 .intensity = spot->intensity,
                                 .radius = spot->radius,
                                 .elevation = spot->elevation,
                                 .shadows = spot->shadows };
        *candidates += rank_light(scene3d, eye, position, &light, cone);
    }
    ecs_query_end(&query);
    return valid;
}

/* Keeps the point and spot lights with the most estimated influence on the
 * camera. Returns false with a binding error for a spot light out of range. */
static bool collect_lights(
    render_scene3d *scene3d,
    ecs_world *world,
    const render_scene3d_camera *camera,
    const engine *runtime
)
{
    scene3d->light_count = 0;
    scene3d->shadowed_lights = 0;
    CF_V3 eye = render_scene3d_eye(camera);
    int candidates = 0;
    collect_point_lights(scene3d, world, eye, runtime, &candidates);
    if (!collect_spot_lights(scene3d, world, eye, runtime, &candidates)) {
        return false;
    }
    int dropped = candidates - scene3d->light_count;
    if ((dropped > 0) != (scene3d->lights_dropped > 0)) {
        fprintf(
            stderr,
            "3D lights: %d active, %d omitted by the %d-light camera "
            "influence budget.\n",
            scene3d->light_count,
            dropped,
            LIGHT_LIMIT
        );
    }
    scene3d->lights_dropped = dropped;
    order_lights(scene3d);
    return true;
}

static CF_V4 radiance(unsigned color, float intensity)
{
    return (CF_V4){ ((color >> 24) & 255u) / 255.0f * intensity,
                    ((color >> 16) & 255u) / 255.0f * intensity,
                    ((color >> 8) & 255u) / 255.0f * intensity,
                    0 };
}

/* Wider suns blur penumbrae past what the soft filter's taps resolve. */
static const float widest_sun = 20;

static bool set_sun(render_scene3d *scene3d, const GameDirectionalLight *light)
{
    if (!(light->angular_diameter >= 0 && light->angular_diameter <= widest_sun)) {
        snprintf(
            scene3d->binding_error,
            sizeof scene3d->binding_error,
            "Directional Light angular diameter must be 0 to %g degrees.",
            widest_sun
        );
        return false;
    }
    CF_V3 direction = light->direction;
    float length = cf_len_v3(direction);
    scene3d->sun_color = (CF_V4){ 0 };
    scene3d->sun_shadows = false;
    scene3d->sun_spread = tanf(light->angular_diameter * CF_PI / 360);
    if (!isfinite(length) || length <= 0 || !isfinite(light->intensity) ||
        light->intensity <= 0) {
        return true;
    }
    direction = cf_div_v3_f(direction, -length);
    scene3d->sun_direction = (CF_V4){ direction.x, direction.y, direction.z, 0 };
    scene3d->sun_color = radiance(light->color, light->intensity);
    scene3d->sun_shadows = light->shadows && light->color >> 8;
    return true;
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
    bool valid = true;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        /* The light has no place in the world, so fog cannot cull it; only
         * hiding it at run time leaves it out. */
        const GameRenderState *state = GAME_GameRenderState_get_const(world, entity);
        if (runtime && state && state->hidden) {
            continue;
        }
        if (count++) {
            break;
        }
        valid = set_sun(scene3d, GAME_GameDirectionalLight_get_const(world, entity));
    }
    ecs_query_end(&query);
    if (count > 1) {
        snprintf(scene3d->binding_error, sizeof scene3d->binding_error, "A 3D scene can have only one Directional Light.");
        return false;
    }
    return valid;
}

/* Orders the geometry of one pass's solids: shapes by primitive, meshes by
 * identity and index range, so that draws of one geometry coalesce. */
static int compare_geometry(const scene3d_solid *a, const scene3d_solid *b)
{
    if (!a->mesh) {
        return (a->shape.primitive > b->shape.primitive) -
               (a->shape.primitive < b->shape.primitive);
    }
    if (a->mesh->identity != b->mesh->identity) {
        return a->mesh->identity < b->mesh->identity ? -1 : 1;
    }
    return (a->first > b->first) - (a->first < b->first);
}

/* Opaque draws group by pass, shapes before imported meshes, whose programs
 * are numbered apart, then by program and geometry. Translucent ones go back
 * to front. */
static int compare_draws(const void *left, const void *right)
{
    const scene3d_draw *a = left, *b = right;
    if (a->transparent != b->transparent) {
        return a->transparent ? 1 : -1;
    }
    if (a->transparent && a->depth != b->depth) {
        return a->depth > b->depth ? -1 : 1;
    }
    if (!a->transparent && !a->solid.mesh != !b->solid.mesh) {
        return a->solid.mesh ? 1 : -1;
    }
    if (!a->transparent && a->program != b->program) {
        return a->program < b->program ? -1 : 1;
    }
    int geometry = a->transparent ? 0 : compare_geometry(&a->solid, &b->solid);
    return geometry ? geometry : (a->row > b->row) - (a->row < b->row);
}

/* Whether any light casts shadows, so solids outside the view still count. */
static bool casts_shadows(const render_scene3d *scene3d)
{
    return scene3d->sun_shadows || scene3d->shadowed_lights;
}

/* Boxes around the solids in view, then the casters outside it. Opaque draws
 * sort first, so draw i has box i. Returns the boxes in view. */
static int collect_shadow_boxes(render_scene3d *scene3d)
{
    aclear(scene3d->shadow_boxes);
    for (int i = 0; i < acount(scene3d->buffer.draws); i++) {
        const scene3d_draw *draw = &scene3d->buffer.draws[i];
        if (!draw->particle_shape) {
            apush(scene3d->shadow_boxes, solid_box(&draw->solid));
        }
    }
    int receivers = acount(scene3d->shadow_boxes);
    for (int i = 0; i < acount(scene3d->buffer.casters); i++) {
        apush(scene3d->shadow_boxes, solid_box(&scene3d->buffer.casters[i].solid));
    }
    return receivers;
}

/* Fits the sun's shadow cascades to the boxes. Returns the cascade count. */
static int fit_shadow(render_scene3d *scene3d, const render_scene3d_camera *camera, int receivers)
{
    if (!scene3d->sun_shadows) {
        return 0;
    }
    CF_V3 toward = { scene3d->sun_direction.x,
                     scene3d->sun_direction.y,
                     scene3d->sun_direction.z };
    return render_scene3d_shadow_fit(
        scene3d->shadow,
        toward,
        scene3d->sun_spread,
        scene3d->shadow_boxes,
        acount(scene3d->shadow_boxes),
        receivers,
        camera
    );
}

/* Submits a solid's triangles under the current shader. Its mesh is
 * already uploaded. */
static void draw_solid(const render_scene3d *scene3d, const scene3d_solid *solid)
{
    cf_draw3d_push();
    cf_draw3d_transform(solid->frame);
    if (solid->mesh) {
        render_scene3d_mesh_cache_draw(
            scene3d->meshes,
            solid->mesh,
            solid->first,
            solid->count
        );
    } else {
        draw_shape(scene3d, &solid->shape);
    }
    cf_draw3d_pop();
}

/* The programs that draw a masked solid into shadow maps. */
static render_programs *shadow_programs(const render_scene3d *scene3d, const scene3d_solid *solid)
{
    return solid->mesh ? scene3d->mesh_shadow_programs : scene3d->shadow_programs;
}

/* Submits a draw with its material row and id, which the scene shaders read
 * as v_material. */
static void draw_with_material(const render_scene3d *scene3d, const scene3d_draw *draw)
{
    const scene3d_solid *solid = &draw->solid;
    CF_V4 material = { (float)draw->row,
                       (float)draw->material,
                       (float)solid->shape.primitive,
                       (float)draw->particle_shape };
    /* Cute solids send the first attributes to in_uv_rect; custom meshes send the second. */
    cf_draw3d_push_mesh_attributes(material);
    cf_draw3d_push_mesh_attributes2(material);
    if (draw->particle_shape) {
        cf_draw3d_push();
        cf_draw3d_transform(solid->frame);
        cf_draw3d_scale((CF_V3){ solid->shape.width, solid->shape.height, 1 });
        cf_draw3d_mesh(scene3d->particle_quad);
        cf_draw3d_pop();
    } else {
        draw_solid(scene3d, solid);
    }
    cf_draw3d_pop_mesh_attributes2();
    cf_draw3d_pop_mesh_attributes();
}

/* Writes a draw's depth into the current shadow pass. A masked material runs
 * its shadow program, which leaves out the pixels it cuts. */
static void cast(const render_scene3d *scene3d, const scene3d_draw *draw)
{
    if (!draw->masked) {
        draw_solid(scene3d, &draw->solid);
        return;
    }
    render_programs *programs = shadow_programs(scene3d, &draw->solid);
    render_programs_bind3d(programs, draw->shadow_program);
    draw_with_material(scene3d, draw);
    render_programs_unbind3d(programs, draw->shadow_program);
}

/* Opaque solids in view and the casters write their depth from the sun into
 * each cascade they reach. */
static bool draw_shadow(render_scene3d *scene3d, int cascades, int receivers)
{
    render_scene3d_shadow *shadow = scene3d->shadow;
    for (int cascade = 0; cascade < cascades; cascade++) {
        if (!render_scene3d_shadow_begin(
                shadow,
                cascade,
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
            if (render_scene3d_shadow_reaches(shadow, cascade, i)) {
                cast(scene3d, draw);
            }
        }
        for (int i = 0; i < acount(scene3d->buffer.casters); i++) {
            if (render_scene3d_shadow_reaches(shadow, cascade, receivers + i)) {
                cast(scene3d, &scene3d->buffer.casters[i]);
            }
        }
        render_scene3d_shadow_end(shadow, cascade);
    }
    return true;
}

/* The draw around box i: an opaque draw in view or a caster outside it. */
static const scene3d_draw *boxed_draw(const render_scene3d *scene3d, int i, int receivers)
{
    return i < receivers ? &scene3d->buffer.draws[i]
                         : &scene3d->buffer.casters[i - receivers];
}

/* Opaque solids in view and the casters near each shadowed point or spot
 * light write their depth from it into each face they reach. */
static bool draw_light_shadows(render_scene3d *scene3d, int receivers)
{
    render_scene3d_light_shadow *shadow = scene3d->light_shadow;
    render_scene3d_light_shadow_set(
        shadow,
        scene3d->light_positions,
        scene3d->light_cones,
        scene3d->shadowed_lights
    );
    int opaque = 0;
    while (opaque < receivers && !scene3d->buffer.draws[opaque].transparent) {
        opaque++;
    }
    for (int light = 0; light < scene3d->shadowed_lights; light++) {
        aclear(scene3d->near_boxes);
        for (int i = 0; i < acount(scene3d->shadow_boxes); i++) {
            bool solid = i < opaque || i >= receivers;
            if (solid &&
                render_scene3d_light_shadow_near(shadow, light, scene3d->shadow_boxes[i])) {
                apush(scene3d->near_boxes, i);
            }
        }
        for (int face = 0; face < render_scene3d_light_shadow_faces(shadow, light);
             face++) {
            if (!render_scene3d_light_shadow_begin(
                    shadow,
                    light,
                    face,
                    scene3d->binding_error,
                    sizeof scene3d->binding_error
                )) {
                return false;
            }
            for (int i = 0; i < acount(scene3d->near_boxes); i++) {
                int box = scene3d->near_boxes[i];
                if (render_scene3d_light_shadow_reaches(
                        shadow,
                        light,
                        face,
                        scene3d->shadow_boxes[box]
                    )) {
                    cast(scene3d, boxed_draw(scene3d, box, receivers));
                }
            }
            render_scene3d_light_shadow_end(shadow, light, face);
        }
    }
    return true;
}

/* The sky and trails go between opaque and translucent surfaces: the sky
 * fills what opaque ones left open, and translucent ones blend over both. */
static void draw_behind_translucents(render_scene3d *scene3d, const render_scene3d_camera *camera)
{
    render_scene3d_sky_draw(scene3d->sky, camera);
    render_scene3d_trails_draw(scene3d->trails, camera);
}

/* The programs that draw a solid: the mesh pass's for an imported mesh. */
static render_programs *solid_programs(const render_scene3d *scene3d, const scene3d_solid *solid)
{
    return solid->mesh ? scene3d->mesh_programs : scene3d->programs;
}

/* Uploads a solid's mesh, or returns false with a binding error. */
static bool upload(render_scene3d *scene3d, const scene3d_solid *solid)
{
    if (!solid->mesh || render_scene3d_mesh_cache_upload(scene3d->meshes, solid->mesh)) {
        return true;
    }
    snprintf(scene3d->binding_error, sizeof scene3d->binding_error, "Cannot upload an imported mesh.");
    return false;
}

/* The program in `programs` that draws `material`, or zero with a binding
 * error. A program that fails to rebuild keeps its last version, which draws
 * while the error is reported. */
static int prepare_program(render_scene3d *scene3d, render_programs *programs, int material)
{
    render_shader_set set = { 0 };
    shader_error error = { 0 };
    if (!render_shader_set_add(&set, material, &error)) {
        snprintf(
            scene3d->binding_error,
            sizeof scene3d->binding_error,
            "%s",
            error.message
        );
        return 0;
    }
    int program = render_programs_prepare(programs, &set);
    if (render_programs_error(programs)[0]) {
        snprintf(scene3d->binding_error, sizeof scene3d->binding_error, "%s", render_programs_error(programs));
    }
    return program;
}

/* Compiles the shadow program a masked draw needs while a light casts
 * shadows, and uploads the draw's mesh. Returns false with a binding error. */
static bool prepare_solid(render_scene3d *scene3d, scene3d_draw *draw)
{
    if (draw->masked && casts_shadows(scene3d)) {
        draw->shadow_program = prepare_program(
            scene3d,
            shadow_programs(scene3d, &draw->solid),
            draw->material
        );
        if (!draw->shadow_program) {
            return false;
        }
    }
    return upload(scene3d, &draw->solid);
}

/* Compiles the programs each draw and caster needs and uploads their meshes.
 * Returns false with a binding error. */
static bool prepare_draws(render_scene3d *scene3d)
{
    for (int i = 0; i < acount(scene3d->buffer.draws); i++) {
        scene3d_draw *draw = &scene3d->buffer.draws[i];
        draw->program = prepare_program(scene3d, solid_programs(scene3d, &draw->solid), draw->material);
        if (!draw->program || !prepare_solid(scene3d, draw)) {
            return false;
        }
    }
    for (int i = 0; i < acount(scene3d->buffer.casters); i++) {
        if (!prepare_solid(scene3d, &scene3d->buffer.casters[i])) {
            return false;
        }
    }
    return true;
}

static bool submit(render_scene3d *scene3d, const render_scene3d_camera *camera, float time)
{
    if (!acount(scene3d->buffer.draws)) {
        draw_behind_translucents(scene3d, camera);
        return true;
    }
    if (!prepare_draws(scene3d)) {
        return false;
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
    asort(scene3d->buffer.draws, compare_draws);
    /* Masked materials read the scene's uniforms and parameters in shadow
     * passes too. */
    CF_V3 position = render_scene3d_eye(camera);
    CF_V4 eye = { position.x, position.y, position.z, time };
    cf_draw3d_set_uniform("scene_camera_time", &eye, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_rhythm", &scene3d->rhythm, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_sun_direction", &scene3d->sun_direction, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_sun_color", &scene3d->sun_color, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_ambient_sky", &scene3d->ambient_sky, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_ambient_ground", &scene3d->ambient_ground, CF_UNIFORM_TYPE_FLOAT4, 1);
    CF_V4 light_count = { .x = (float)scene3d->light_count };
    cf_draw3d_set_uniform("scene_light_count", &light_count, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_light_positions", scene3d->light_positions, CF_UNIFORM_TYPE_FLOAT4, LIGHT_LIMIT);
    cf_draw3d_set_uniform("scene_light_colors", scene3d->light_colors, CF_UNIFORM_TYPE_FLOAT4, LIGHT_LIMIT);
    cf_draw3d_set_uniform("scene_light_cones", scene3d->light_cones, CF_UNIFORM_TYPE_FLOAT4, LIGHT_LIMIT);
    render_influences_bind(scene3d->influences);
    cf_draw3d_set_texture("scene_parameters", scene3d->parameters);
    int receivers = casts_shadows(scene3d) ? collect_shadow_boxes(scene3d) : 0;
    int cascades = fit_shadow(scene3d, camera, receivers);
    scene3d->work.cascades = cascades;
    scene3d->work.light_shadows = scene3d->shadowed_lights;
    if (!draw_shadow(scene3d, cascades, receivers) ||
        !draw_light_shadows(scene3d, receivers)) {
        cf_draw3d_set_texture("scene_parameters", (CF_Texture){ 0 });
        return false;
    }
    render_scene3d_shadow_bind(scene3d->shadow, cascades);
    render_scene3d_light_shadow_bind(scene3d->light_shadow);
    render_scene3d_sky_bind(scene3d->sky);
    cf_draw3d_push_projection(camera->projection);
    cf_draw3d_push_view(camera->view);
    render_programs *bound = NULL;
    int program = 0;
    CF_RenderState state = cf_render_state_3d_defaults();
    cf_draw3d_push_render_state(state);
    bool transparent = false;
    for (int i = 0; i < acount(scene3d->buffer.draws); i++) {
        const scene3d_draw *draw = &scene3d->buffer.draws[i];
        render_programs *programs = solid_programs(scene3d, &draw->solid);
        if (bound != programs || program != draw->program) {
            if (bound) {
                render_programs_unbind3d(bound, program);
            }
            bound = programs;
            program = draw->program;
            render_programs_bind3d(bound, program);
        }
        if (draw->transparent && !transparent) {
            draw_behind_translucents(scene3d, camera);
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
        draw_with_material(scene3d, draw);
    }
    cf_draw3d_pop_render_state();
    render_programs_unbind3d(bound, program);
    /* Later meshes would otherwise keep the parameters bound past their resize or free. */
    cf_draw3d_set_texture("scene_parameters", (CF_Texture){ 0 });
    render_scene3d_shadow_unbind();
    render_scene3d_light_shadow_unbind();
    render_scene3d_sky_unbind();
    cf_draw3d_pop_view();
    cf_draw3d_pop_projection();
    if (!transparent) {
        draw_behind_translucents(scene3d, camera);
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
        aclear(buffer->casters);
        buffer->binding_error[0] = 0;
        buffer->worker = worker;
        buffer->shadows = casts_shadows(scene3d);
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
        scene3d->buffer.shadows = casts_shadows(scene3d);
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
        int pixels = acount(scene3d->buffer.pixels), rows = pixels / MATERIAL_TEXELS;
        for (int i = 0; i < acount(buffer->draws); i++) {
            scene3d_draw draw = buffer->draws[i];
            draw.row += rows;
            apush(scene3d->buffer.draws, draw);
        }
        for (int i = 0; i < acount(buffer->casters); i++) {
            scene3d_draw caster = buffer->casters[i];
            caster.row += caster.masked ? rows : 0;
            apush(scene3d->buffer.casters, caster);
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
    /* Hosts may draw edits, loads and hand-built worlds that no engine frame
     * has propagated yet, so drawing starts from current global transforms. */
    int propagated = game_transform_propagate(world);
    if (propagated != ECS_OK) {
        snprintf(scene3d->binding_error, sizeof scene3d->binding_error, "Transform propagation failed (%d).", propagated);
        return false;
    }
    aclear(scene3d->buffer.pixels);
    aclear(scene3d->buffer.draws);
    aclear(scene3d->buffer.casters);
    render_scene3d_mesh_cache_prune(scene3d->meshes);
    /* Rhythm is an optional material input; without a valid clock it rests at zero. */
    game_beat beat = { 0 };
    game_rhythm_sample(world, time, &beat);
    scene3d->rhythm = (CF_V4){ (float)beat.phase,
                               (float)beat.pulse,
                               (float)beat.period,
                               (float)beat.duration };
    set_ambient(scene3d, environment);
    if (!render_scene3d_sky_prepare(
            scene3d->sky,
            environment,
            scene3d->binding_error,
            sizeof scene3d->binding_error
        )) {
        return false;
    }
    if (!collect_sun(scene3d, world, runtime)) {
        return false;
    }
    if (!collect_lights(scene3d, world, camera, runtime)) {
        return false;
    }
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
    bool submitted = submit(scene3d, camera, time);
    scene3d->work.meshes = render_scene3d_mesh_cache_count(scene3d->meshes);
    return submitted;
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
    aclear(scene3d->buffer.casters);
    scene3d->light_count = 0;
    scene3d->shadowed_lights = 0;
    scene3d->rhythm = (CF_V4){ .x = activity->beat_phase, .y = activity->beat_pulse };
    GameDirectionalLight sun = game_directional_light_defaults();
    sun.shadows = false;
    GameEnvironment environment = game_environment_defaults();
    set_sun(scene3d, &sun);
    set_ambient(scene3d, &environment);
    if (!render_scene3d_sky_prepare(
            scene3d->sky,
            &environment,
            scene3d->binding_error,
            sizeof scene3d->binding_error
        )) {
        return false;
    }
    render_scene3d_reset(scene3d);
    render_influences_preview(scene3d->influences, activity->influence, RENDER_SCENE3D_PREVIEW_RADIUS);
    if (!material || render_shader_is_shape(material)) {
        return false;
    }
    float parameters[SHADER_PARAMETER_MAX][4];
    render_shader_parameters(material, NULL, (ecs_entity){ 0 }, parameters_json, parameters);
    game_material_coefficients color = { .scene = { 1, 1, 1, 1 } };
    scene3d_draw draw = {
        .solid = { .frame = cf_m4_identity(),
                   .shape = { .primitive = GAME_PRIMITIVE_SPHERE,
                              .width = RENDER_SCENE3D_PREVIEW_RADIUS * 2,
                              .height = RENDER_SCENE3D_PREVIEW_RADIUS * 2,
                              .depth = RENDER_SCENE3D_PREVIEW_RADIUS * 2 } },
        .material = material
    };
    append_draw(&scene3d->buffer, draw, &color, parameters, camera);
    return submit(scene3d, camera, time);
}
