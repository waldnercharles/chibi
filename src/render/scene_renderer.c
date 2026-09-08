#include "render/scene_renderer.h"

#include "platform/cute.h"
#include "render/appearance_geometry.h"
#include "render/material.h"
#include "render/material_raster.h"
#include "render/radiance.h"
#include "render/sdf_motion.h"
#include "render/shader_catalog.h"
#include "render/shader_fragment.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct app_scene_renderer
{
    render_material_raster *raster;
    render_radiance *radiance;
    CF_Canvas output;
    render_material_entity *entities;
    ecs_world *world;
    engine_viewport_camera camera;
    int width, height;
    float pixel_aa, time;
    CF_Shader fragment;
    unsigned fragment_revision;
};

void app_scene_renderer_initialize()
{
    cf_fs_mount(CBLUEPRINT_RENDER_SHADER_DIR, "/cblueprint-render", true);
    cf_shader_directory("/cblueprint-render");
}

app_scene_renderer *app_scene_renderer_new()
{
    app_scene_renderer *renderer = xcalloc(1, sizeof *renderer);
    if (cf_query_backend() == CF_BACKEND_TYPE_GLES3) {
        return renderer;
    }
    renderer->raster = render_material_raster_new();
    renderer->radiance = render_radiance_new();
    if (!renderer->raster || !renderer->radiance) {
        app_scene_renderer_free(renderer);
        return NULL;
    }
    return renderer;
}

void app_scene_renderer_free(app_scene_renderer *renderer)
{
    if (!renderer) {
        return;
    }
    render_material_raster_free(renderer->raster);
    render_radiance_free(renderer->radiance);
    if (renderer->output.id) {
        cf_destroy_canvas(renderer->output);
    }
    if (renderer->fragment.id) {
        cf_destroy_shader(renderer->fragment);
    }
    afree(renderer->entities);
    free(renderer);
}

void app_scene_renderer_reset(app_scene_renderer *renderer)
{
    if (renderer && renderer->radiance) {
        render_radiance_reset(renderer->radiance);
    }
}

float app_scene_renderer_time(const app_scene_renderer *renderer)
{
    return renderer ? renderer->time : 0;
}
void app_scene_renderer_set_time(app_scene_renderer *renderer, float time)
{
    if (renderer) {
        renderer->time = time;
    }
}
static bool fragment_ready(app_scene_renderer *renderer)
{
    unsigned version = render_shader_revision();
    if (renderer->fragment_revision != version) {
        CF_Shader fresh = render_shader_fragment_new();
        renderer->fragment_revision = version;
        if (fresh.id) {
            if (renderer->fragment.id) {
                cf_destroy_shader(renderer->fragment);
            }
            renderer->fragment = fresh;
        }
    }
    return renderer->fragment.id != 0;
}

static void copy_material(render_material_entity *entity, const game_material_coefficients *material)
{
    memcpy(entity->scene, material->scene, sizeof entity->scene);
    memcpy(entity->emission, material->emission, sizeof entity->emission);
    memcpy(entity->transmittance, material->transmittance, sizeof entity->transmittance);
    memcpy(entity->albedo, material->albedo, sizeof entity->albedo);
    memcpy(entity->border_scene, material->border_scene, sizeof entity->border_scene);
    memcpy(entity->border_emission, material->border_emission, sizeof entity->border_emission);
    memcpy(
        entity->border_transmittance,
        material->border_transmittance,
        sizeof entity->border_transmittance
    );
    memcpy(entity->border_albedo, material->border_albedo, sizeof entity->border_albedo);
}

static bool append_entity(
    app_scene_renderer *renderer,
    const GameTransform *position,
    const GameAppearance *appearance,
    float angle,
    const GameSdfMotion *motion,
    const GameMaterial *material
)
{
    CF_V2 min, max;
    float padding = 0;
    if (!isfinite(position->x) || !isfinite(position->y) ||
        !game_appearance_rotated_bounds(appearance, angle, &min, &max) ||
        (motion && !game_sdf_motion_padding(motion, &padding))) {
        return false;
    }
    game_material_coefficients coefficients;
    GameMaterial fallback = { .scene = appearance->color,
                              .emission_color = appearance->color,
                              .border_scene = appearance->border_color,
                              .border_emission_color = appearance->border_color,
                              .emission_intensity = 0.1f,
                              .emission_multiplier = 1 };
    if (!game_material_extract(material ? material : &fallback, &coefficients)) {
        return false;
    }
    float aa = isfinite(appearance->antialias) ? fmaxf(appearance->antialias, 0) : 1;
    padding += aa > 0 ? aa : renderer->pixel_aa;
    render_material_entity entity = {
        .pos = { position->x, -position->y },
        .extent = { fmaxf(fabsf(min.x), fabsf(max.x)) + padding,
                    fmaxf(fabsf(min.y), fabsf(max.y)) + padding },
        .inv_rot = { cosf(angle), sinf(angle), -sinf(angle), cosf(angle) },
        .aa = aa,
        .border_thickness = fmaxf(0, appearance->border_width)
    };
    float half_x = appearance->width * 0.5f, half_y = appearance->height * 0.5f;
    switch (appearance->shape) {
        case GAME_SHAPE_CIRCLE:
            entity.shape_type = RENDER_SHAPE_CIRCLE;
            entity.radius = fminf(half_x, half_y);
            break;
        case GAME_SHAPE_CAPSULE: {
            entity.shape_type = RENDER_SHAPE_CAPSULE;
            entity.radius = fminf(half_x, half_y);
            entity.points[0] = -(half_x - entity.radius);
            entity.points[1] = -(half_y - entity.radius);
            entity.points[2] = -entity.points[0];
            entity.points[3] = -entity.points[1];
            break;
        }
        case GAME_SHAPE_DIAMOND:
        case GAME_SHAPE_CHEVRON:
        case GAME_SHAPE_STROKED_CHEVRON: {
            CF_V2 points[4];
            int count = game_appearance_points(appearance, points);
            entity.point_count = (unsigned)count;
            entity.shape_type = appearance->shape == GAME_SHAPE_STROKED_CHEVRON
                                  ? RENDER_SHAPE_POLY
                                  : RENDER_SHAPE_POLY_FILL;
            entity.thickness = appearance->stroke_width;
            for (int i = 0; i < count; i++) {
                entity.points[i * 2] = points[i].x;
                entity.points[i * 2 + 1] = points[i].y;
            }
            break;
        }
        default:
            entity.shape_type = RENDER_SHAPE_BOX;
            entity.points[0] = half_x;
            entity.points[1] = half_y;
            break;
    }
    if (motion) {
        entity.wobble[0] = motion->amplitude;
        entity.wobble[1] = motion->angular_frequency;
        entity.wobble[2] = motion->temporal_frequency;
        entity.pluck[0] = motion->pluck_amplitude;
        entity.pluck[1] = motion->pluck_origin;
        entity.pluck[2] = motion->pluck_age;
        entity.pluck[3] = motion->pluck_speed;
        entity.time[0] = motion->time;
    }
    entity.time[0] = renderer->time;
    copy_material(&entity, &coefficients);
    apush(renderer->entities, entity);
    return true;
}

static unsigned particle_color(unsigned start, unsigned end, float age)
{
    unsigned color = 0;
    for (int shift = 0; shift <= 24; shift += 8) {
        float a = (float)((start >> shift) & 255u);
        float b = (float)((end >> shift) & 255u);
        color |= (unsigned)roundf(a + (b - a) * age) << shift;
    }
    return color;
}

static void collect(app_scene_renderer *renderer, ecs_world *world, const game_particles *particles)
{
    asetlen(renderer->entities, 0);
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameAppearance.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            const GameOrientation *orientation = GAME_GameOrientation_get_const(world, entity);
            const GameShader *binding = GAME_GameShader_get_const(world, entity);
            GameAppearance appearance = *GAME_GameAppearance_get_const(world, entity);
            int shape = binding ? render_shader_find(binding->shape) : 0;
            float bounds[2];
            if (render_shader_bounds(shape, bounds)) {
                appearance.shape = GAME_SHAPE_BOX;
                appearance.width = 2 * (binding->bounds_x > 0 ? binding->bounds_x
                                                              : bounds[0]);
                appearance.height = 2 * (binding->bounds_y > 0 ? binding->bounds_y
                                                               : bounds[1]);
            }
            if (!append_entity(
                    renderer,
                    GAME_GameTransform_get_const(world, entity),
                    &appearance,
                    orientation ? orientation->radians : 0,
                    GAME_GameSdfMotion_get_const(world, entity),
                    GAME_GameMaterial_get_const(world, entity)
                )) {
                continue;
            }
            if (binding) {
                render_material_entity
                    *draw = &renderer->entities[acount(renderer->entities) - 1];
                int material = render_shader_find(binding->material);
                draw->reserved[0] = render_shader_is_shape(material) ? 0 : (float)material;
                draw->reserved[1] = render_shader_is_shape(shape) ? (float)shape : 0;
                render_shader_parameters(material, world, entity, binding->parameters, draw->material_parameters);
                render_shader_parameters(shape, world, entity, binding->parameters, draw->shape_parameters);
            }
        }
        ecs_query_end(&query);
    }
    const game_particle *items = game_particles_data(particles);
    for (int i = 0; i < game_particles_count(particles); i++) {
        const game_particle *particle = &items[i];
        float life = 1 - particle->age / particle->lifetime;
        if (particle->size > 0) {
            float age = 1 - life;
            float phase = particle->size_exponent > 0
                            ? powf(age, particle->size_exponent)
                            : 0;
            float diameter = 2 * particle->size * (1 - phase);
            unsigned color = particle_color(particle->color, particle->end_color, age);
            GameAppearance appearance = { .width = diameter,
                                          .height = diameter,
                                          .color = color,
                                          .shape = GAME_SHAPE_CIRCLE,
                                          .antialias = 1.5f };
            GameMaterial material = { .scene = color,
                                      .emission_color = color,
                                      .emission_intensity = particle->emission,
                                      .emission_multiplier = 1,
                                      .absorption = 0.01f };
            GameTransform position = { particle->position.x, particle->position.y };
            append_entity(renderer, &position, &appearance, 0, NULL, &material);
            continue;
        }
        unsigned color = (particle->color & 0xffffff00u) |
                         (unsigned)((particle->color & 255u) * life);
        GameAppearance appearance = { .width = 1 + 3 * life,
                                      .height = 1 + 3 * life,
                                      .color = color,
                                      .shape = GAME_SHAPE_BOX,
                                      .antialias = 1 };
        GameTransform position = { particle->position.x, particle->position.y };
        append_entity(renderer, &position, &appearance, 0, NULL, NULL);
    }
}

bool app_scene_renderer_draw(
    app_scene_renderer *renderer,
    ecs_world *world,
    const game_particles *particles,
    engine_viewport *viewport
)
{
    if (!renderer) {
        return false;
    }
    engine_viewport_camera camera;
    engine_viewport_get_camera(viewport, &camera);
    int width, height;
    engine_viewport_pixel_size(viewport, &width, &height);
    /* Material edges and final composition retain viewport pixel resolution.
     * Only the radiance work grid is reduced, inside render_radiance_resize. */
    width = width < 2 ? 2 : width;
    height = height < 2 ? 2 : height;
    if (renderer->world != world || memcmp(&renderer->camera, &camera, sizeof camera)) {
        app_scene_renderer_reset(renderer);
        renderer->world = world;
        renderer->camera = camera;
    }
    if (!renderer->raster) {
        renderer->pixel_aa = fmaxf(camera.span_width / width, camera.span_height / height) *
                             0.5f;
        collect(renderer, world, particles);
        if (!fragment_ready(renderer)) {
            return false;
        }
        for (int i = 0; i < acount(renderer->entities); i++) {
            render_shader_fragment_draw(renderer->fragment, &renderer->entities[i]);
        }
        return true;
    }
    if (renderer->width != width || renderer->height != height) {
        if (!render_material_raster_resize(renderer->raster, width, height) ||
            !render_radiance_resize(renderer->radiance, width, height)) {
            return false;
        }
        CF_Canvas output = cf_make_canvas(cf_canvas_defaults(width, height));
        if (!output.id) {
            return false;
        }
        if (renderer->output.id) {
            cf_destroy_canvas(renderer->output);
        }
        renderer->output = output;
        renderer->width = width;
        renderer->height = height;
    }
    renderer->pixel_aa = fmaxf(camera.span_width / width, camera.span_height / height) *
                         0.5f;
    collect(renderer, world, particles);
    render_material_view view = { camera.center_x - camera.span_width * 0.5f,
                                  camera.center_y + camera.span_height * 0.5f,
                                  camera.span_width,
                                  camera.span_height };
    if (!render_material_raster_draw(
            renderer->raster,
            renderer->entities,
            acount(renderer->entities),
            view
        )) {
        return false;
    }
    render_material_textures textures = render_material_raster_textures(renderer->raster);
    render_radiance_inputs inputs = { textures.scene,
                                      textures.emission,
                                      textures.transmittance,
                                      textures.albedo };
    if (!render_radiance_draw(renderer->radiance, inputs, renderer->output)) {
        return false;
    }
    cf_draw_canvas(
        renderer->output,
        (CF_V2){ camera.center_x, camera.center_y },
        (CF_V2){ camera.span_width, camera.span_height }
    );
    return true;
}

bool app_scene_renderer_preview(
    app_scene_renderer *renderer,
    const behavior_asset *asset,
    float time,
    int width,
    int height
)
{
    (void)width;
    (void)height;
    shader_error error;
    if (!renderer || !asset) {
        return false;
    }
    render_shader_upsert(asset, &error);
    int id = render_shader_find(asset->name);
    if (!id || !fragment_ready(renderer)) {
        return false;
    }
    GameAppearance appearance = { .width = 100,
                                  .height = 100,
                                  .shape = GAME_SHAPE_CIRCLE,
                                  .color = 0xFFFFFFFF,
                                  .antialias = 0.5f };
    float bounds[2];
    if (render_shader_bounds(id, bounds)) {
        appearance.width = 2 * bounds[0];
        appearance.height = 2 * bounds[1];
        appearance.shape = GAME_SHAPE_BOX;
    }
    GameTransform position = { 0 };
    asetlen(renderer->entities, 0);
    renderer->time = time;
    if (!append_entity(renderer, &position, &appearance, 0, NULL, NULL)) {
        return false;
    }
    render_material_entity *draw = &renderer->entities[0];
    if (render_shader_is_shape(id)) {
        draw->reserved[1] = (float)id;
        render_shader_parameters(id, NULL, (ecs_entity){ 0 }, NULL, draw->shape_parameters);
    } else {
        draw->reserved[0] = (float)id;
        render_shader_parameters(id, NULL, (ecs_entity){ 0 }, NULL, draw->material_parameters);
    }
    render_shader_fragment_draw(renderer->fragment, draw);
    return true;
}
