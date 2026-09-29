#include "render/scene_renderer.h"

#include "diagnostics/tracy.h"
#include "platform/cute.h"
#include "render/appearance_geometry.h"
#include "render/arena_post.h"
#include "render/fluid_raster.h"
#include "render/fog.h"
#include "render/geometry_2d.h"
#include "render/gpu_clock.h"
#include "render/gpu_timing.h"
#include "render/material.h"
#include "render/material_raster.h"
#include "render/post_2d.h"
#include "render/post_2d_scene.h"
#include "render/radiance.h"
#include "render/reflections.h"
#include "render/sdf_motion.h"
#include "render/shader_catalog.h"
#include "render/shader_dir.h"
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

typedef struct
{
    ecs_entity entity;
    int layer;
} draw_actor;

struct app_scene_renderer
{
    render_arena *arena;
    render_arena_post *post;
    render_fog *fog;
    bool arena_attempted, parallel_disabled;
    render_material_raster *raster;
    render_fluid_raster *fluid;
    render_radiance *radiance;
    /* HDR presentation for tone-mapped 2D scenes. */
    render_post_2d *hdr;
    GameHRC2D lighting;
    render_reflections *reflections;
    render_reflection *reflectors;
    CF_Canvas output, background;
    CF_Shader background_shader;
    GameStarfield2D background_style;
    engine_viewport_camera background_camera;
    bool background_valid, gpu_clock;
    /* The scene's GPU work each frame, for adaptive lighting resolution. */
    render_gpu_span span;
    /* Without GPU timestamps, the previous frame's start in ticks. */
    uint64_t frame_start;
    /* Tile groups numbered so far this frame. */
    int tile_groups;
    render_material_entity *entities;
    draw_actor *actors;
    ecs_world *world;
    engine_viewport_camera camera;
    int width, height;
    int raster_width, raster_height;
    float pixel_aa, time, particle_focal_length;
    render_programs *fragment;
    int *fragment_programs;
    char error[256];
    /* Lighting settings adjusted to what HRC supports, for authors. */
    char warning[512];
};

void app_scene_renderer_initialize()
{
    char message[256];
    if (!render_image_mount_content("content", message, sizeof message)) {
        fprintf(stderr, "%s\n", message);
    }
    cf_fs_mount(render_shader_dir(), "/chibi-render", true);
    cf_shader_directory("/chibi-render");
}

app_scene_renderer *app_scene_renderer_new()
{
    app_scene_renderer *renderer = xcalloc(1, sizeof *renderer);
    renderer->fog = render_fog_new();
    renderer->raster = render_material_raster_new();
    renderer->radiance = render_radiance_new();
    /* Adaptive lighting resolution times lighting with GPU timestamps. */
    renderer->gpu_clock = cf_query_backend() == CF_BACKEND_TYPE_METAL &&
                          render_gpu_clock_install();
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
    render_fluid_raster_free(renderer->fluid);
    render_arena_free(renderer->arena);
    render_arena_post_free(renderer->post);
    render_post_2d_free(renderer->hdr);
    render_fog_free(renderer->fog);
    render_radiance_free(renderer->radiance);
    if (renderer->gpu_clock) {
        render_gpu_clock_release();
    }
    render_reflections_free(renderer->reflections);
    afree(renderer->reflectors);
    if (renderer->output.id) {
        cf_destroy_canvas(renderer->output);
    }
    if (renderer->background.id) {
        cf_destroy_canvas(renderer->background);
    }
    if (renderer->background_shader.id) {
        cf_destroy_shader(renderer->background_shader);
    }
    render_programs_free(renderer->fragment);
    afree(renderer->fragment_programs);
    afree(renderer->entities);
    afree(renderer->actors);
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

const char *app_scene_renderer_warning(const app_scene_renderer *renderer)
{
    return renderer ? renderer->warning : "";
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

bool app_scene_renderer_lighting_status(const app_scene_renderer *renderer, render_radiance_status *status)
{
    if (!renderer || !renderer->radiance) {
        return false;
    }
    *status = render_radiance_get_status(renderer->radiance);
    return true;
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
    memcpy(entity->absorption, material->absorption, sizeof entity->absorption);
    memcpy(entity->albedo, material->albedo, sizeof entity->albedo);
    memcpy(entity->border_scene, material->border_scene, sizeof entity->border_scene);
    memcpy(entity->border_emission, material->border_emission, sizeof entity->border_emission);
    memcpy(entity->border_absorption, material->border_absorption, sizeof entity->border_absorption);
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
                              .albedo = appearance->color,
                              .emission_color = appearance->border_color,
                              .border_scene = appearance->border_color,
                              .border_albedo = appearance->border_color,
                              .border_emission_color = appearance->border_color,
                              .emission_intensity = 0.035f,
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
        .reserved = { 0, 0, material ? material->edge_light : 0 },
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
        case GAME_SHAPE_STROKED_CHEVRON:
        case GAME_SHAPE_TRIANGLE:
        case GAME_SHAPE_STROKED_TRIANGLE: {
            CF_V2 points[4];
            int count = game_appearance_points(appearance, points);
            entity.point_count = (unsigned)count;
            entity.shape_type = appearance->shape == GAME_SHAPE_STROKED_CHEVRON ||
                                        appearance->shape == GAME_SHAPE_STROKED_TRIANGLE
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

static void reflect_rect(
    app_scene_renderer *renderer,
    material_rect rect,
    GameTransform origin,
    float heading,
    GameReflection style,
    const GameMaterial *material
)
{
    if (!isfinite(style.strength + style.roughness + style.angle + style.offset) ||
        style.strength <= 0) {
        return;
    }
    float c = cosf(heading), s = sinf(heading);
    float t = -heading - style.angle, tx = cosf(t), ty = sinf(t);
    render_reflection r = {
        .plane = { origin.x - ty * style.offset, -origin.y + tx * style.offset, tx, ty },
        .strength = fminf(style.strength, 1),
        .roughness = fmaxf(0, fminf(style.roughness, 1)),
        .tint = { 1, 1, 1, 1 }
    };
    if (material) {
        for (int k = 0; k < 3; k++) {
            r.tint[k] = .5f + .5f * ((material->albedo >> (24 - 8 * k)) & 255) / 255.f;
        }
    }
    float x[] = { rect.x - rect.width * .5f,
                  rect.x + rect.width * .5f,
                  rect.x + rect.width * .5f,
                  rect.x - rect.width * .5f };
    float y[] = { rect.y - rect.height * .5f,
                  rect.y - rect.height * .5f,
                  rect.y + rect.height * .5f,
                  rect.y + rect.height * .5f };
    for (int i = 0; i < 4; i++) {
        r.corners[i] = (CF_V2){ origin.x + c * x[i] - s * y[i],
                                -origin.y - s * x[i] - c * y[i] };
    }
    apush(renderer->reflectors, r);
}

static bool edge_cell(const material_mask *mask, int reach, int x, int y)
{
    const unsigned char *cells = material_mask_occupancy(mask).cells;
    if (!cells[y * mask->width + x]) {
        return false;
    }
    for (int d = 1; d <= reach; d++) {
        if (x < d || y < d || x + d >= mask->width || y + d >= mask->height ||
            !cells[y * mask->width + x - d] || !cells[y * mask->width + x + d] ||
            !cells[(y - d) * mask->width + x] || !cells[(y + d) * mask->width + x]) {
            return true;
        }
    }
    return false;
}

/* One box per row run of cells within the border width of the mask's edge. */
static void mask_edges(
    app_scene_renderer *renderer,
    const material_mask *mask,
    GameTransform position,
    float heading,
    const GameAppearance *look,
    const GameMaterial *material
)
{
    if (!material || look->border_width <= 0) {
        return;
    }
    GameMaterial edge = *material;
    edge.scene = material->border_scene;
    edge.albedo = material->border_albedo;
    edge.filter = material->border_filter;
    edge.emission_color = material->border_emission_color;
    int reach = (int)ceilf(look->border_width / mask->cell_size);
    float c = cosf(heading), s = sinf(heading);
    for (int y = 0; y < mask->height; y++) {
        for (int x = 0; x < mask->width; x++) {
            if (!edge_cell(mask, reach, x, y)) {
                continue;
            }
            int run = 1;
            while (x + run < mask->width && edge_cell(mask, reach, x + run, y)) {
                run++;
            }
            float px = (x + run * .5f - mask->width * .5f) * mask->cell_size;
            float py = (y + .5f - mask->height * .5f) * mask->cell_size;
            GameTransform p = { position.x + c * px - s * py,
                                position.y + s * px + c * py };
            GameAppearance cells = { .shape = GAME_SHAPE_BOX,
                                     .width = run * mask->cell_size,
                                     .height = mask->cell_size,
                                     .antialias = .1f,
                                     .color = look->border_color };
            append_entity(renderer, &p, &cells, -heading, NULL, &edge);
            x += run - 1;
        }
    }
}

/* Marks the entities appended since first as the pieces of a tile group, and
 * pads their bounds for the pixel footprint their coverage integrates. */
static void tile_group(app_scene_renderer *renderer, int first, float group)
{
    for (int i = first; i < acount(renderer->entities); i++) {
        renderer->entities[i].tile_group = group;
        renderer->entities[i].extent[0] += 2 * renderer->pixel_aa;
        renderer->entities[i].extent[1] += 2 * renderer->pixel_aa;
    }
}

/* Marching squares cuts a piece from its bounding rectangle with at most one
 * line. Writes that line in the piece's frame, Y up about the rectangle's
 * center, as a box cut (see render_material_entity); false for polygons
 * that need more than one. */
static bool polygon_cut(const material_polygon *polygon, material_rect rect, float cut[4])
{
    int count = polygon->count, cuts = 0;
    float cx = 0, cy = 0;
    for (int i = 0; i < count; i++) {
        cx += (polygon->points[2 * i] - rect.x) / count;
        cy += (rect.y - polygon->points[2 * i + 1]) / count;
    }
    for (int i = 0; i < count; i++) {
        int j = (i + 1) % count;
        float ax = polygon->points[2 * i] - rect.x,
              ay = rect.y - polygon->points[2 * i + 1];
        float bx = polygon->points[2 * j] - rect.x,
              by = rect.y - polygon->points[2 * j + 1];
        if (ax == bx || ay == by) {
            continue;
        }
        float nx = by - ay, ny = ax - bx, length = hypotf(nx, ny);
        float side = nx * (cx - ax) + ny * (cy - ay) > 0 ? -1 : 1;
        cut[0] = side * nx / length;
        cut[1] = side * ny / length;
        cut[2] = cut[0] * ax + cut[1] * ay;
        cut[3] = 1;
        cuts++;
    }
    return cuts <= 1;
}

static bool mask_visible(
    render_material_view view,
    GameTransform origin,
    material_rect rect,
    float heading,
    float padding
)
{
    float c = cosf(heading), s = sinf(heading);
    float x = origin.x + c * rect.x - s * rect.y;
    float y = -origin.y - s * rect.x - c * rect.y;
    float rx = (fabsf(c) * rect.width + fabsf(s) * rect.height) * .5f + padding;
    float ry = (fabsf(s) * rect.width + fabsf(c) * rect.height) * .5f + padding;
    return x + rx >= view.min_x && x - rx <= view.min_x + view.width &&
           y + ry >= view.max_y - view.height && y - ry <= view.max_y;
}

static int compare_actors(const void *left, const void *right)
{
    const draw_actor *a = left, *b = right;
    if (a->layer != b->layer) {
        return (a->layer > b->layer) - (a->layer < b->layer);
    }
    return (a->entity.slot > b->entity.slot) - (a->entity.slot < b->entity.slot);
}

static void collect_order(app_scene_renderer *renderer, ecs_world *world)
{
    aclear(renderer->actors);
    ecs_component_id component = GAME_COMPONENT_GameTransform.id;
    ecs_query query;
    if (ecs_query_begin(world, &query, &component, 1) != ECS_OK) {
        return;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const GameAppearance *appearance = GAME_GameAppearance_get_const(world, entity);
        apush(renderer->actors, ((draw_actor){ entity, appearance ? appearance->layer : 0 }));
    }
    ecs_query_end(&query);
    if (acount(renderer->actors) > 1) {
        qsort(
            renderer->actors,
            (size_t)acount(renderer->actors),
            sizeof *renderer->actors,
            compare_actors
        );
    }
}

/* Crater chunks draw as small rock polygons in their source's material, with
 * the pattern of the rock they left. */
static void collect_chunks(
    app_scene_renderer *renderer,
    ecs_world *world,
    const engine *runtime,
    render_material_view view
)
{
    int count;
    const material_chunk *chunks = game_materials_chunks(engine_materials(runtime), &count);
    uint64_t cached = 0;
    const GameMaterial *material = NULL;
    const GameAppearance *look = NULL;
    const MaterialBreakProfile *profile = NULL;
    int shader = 0;
    float parameters[32][4] = { 0 };
    for (int i = 0; i < count; i++) {
        const material_chunk *chunk = &chunks[i];
        float scale = material_chunk_scale(chunk), radius = chunk->radius * scale;
        if (radius <= .05f || chunk->position.x + radius < view.min_x ||
            chunk->position.x - radius > view.min_x + view.width ||
            -chunk->position.y + radius < view.max_y - view.height ||
            -chunk->position.y - radius > view.max_y) {
            continue;
        }
        if (chunk->source != cached) {
            cached = chunk->source;
            ecs_entity source = ecs_entity_from_u64(chunk->source);
            bool alive = ecs_entity_is_alive(world, source);
            material = alive ? GAME_GameMaterial_get_const(world, source) : NULL;
            look = alive ? GAME_GameAppearance_get_const(world, source) : NULL;
            profile = alive ? GAME_MaterialBreakProfile_get_const(world, source) : NULL;
            const GameShader *binding = alive ? GAME_GameShader_get_const(world, source)
                                              : NULL;
            shader = binding ? render_shader_find(binding->material) : 0;
            if (shader && render_shader_is_shape(shader)) {
                shader = 0;
            }
            if (shader) {
                render_shader_parameters(shader, world, source, binding->parameters, parameters);
            }
        }
        GameTransform position = { chunk->position.x, chunk->position.y };
        GameAppearance part = { .width = 2 * radius,
                                .height = 2 * radius,
                                .shape = GAME_SHAPE_BOX,
                                .color = look ? look->color : 0x4a4744ffu,
                                .antialias = look ? look->antialias : 0 };
        /* A hot chunk glows in its material's spark color until it cools
         * enough to show the material's pattern. */
        bool hot = profile && chunk->heat > .05f;
        GameMaterial glow = material ? *material : (GameMaterial){ 0 };
        if (hot) {
            glow.emission_color = profile->spark_color;
            glow.emission_intensity = chunk->heat * profile->spark_emission;
            glow.emission_multiplier = 1;
        }
        if (!append_entity(renderer, &position, &part, -chunk->angle, NULL, hot ? &glow : material)) {
            continue;
        }
        render_material_entity *draw = &alast(renderer->entities);
        draw->shape_type = RENDER_SHAPE_POLY_FILL;
        draw->point_count = (uint32_t)chunk->count;
        for (int k = 0; k < chunk->count; k++) {
            draw->points[2 * k] = chunk->points[2 * k] * scale;
            draw->points[2 * k + 1] = -chunk->points[2 * k + 1] * scale;
        }
        draw->reserved[0] = hot ? 0 : (float)shader;
        memcpy(draw->material_parameters, parameters, sizeof parameters);
        /* The pattern stays where the chunk was carved and turns with it. */
        draw->material_frame[0] = chunk->origin.x;
        draw->material_frame[1] = -chunk->origin.y;
        draw->material_frame[2] = 1;
        draw->material_frame[3] = 0;
    }
}

static void collect(
    app_scene_renderer *renderer,
    ecs_world *world,
    const game_particles *particles,
    const engine *runtime,
    render_material_view view
)
{
    asetlen(renderer->entities, 0);
    renderer->tile_groups = 0;
    aclear(renderer->reflectors);
    collect_order(renderer, world);
    for (int actor = 0; actor < acount(renderer->actors); actor++) {
        ecs_entity entity = renderer->actors[actor].entity;
        if (!engine_entity_visible(runtime, world, entity)) {
            continue;
        }
        const GameOrientation *orientation = GAME_GameOrientation_get_const(world, entity);
        const GameShader *binding = GAME_GameShader_get_const(world, entity);
        const GameAppearance *authored = GAME_GameAppearance_get_const(world, entity);
        const GameGeometry3D *geometry = GAME_GameGeometry3D_get_const(world, entity);
        if (!authored && !geometry) {
            continue;
        }
        GameAppearance fallback = authored
                                    ? *authored
                                    : (GameAppearance){ .width = geometry->width,
                                                        .height = geometry->depth,
                                                        .color = 0xffffffffu,
                                                        .shape = GAME_SHAPE_BOX,
                                                        .antialias = 1 };
        const GameVisualTransform *visual = GAME_GameVisualTransform_get_const(world, entity);
        float heading = orientation ? orientation->radians : 0;
        GameAppearance appearance;
        float angle;
        if (!render_geometry_2d_project(authored ? NULL : geometry, &fallback, visual, heading, &appearance, &angle)) {
            continue;
        }
        int shape = !authored && binding ? render_shader_find(binding->shape) : 0;
        float bounds[2];
        if (!authored && render_shader_bounds(shape, bounds)) {
            float scale = visual ? visual->scale : 1;
            appearance.shape = GAME_SHAPE_BOX;
            appearance.width = 2 *
                               (binding->bounds_x > 0 ? binding->bounds_x : bounds[0]) *
                               scale;
            appearance.height = 2 *
                                (binding->bounds_y > 0 ? binding->bounds_y
                                                       : bounds[1]) *
                                scale;
        }
        GameTransform position = *GAME_GameTransform_get_const(world, entity);
        if (runtime) {
            engine_render_position(runtime, entity, &position);
        }
        const material_mask *mask = game_materials_mask(engine_materials(runtime), entity);
        if (mask) {
            float padding = fmaxf(appearance.antialias, renderer->pixel_aa);
            material_rect bounds = { 0,
                                     0,
                                     mask->width * mask->cell_size,
                                     mask->height * mask->cell_size };
            if (!mask_visible(view, position, bounds, heading, padding)) {
                continue;
            }
            int shader = binding ? render_shader_find(binding->material) : 0;
            float parameters[32][4] = { 0 };
            if (shader && !render_shader_is_shape(shader)) {
                render_shader_parameters(shader, world, entity, binding->parameters, parameters);
            }
            const GameMaterialFrame *frame = GAME_GameMaterialFrame_get_const(world, entity);
            const GameReflection *reflection = GAME_GameReflection_get_const(world, entity);
            if (reflection) {
                for (int i = 0; i < material_mask_collision(mask).count; i++) {
                    reflect_rect(
                        renderer,
                        material_mask_collision(mask).rects[i],
                        position,
                        heading,
                        *reflection,
                        GAME_GameMaterial_get_const(world, entity)
                    );
                }
            }
            /* The border comes first as the group's half, which the raster
             * lays over the fill that follows. */
            float group = (float)++renderer->tile_groups;
            int first = acount(renderer->entities);
            mask_edges(renderer, mask, position, heading, &appearance, GAME_GameMaterial_get_const(world, entity));
            tile_group(renderer, first, group + .5f);
            first = acount(renderer->entities);
            for (int i = 0; i < material_mask_surface(mask).count; i++) {
                const material_polygon *polygon = &material_mask_surface(mask).polygons[i];
                float min_x = polygon->points[0], max_x = min_x;
                float min_y = polygon->points[1], max_y = min_y;
                for (int j = 1; j < polygon->count; j++) {
                    min_x = fminf(min_x, polygon->points[2 * j]);
                    max_x = fmaxf(max_x, polygon->points[2 * j]);
                    min_y = fminf(min_y, polygon->points[2 * j + 1]);
                    max_y = fmaxf(max_y, polygon->points[2 * j + 1]);
                }
                material_rect rect = { (min_x + max_x) * .5f,
                                       (min_y + max_y) * .5f,
                                       max_x - min_x,
                                       max_y - min_y };
                if (!mask_visible(view, position, rect, heading, padding)) {
                    continue;
                }
                GameTransform p = {
                    position.x + cosf(heading) * rect.x - sinf(heading) * rect.y,
                    position.y + sinf(heading) * rect.x + cosf(heading) * rect.y
                };
                GameAppearance part = { .width = rect.width,
                                        .height = rect.height,
                                        .shape = GAME_SHAPE_BOX,
                                        .color = appearance.color,
                                        .antialias = appearance.antialias };
                if (!append_entity(renderer, &p, &part, -heading, NULL, GAME_GameMaterial_get_const(world, entity))) {
                    continue;
                }
                render_material_entity *draw = &alast(renderer->entities);
                if (!polygon_cut(polygon, rect, draw->points + 4)) {
                    draw->shape_type = RENDER_SHAPE_POLY_FILL;
                    draw->point_count = polygon->count;
                    for (int j = 0; j < polygon->count; j++) {
                        draw->points[2 * j] = polygon->points[2 * j] - rect.x;
                        draw->points[2 * j + 1] = rect.y - polygon->points[2 * j + 1];
                    }
                }
                draw->reserved[0] = render_shader_is_shape(shader) ? 0 : (float)shader;
                memcpy(draw->material_parameters, parameters, sizeof parameters);
                if (frame) {
                    /* The piece's centre and axes where its material was laid out. */
                    float c = cosf(frame->radians), s = sinf(frame->radians);
                    draw->material_frame[0] = frame->x + c * rect.x - s * rect.y;
                    draw->material_frame[1] = -(frame->y + s * rect.x + c * rect.y);
                    draw->material_frame[2] = c;
                    draw->material_frame[3] = -s;
                }
            }
            tile_group(renderer, first, group);
            continue;
        }
        if (!append_entity(
                renderer,
                &position,
                &appearance,
                angle,
                GAME_GameSdfMotion_get_const(world, entity),
                GAME_GameMaterial_get_const(world, entity)
            )) {
            continue;
        }
        /* Appearance owns the 2D footprint; its shader still owns the
         * material channels used by the SDF raster and HRC inputs. */
        if (binding) {
            render_material_entity *draw = &renderer->entities[acount(renderer->entities) - 1];
            int material = render_shader_find(binding->material);
            draw->reserved[0] = render_shader_is_shape(material) ? 0 : (float)material;
            draw->reserved[1] = !authored && render_shader_is_shape(shape)
                                  ? (float)shape
                                  : 0;
            render_shader_parameters(material, world, entity, binding->parameters, draw->material_parameters);
            if (!authored) {
                render_shader_parameters(shape, world, entity, binding->parameters, draw->shape_parameters);
            }
        }
    }
    collect_chunks(renderer, world, runtime, view);
    const game_particle *items = game_particles_data(particles);
    for (int i = 0; i < game_particles_count(particles); i++) {
        const game_particle *particle = &items[i];
        float life = 1 - particle->age / particle->lifetime;
        if (particle->size > 0) {
            float age = 1 - life;
            float diameter = 2 * game_particle_curve_value(
                                     particle->size,
                                     particle->size_end,
                                     particle->size_exponent,
                                     age
                                 );
            bool ring = particle->shape == GAME_PARTICLE_RING;
            bool triangle = particle->shape == GAME_PARTICLE_TRIANGLE ||
                            particle->shape == GAME_PARTICLE_TRIANGLE_OUTLINE;
            if (ring) {
                diameter = 2 * particle->size * (.45f + .55f * age);
            }
            unsigned color = particle_color(particle->color, particle->end_color, age);
            float length = particle->stretch *
                           hypotf(particle->velocity.x, particle->velocity.y);
            float focal = renderer->particle_focal_length;
            float perspective = focal / fmaxf(1, focal + particle->elevation);
            diameter *= perspective;
            length *= perspective;
            GameAppearance appearance = {
                .width = diameter +
                         (particle->shape == GAME_PARTICLE_STREAK ? length : 0),
                .height = diameter,
                .color = ring ? color & 0xffffff00u : color,
                .border_color = color,
                .border_width = ring ? diameter * .07f : 0,
                .shape = particle->shape == GAME_PARTICLE_STREAK ? GAME_SHAPE_CAPSULE
                       : triangle ? GAME_SHAPE_BOX
                                  : GAME_SHAPE_CIRCLE,
                .antialias = game_particle_curve_value(
                    particle->antialias,
                    particle->antialias_end,
                    particle->antialias_exponent,
                    age
                )
            };
            /* Particles are matter: they reflect received light as well as
             * emitting their own. */
            GameMaterial material = { .scene = appearance.color,
                                      .albedo = appearance.color,
                                      .emission_color = appearance.color,
                                      .border_scene = color,
                                      .border_emission_color = color,
                                      .emission_intensity = game_particle_curve_value(
                                          particle->emission,
                                          particle->emission_end,
                                          particle->emission_exponent,
                                          age
                                      ),
                                      .emission_multiplier = 1,
                                      .absorption = particle->absorption };
            GameTransform position = { particle->position.x, particle->position.y };
            if (diameter <= 0) {
                continue;
            }
            if (!append_entity(renderer, &position, &appearance, -particle->angle, NULL, &material)) {
                continue;
            }
            render_material_entity
                *particle_draw = &renderer->entities[acount(renderer->entities) - 1];
            particle_draw->metaball_group = particle->metaball_group;
            particle_draw->metaball_blend = particle->metaball_blend;
            particle_draw->extent[0] += particle->metaball_blend;
            particle_draw->extent[1] += particle->metaball_blend;
            if (triangle) {
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

static render_view_2d padded_view(
    const engine_viewport_camera *camera,
    int output_width,
    int output_height,
    int pad_pixels
)
{
    float pixels_x = output_width / camera->span_width;
    float pixels_y = output_height / camera->span_height;
    float width = (output_width + 2 * pad_pixels) / pixels_x;
    float height = (output_height + 2 * pad_pixels) / pixels_y;
    return (render_view_2d){
        .min_x = camera->center_x - width * 0.5f,
        .max_y = camera->center_y + height * 0.5f,
        .width = width,
        .height = height,
        .crop_min_u = pad_pixels / (float)(output_width + 2 * pad_pixels),
        .crop_min_v = pad_pixels / (float)(output_height + 2 * pad_pixels),
        .crop_max_u = (output_width + pad_pixels) / (float)(output_width + 2 * pad_pixels),
        .crop_max_v = (output_height + pad_pixels) / (float)(output_height + 2 * pad_pixels)
    };
}

static bool scene_lighting(app_scene_renderer *renderer, ecs_world *world, CF_V2 *flat)
{
    GameHRC2D lighting = renderer->radiance
                           ? game_hrc2d_defaults()
                           : (GameHRC2D){ .ambient = 1, .intensity = 1 };
    ecs_component_id component = GAME_COMPONENT_GameHRC2D.id;
    ecs_query query;
    int settings = 0;
    if (ecs_world_component(world, component) &&
        ecs_query_begin(world, &query, &component, 1) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            if (!ecs_entity_is_retiring(world, entity) && settings++ == 0) {
                lighting = *GAME_GameHRC2D_get_const(world, entity);
            }
        }
        ecs_query_end(&query);
    }
    char notes[400] = "";
    if (settings > 0) {
        lighting = render_radiance_normalize(lighting, notes, sizeof notes);
    }
    renderer->warning[0] = '\0';
    if (settings > 1) {
        snprintf(
            renderer->warning,
            sizeof renderer->warning,
            "Lighting uses the first of %d GameHRC2D entities. ",
            settings
        );
    }
    if (notes[0]) {
        size_t used = strlen(renderer->warning);
        snprintf(renderer->warning + used, sizeof renderer->warning - used, "Lighting adjusted: %s.", notes);
    }
    renderer->particle_focal_length = lighting.particle_focal_length;
    *flat = (CF_V2){ lighting.ambient, lighting.intensity };
    renderer->lighting = lighting;
    if (renderer->radiance &&
        !render_radiance_set_lighting(renderer->radiance, lighting)) {
        snprintf(renderer->error, sizeof renderer->error, "HRC could not allocate its lighting resources.");
        return false;
    }
    return true;
}

static CF_Texture scene_background(
    app_scene_renderer *renderer,
    ecs_world *world,
    const engine_viewport_camera *camera
)
{
    GameStarfield2D style = { 0 };
    ecs_component_id component = GAME_COMPONENT_GameStarfield2D.id;
    if (!ecs_world_component(world, component)) {
        return (CF_Texture){ 0 };
    }
    ecs_query query;
    int result = ecs_query_begin(world, &query, &component, 1);
    assert(result == ECS_OK);
    ecs_entity entity;
    bool found = ecs_query_next(&query, &entity);
    if (found) {
        style = *GAME_GameStarfield2D_get_const(world, entity);
    }
    ecs_query_end(&query);
    if (!found) {
        return (CF_Texture){ 0 };
    }
    if (!renderer->background.id) {
        renderer->background = cf_make_canvas(
            cf_canvas_defaults(renderer->width, renderer->height)
        );
        renderer->background_valid = false;
    }
    if (!renderer->background_shader.id) {
        renderer->background_shader = cf_make_draw_shader("starfield.shd");
    }
    if (renderer->background_valid &&
        !memcmp(&style, &renderer->background_style, sizeof style) &&
        !memcmp(camera, &renderer->background_camera, sizeof *camera)) {
        return cf_canvas_get_target(renderer->background);
    }
    renderer->background_style = style;
    renderer->background_camera = *camera;
    renderer->background_valid = true;
    CF_V2 center = { camera->center_x, camera->center_y };
    CF_V2 span = { camera->span_width, camera->span_height };
    cf_draw_push_shader(renderer->background_shader);
    cf_draw_set_uniform("camera_pos", &center, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_draw_set_uniform("world_size", &span, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_draw_set_uniform("far_layer", &style.far_layer, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("middle_layer", &style.middle_layer, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("near_layer", &style.near_layer, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("parallax", &style.parallax, CF_UNIFORM_TYPE_FLOAT3, 1);
    const unsigned colors[] = { style.color_a,
                                style.color_b,
                                style.color_c,
                                style.color_d };
    const char *names[] = { "palette_a", "palette_b", "palette_c", "palette_d" };
    for (int i = 0; i < 4; i++) {
        float rgba[] = { (colors[i] >> 24) / 255.f,
                         ((colors[i] >> 16) & 255) / 255.f,
                         ((colors[i] >> 8) & 255) / 255.f,
                         (colors[i] & 255) / 255.f };
        cf_draw_set_uniform(names[i], rgba, CF_UNIFORM_TYPE_FLOAT4, 1);
    }
    CF_Aabb screen = { .min = { center.x - span.x * .5f, center.y - span.y * .5f },
                       .max = { center.x + span.x * .5f, center.y + span.y * .5f } };
    cf_draw_quad_fill(screen, 0);
    cf_draw_pop_shader();
    cf_render_to(renderer->background, true);
    return cf_canvas_get_target(renderer->background);
}

/* Distorts, grades and tone maps the linear frame into the output. */
static bool present_hdr(
    app_scene_renderer *renderer,
    ecs_world *world,
    const engine *runtime,
    render_view_2d screen,
    render_material_textures textures
)
{
    render_post_2d_settings settings = { .exposure = renderer->lighting.exposure,
                                         .bloom = renderer->lighting.bloom,
                                         .dither_phase = fmodf(renderer->time * 60, 64) };
    render_post_2d_distortion distortion = { .normal = textures.normal,
                                             .cover = textures.cover,
                                             .time = renderer->time };
    render_post_2d_collect(world, runtime, screen, &settings, &distortion);
    if (!renderer->fluid || !render_fluid_raster_hot(renderer->fluid)) {
        distortion.fluid_haze = 0;
    }
    if (render_post_2d_distorts(&distortion)) {
        if (!render_post_2d_distort(renderer->hdr, &distortion)) {
            snprintf(renderer->error, sizeof renderer->error, "Screen distortion could not allocate its RGBA16F canvas");
            return false;
        }
        render_gpu_timing_mark("Distortion");
    }
    render_post_2d_present(renderer->hdr, renderer->output, &settings);
    render_gpu_timing_mark("Tone map and bloom");
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
    double gpu_ms;
    while (renderer->radiance && render_gpu_span_take(&renderer->span, &gpu_ms)) {
        render_radiance_frame_time(renderer->radiance, gpu_ms);
    }
    /* Without GPU timestamps, as on the web, adaptive lighting judges the
     * whole frame's interval, which includes the rest of the frame's work. */
    if (renderer->radiance && !render_gpu_span_available()) {
        uint64_t now = cf_get_ticks();
        if (renderer->frame_start) {
            double ms = (double)(now - renderer->frame_start) * 1000 /
                        (double)cf_get_tick_frequency();
            render_radiance_frame_time(renderer->radiance, ms);
        }
        renderer->frame_start = now;
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
    if (renderer->world != world || renderer->camera.span_width != camera.span_width ||
        renderer->camera.span_height != camera.span_height) {
        app_scene_renderer_reset(renderer);
        renderer->world = world;
    }
    renderer->camera = camera;
    CF_V2 flat;
    if (!scene_lighting(renderer, world, &flat)) {
        return false;
    }
    if (!renderer->raster) {
        /* This is a supported rendering path, not a scene error. Its status is
         * available in the renderer diagnostics without covering the game. */
        renderer->error[0] = '\0';
        renderer->pixel_aa = fmaxf(camera.span_width / width, camera.span_height / height) *
                             0.5f;
        collect(renderer, world, particles, runtime, padded_view(&camera, width, height, 0));
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
    render_view_2d_frame frame;
    render_radiance_settings radiance = render_radiance_get_settings(renderer->radiance);
    render_view_2d_camera camera_view = {
        .min_x = camera.center_x - camera.span_width * .5f,
        .max_y = camera.center_y + camera.span_height * .5f,
        .width = camera.span_width,
        .height = camera.span_height,
        .output_width = width,
        .output_height = height,
    };
    render_view_2d_settings padding = {
        .margin = renderer->lighting.lighting_margin,
        .work_scale = radiance.scale,
        .min_work_scale = radiance.min_scale,
        .max_raster_size = 4096,
    };
    if (!render_view_2d_world_margin(camera_view, padding, &frame)) {
        snprintf(renderer->error, sizeof renderer->error, "HRC view cannot fit within the 4096-pixel raster limit");
        return false;
    }
    int raster_width = frame.raster_width;
    int raster_height = frame.raster_height;
    if (renderer->width != width || renderer->height != height ||
        renderer->raster_width != raster_width ||
        renderer->raster_height != raster_height) {
        if (!render_material_raster_resize(renderer->raster, raster_width, raster_height) ||
            !render_radiance_resize(renderer->radiance, raster_width, raster_height)) {
            return false;
        }
        CF_Canvas output = cf_make_canvas(cf_canvas_defaults(width, height));
        if (!output.id) {
            return false;
        }
        if (renderer->output.id) {
            cf_destroy_canvas(renderer->output);
        }
        if (renderer->background.id) {
            cf_destroy_canvas(renderer->background);
        }
        renderer->background = (CF_Canvas){ 0 };
        renderer->output = output;
        renderer->width = width;
        renderer->height = height;
        renderer->raster_width = raster_width;
        renderer->raster_height = raster_height;
    }
    renderer->pixel_aa = fmaxf(camera.span_width / width, camera.span_height / height) *
                         0.5f;
    render_material_view view = frame.view;
    CHIBI_TRACE_ZONE_BEGIN(collect_zone, "Material collect");
    collect(renderer, world, particles, runtime, view);
    CHIBI_TRACE_ZONE_END(collect_zone);
    CHIBI_TRACE_ZONE_BEGIN(raster_zone, "Material raster submission");
    render_gpu_span_begin(&renderer->span);
    render_gpu_timing_start();
    bool drawn = render_material_raster_draw(
        renderer->raster,
        renderer->entities,
        acount(renderer->entities),
        view
    );
    CHIBI_TRACE_ZONE_END(raster_zone);
    snprintf(
        renderer->error,
        sizeof renderer->error,
        "%s",
        render_material_raster_error(renderer->raster)
    );
    if (!drawn) {
        return false;
    }
    render_gpu_timing_mark("Material raster");
    render_material_textures textures = render_material_raster_textures(renderer->raster);
    const game_materials *materials = engine_materials(runtime);
    if (materials && game_materials_fluid(materials)) {
        if (!renderer->fluid) {
            renderer->fluid = render_fluid_raster_new();
        }
        if (!renderer->fluid || !render_fluid_raster_draw(
                                    renderer->fluid,
                                    game_materials_fluid(materials),
                                    render_material_raster_canvas(renderer->raster),
                                    view,
                                    raster_width,
                                    raster_height
                                )) {
            snprintf(renderer->error, sizeof renderer->error, "Fluid raster could not allocate or draw its visible material field");
            return false;
        }
        render_gpu_timing_mark("Fluid raster");
    }
    render_radiance_inputs inputs = { .material = textures };
    inputs.background = scene_background(renderer, world, &camera);
    render_gpu_timing_mark("Background");
    bool hdr = renderer->lighting.tone_mapping > 0;
    CF_Canvas composite = renderer->output;
    if (hdr) {
        if (!renderer->hdr) {
            renderer->hdr = render_post_2d_new();
        }
        if (!renderer->hdr || !render_post_2d_resize(renderer->hdr, width, height)) {
            snprintf(renderer->error, sizeof renderer->error, "Tone mapping requires filterable RGBA16F render targets");
            return false;
        }
        composite = render_post_2d_target(renderer->hdr);
    }
    if (!render_radiance_draw(renderer->radiance, inputs, view, composite)) {
        return false;
    }
    render_view_2d screen = { .min_x = camera.center_x - camera.span_width * .5f,
                              .max_y = camera.center_y + camera.span_height * .5f,
                              .width = camera.span_width,
                              .height = camera.span_height,
                              .crop_min_u = view.crop_min_u,
                              .crop_min_v = view.crop_min_v,
                              .crop_max_u = view.crop_max_u,
                              .crop_max_v = view.crop_max_v };
    if (hdr && !present_hdr(renderer, world, runtime, screen, textures)) {
        return false;
    }
    CF_Canvas output = renderer->output;
    if (acount(renderer->reflectors)) {
        if (!renderer->reflections) {
            renderer->reflections = render_reflections_new();
        }
        output = render_reflections_draw(
            renderer->reflections,
            output,
            width,
            height,
            screen,
            renderer->reflectors,
            acount(renderer->reflectors)
        );
        if (!output.id) {
            return false;
        }
        render_gpu_timing_mark("Reflections");
    }
    render_gpu_span_end(&renderer->span);
    cf_draw_canvas(
        output,
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
