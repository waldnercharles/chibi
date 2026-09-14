#include "render/scene_renderer.h"

#include "platform/cute.h"
#include "render/appearance_geometry.h"
#include "render/arena_post.h"
#include "render/fog.h"
#include "render/material.h"
#include "render/material_raster.h"
#include "render/radiance.h"
#include "render/sdf_motion.h"
#include "render/shader_catalog.h"
#include "render/shader_fragment.h"
#include "render/shader_images.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct app_scene_renderer
{
    render_arena *arena;
    render_arena_post *post;
    render_fog *fog;
    bool arena_attempted, parallel_disabled;
    render_material_raster *raster;
    render_radiance *radiance;
    CF_Canvas output;
    render_material_entity *entities;
    ecs_world *world;
    engine_viewport_camera camera;
    int width, height;
    float pixel_aa, time;
    render_programs *fragment;
    int *fragment_programs;
    char error[256];
};

void app_scene_renderer_initialize()
{
    char message[256];
    if (!render_image_mount_content("content", message, sizeof message)) {
        fprintf(stderr, "%s\n", message);
    }
    cf_fs_mount(CHIBI_RENDER_SHADER_DIR, "/chibi-render", true);
    cf_shader_directory("/chibi-render");
}

unsigned app_scene_renderer_shader_compile_count()
{
    return render_shader_compile_count();
}

app_scene_renderer *app_scene_renderer_new()
{
    app_scene_renderer *renderer = xcalloc(1, sizeof *renderer);
    renderer->fog = render_fog_new();
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
    render_arena_free(renderer->arena);
    render_arena_post_free(renderer->post);
    render_fog_free(renderer->fog);
    render_radiance_free(renderer->radiance);
    if (renderer->output.id) {
        cf_destroy_canvas(renderer->output);
    }
    render_programs_free(renderer->fragment);
    afree(renderer->fragment_programs);
    afree(renderer->entities);
    free(renderer);
}

static bool arena_ready(app_scene_renderer *renderer)
{
    if (!renderer->arena_attempted) {
        renderer->arena_attempted = true;
        renderer->arena = render_arena_new();
        render_arena_set_parallel(renderer->arena, !renderer->parallel_disabled);
        renderer->post = render_arena_post_new();
    }
    return renderer->arena && renderer->post;
}

const char *app_scene_renderer_error(const app_scene_renderer *renderer)
{
    return renderer ? renderer->error : "";
}

int app_scene_renderer_particle_count(const app_scene_renderer *renderer)
{
    return renderer && renderer->arena ? render_arena_particle_count(renderer->arena)
                                       : 0;
}

static bool arena_environment(app_scene_renderer *renderer, ecs_world *world, GameEnvironment *settings)
{
    *settings = render_arena_post_defaults();
    if (!ecs_world_component(world, GAME_COMPONENT_GameEnvironment.id)) {
        return true;
    }
    ecs_component_id required[] = { GAME_COMPONENT_GameEnvironment.id };
    ecs_query query;
    int result = ecs_query_begin(world, &query, required, 1);
    assert(result == ECS_OK);
    (void)result;
    ecs_entity entity;
    bool found = false, valid = true;
    while (ecs_query_next(&query, &entity)) {
        if (found) {
            snprintf(renderer->error, sizeof renderer->error, "A 3D scene can have only one Environment component.");
            valid = false;
            break;
        }
        *settings = *GAME_GameEnvironment_get_const(world, entity);
        found = true;
    }
    ecs_query_end(&query);
    return valid;
}

static void arena_present(app_scene_renderer *renderer)
{
    CF_Canvas output = render_arena_post_output(renderer->post);
    if (!output.id) {
        return;
    }
    cf_draw_push();
    cf_draw_TSR_absolute((CF_V2){ 0 }, (CF_V2){ 1, 1 }, 0);
    cf_draw_projection(cf_ortho_2d(0, 0, 2, 2));
    cf_draw_canvas(output, (CF_V2){ 0 }, (CF_V2){ 2, 2 });
    cf_draw_pop();
}

static bool arena_prepare(app_scene_renderer *renderer, int width, int height, const GameEnvironment *settings)
{
    if (render_arena_post_prepare(renderer->post, width, height, settings)) {
        return true;
    }
    snprintf(
        renderer->error,
        sizeof renderer->error,
        "%s",
        render_arena_post_error(renderer->post)
    );
    arena_present(renderer);
    return false;
}

static bool arena_finish(app_scene_renderer *renderer, bool submitted)
{
    snprintf(
        renderer->error,
        sizeof renderer->error,
        "%s",
        render_arena_error(renderer->arena)
    );
    bool processed = false;
    if (submitted) {
        cf_render_to(render_arena_post_target(renderer->post), true);
        processed = render_arena_post_process(renderer->post);
        if (!processed) {
            snprintf(
                renderer->error,
                sizeof renderer->error,
                "%s",
                render_arena_post_error(renderer->post)
            );
        }
    }
    arena_present(renderer);
    return submitted && processed;
}

bool app_scene_renderer_draw3d(
    app_scene_renderer *renderer,
    ecs_world *world,
    const game_particles *particles,
    const render_arena_camera *camera,
    engine_viewport *viewport,
    const engine *runtime
)
{
    if (!arena_ready(renderer)) {
        return false;
    }
    renderer->error[0] = 0;
    GameEnvironment settings;
    if (!arena_environment(renderer, world, &settings)) {
        arena_present(renderer);
        return false;
    }
    int width, height;
    engine_viewport_pixel_size(viewport, &width, &height);
    if (!arena_prepare(renderer, width, height, &settings)) {
        return false;
    }
    bool drawn = arena_finish(
        renderer,
        render_arena_draw(renderer->arena, world, particles, camera, renderer->time, runtime)
    );
    engine_viewport_camera frame;
    engine_viewport_get_camera(viewport, &frame);
    return drawn && render_fog_draw(
                        renderer->fog,
                        engine_visibility(runtime),
                        &frame,
                        camera,
                        renderer->time
                    );
}

void app_scene_renderer_reset(app_scene_renderer *renderer)
{
    if (renderer && renderer->arena) {
        render_arena_reset(renderer->arena);
    }
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
static bool prepare_fragments(app_scene_renderer *renderer)
{
    if (!renderer->fragment) {
        renderer->fragment = render_programs_new(RENDER_PROGRAM_FRAGMENT);
    }
    asetlen(renderer->fragment_programs, acount(renderer->entities));
    for (int i = 0; i < acount(renderer->entities); i++) {
        shader_error error = { 0 };
        int program = render_shader_fragment_prepare(
            renderer->fragment,
            &renderer->entities[i],
            &error
        );
        if (error.message[0]) {
            snprintf(renderer->error, sizeof renderer->error, "%s", error.message);
        }
        if (!program) {
            snprintf(renderer->error, sizeof renderer->error, "%s", error.message);
            return false;
        }
        renderer->fragment_programs[i] = program;
    }
    return true;
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

static void collect(
    app_scene_renderer *renderer,
    ecs_world *world,
    const game_particles *particles,
    const engine *runtime
)
{
    asetlen(renderer->entities, 0);
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameAppearance.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            if (!engine_entity_visible(runtime, world, entity)) {
                continue;
            }
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
            GameTransform position = *GAME_GameTransform_get_const(world, entity);
            if (runtime) {
                engine_render_position(runtime, entity, &position);
            }
            if (!append_entity(
                    renderer,
                    &position,
                    &appearance,
                    orientation ? -orientation->radians : 0,
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
            bool ring = particle->shape == GAME_PARTICLE_RING;
            bool triangle = particle->shape == GAME_PARTICLE_TRIANGLE ||
                            particle->shape == GAME_PARTICLE_TRIANGLE_OUTLINE;
            if (ring) {
                diameter = 2 * particle->size * (.45f + .55f * age);
            }
            unsigned color = particle_color(particle->color, particle->end_color, age);
            unsigned core = particle_color(color, 0xfffae0ffu, .7f);
            core = (core & 0xffffff00u) | (color & 255u);
            GameAppearance appearance = {
                .width = diameter * (particle->shape == GAME_PARTICLE_STREAK ? 2.8f : 1),
                .height = diameter,
                .color = ring ? color & 0xffffff00u : core,
                .border_color = color,
                .border_width = diameter * (ring ? .07f : .16f),
                .shape = particle->shape == GAME_PARTICLE_STREAK ? GAME_SHAPE_DIAMOND
                       : triangle ? GAME_SHAPE_BOX
                                  : GAME_SHAPE_CIRCLE,
                .antialias = 1.5f
            };
            GameMaterial material = { .scene = appearance.color,
                                      .emission_color = appearance.color,
                                      .border_scene = color,
                                      .border_emission_color = color,
                                      .emission_intensity = particle->emission,
                                      .emission_multiplier = 1,
                                      .absorption = 0.01f };
            GameTransform position = { particle->position.x, particle->position.y };
            if (append_entity(renderer, &position, &appearance, -particle->angle, NULL, &material) &&
                triangle) {
                render_material_entity
                    *draw = &renderer->entities[acount(renderer->entities) - 1];
                bool outline = particle->shape == GAME_PARTICLE_TRIANGLE_OUTLINE;
                draw->shape_type = outline ? RENDER_SHAPE_POLY : RENDER_SHAPE_POLY_FILL;
                draw->point_count = 3;
                draw->thickness = diameter * .08f;
                const CF_V2 points[] = { { .5f, 0 }, { -.5f, .5f }, { -.5f, -.5f } };
                for (int i = 0; i < 3; i++) {
                    // Center the stroke inside the same triangle footprint as the 3D particle.
                    draw->points[i * 2] = diameter *
                                          (outline ? -1.0f / 6 +
                                                         (points[i].x + 1.0f / 6) * .88f
                                                   : points[i].x);
                    draw->points[i * 2 + 1] = diameter * points[i].y *
                                              (outline ? .88f : 1);
                }
            }
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

static bool scene_lighting(app_scene_renderer *renderer, ecs_world *world, CF_V2 *flat)
{
    GameLighting2D lighting = renderer->radiance
                                ? (GameLighting2D){ .emission = 7.5f, .fog = 1 }
                                : (GameLighting2D){ .ambient = 1, .emission = 1 };
    ecs_component_id component = GAME_COMPONENT_GameLighting2D.id;
    ecs_query query;
    bool found = false, valid = true;
    if (ecs_world_component(world, component) &&
        ecs_query_begin(world, &query, &component, 1) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            if (ecs_entity_is_retiring(world, entity)) {
                continue;
            }
            const GameLighting2D *source = GAME_GameLighting2D_get_const(world, entity);
            if (found || !isfinite(source->ambient) || !isfinite(source->emission) ||
                !isfinite(source->fog) || !isfinite(source->shadow_floor) ||
                source->ambient < 0 || source->emission < 0 ||
                source->fog < 0 || source->shadow_floor < 0 ||
                source->ambient > 100 || source->emission > 100 ||
                source->fog > 100 || source->shadow_floor > 100) {
                valid = false;
                break;
            }
            lighting = *source;
            found = true;
        }
        ecs_query_end(&query);
    }
    if (!valid) {
        snprintf(renderer->error, sizeof renderer->error, "GameLighting2D requires one settings entity and finite gains from 0 to 100");
        return false;
    }
    *flat = (CF_V2){ lighting.ambient, lighting.emission };
    if (renderer->radiance) {
        render_radiance_set_lighting(renderer->radiance, lighting);
    }
    return true;
}

bool app_scene_renderer_draw(
    app_scene_renderer *renderer,
    ecs_world *world,
    const game_particles *particles,
    engine_viewport *viewport,
    const engine *runtime
)
{
    if (!renderer) {
        return false;
    }
    engine_viewport_camera camera;
    engine_viewport_get_camera(viewport, &camera);
    int width, height;
    renderer->error[0] = 0;
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
    CF_V2 flat;
    if (!scene_lighting(renderer, world, &flat)) {
        return false;
    }
    if (!renderer->raster) {
        renderer->pixel_aa = fmaxf(camera.span_width / width, camera.span_height / height) *
                             0.5f;
        collect(renderer, world, particles, runtime);
        if (!prepare_fragments(renderer)) {
            return false;
        }
        for (int i = 0; i < acount(renderer->entities); i++) {
            render_shader_fragment_draw(
                renderer->fragment,
                renderer->fragment_programs[i],
                &renderer->entities[i],
                flat
            );
        }
        return render_fog_draw(
            renderer->fog,
            engine_visibility(runtime),
            &camera,
            NULL,
            renderer->time
        );
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
    collect(renderer, world, particles, runtime);
    render_material_view view = { camera.center_x - camera.span_width * 0.5f,
                                  camera.center_y + camera.span_height * 0.5f,
                                  camera.span_width,
                                  camera.span_height };
    bool drawn = render_material_raster_draw(
        renderer->raster,
        renderer->entities,
        acount(renderer->entities),
        view
    );
    snprintf(
        renderer->error,
        sizeof renderer->error,
        "%s",
        render_material_raster_error(renderer->raster)
    );
    if (!drawn) {
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
    return render_fog_draw(
        renderer->fog,
        engine_visibility(runtime),
        &camera,
        NULL,
        renderer->time
    );
}

bool app_scene_renderer_preview(
    app_scene_renderer *renderer,
    const behavior_asset *asset,
    float time,
    int width,
    int height
)
{
    shader_error error = { 0 };
    if (!renderer || !asset) {
        return false;
    }
    renderer->time = time;
    renderer->error[0] = 0;
    if (asset->domain == BEHAVIOR_DOMAIN_MATERIAL) {
        if (!arena_ready(renderer)) {
            return false;
        }
        CF_V3 corners[8];
        for (int i = 0; i < 8; i++) {
            corners[i] = (CF_V3){ i & 1 ? 50 : -50, i & 2 ? 50 : -50, i & 4 ? 50 : -50 };
        }
        CF_Aabb bounds = render_arena_fit(corners, 8, (float)width / height);
        render_arena_camera camera = render_arena_camera_make(
            (bounds.min.x + bounds.max.x) * .5f,
            (bounds.min.y + bounds.max.y) * .5f,
            bounds.max.x - bounds.min.x,
            bounds.max.y - bounds.min.y
        );
        GameEnvironment settings = render_arena_post_defaults();
        if (!arena_prepare(renderer, width, height, &settings)) {
            return false;
        }
        return arena_finish(renderer, render_arena_preview(renderer->arena, asset, &camera, NULL, time));
    }
    if (!render_shader_upsert(asset, &error)) {
        snprintf(renderer->error, sizeof renderer->error, "%s", error.message);
    }
    int id = render_shader_find(asset->name);
    if (!id) {
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
    if (!prepare_fragments(renderer)) {
        return false;
    }
    render_shader_fragment_draw(
        renderer->fragment,
        renderer->fragment_programs[0],
        draw,
        (CF_V2){ 1, 1 }
    );
    return true;
}

void app_scene_renderer_set_parallel(app_scene_renderer *renderer, bool enabled)
{
    renderer->parallel_disabled = !enabled;
    if (renderer->arena) {
        render_arena_set_parallel(renderer->arena, enabled);
    }
}
render_arena_work app_scene_renderer_last_work(const app_scene_renderer *renderer)
{
    return renderer->arena ? render_arena_last_work(renderer->arena)
                           : (render_arena_work){ 0 };
}
