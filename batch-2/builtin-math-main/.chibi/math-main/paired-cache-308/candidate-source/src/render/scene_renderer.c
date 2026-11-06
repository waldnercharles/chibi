#include "render/scene_renderer.h"

#include "diagnostics/tracy.h"
#include "platform/cute.h"
#include "render/appearance_geometry.h"
#include "render/draw_textures.h"
#include "render/fluid_raster.h"
#include "render/fog.h"
#include "render/fullscreen.h"
#include "render/geometry_2d.h"
#include "render/gpu_clock.h"
#include "render/gpu_timing.h"
#include "render/grid_2d.h"
#include "render/material.h"
#include "render/material_lod.h"
#include "render/material_mesh.h"
#include "render/material_raster.h"
#include "render/mesh_projection.h"
#include "render/mesh_surface.h"
#include "render/post_2d.h"
#include "render/radiance.h"
#include "render/reflections.h"
#include "render/scene3d_cascades.h"
#include "render/scene3d_post.h"
#include "render/shader_catalog.h"
#include "render/shader_dir.h"
#include "render/shader_fragment.h"
#include "render/shader_images.h"
#include "render/viewport.h"
#include "runtime/component_catalog.h"
#include "runtime/tilemap.h"
#include "runtime/transform_propagation.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Perspective distance for particles: raised particles scale by
 * focal / (focal + elevation). */
#define PARTICLE_FOCAL_LENGTH 300.f

typedef struct
{
    ecs_entity entity;
    int layer;
} draw_actor;

typedef struct
{
    ecs_entity entity;
    tilemap_grid grid;
    bool used;
} draw_tilemap;

struct app_scene_renderer
{
    render_scene3d *scene3d;
    render_scene3d_post *post;
    render_fog *fog;
    bool scene3d_attempted, parallel_disabled, resources_attempted;
    render_material_raster *raster;
    render_model_inspection inspection;
    int sdf_batch_limit;
    SurfaceQuality surface_quality;
    HRCShadowMode shadow_mode;
    bool quality_override, shadow_override, sdf_trace_debug;
    bool hidden_rejection, spatial_scratch;
    render_radiance_lab lab;
    render_fluid_raster *fluid;
    render_radiance *radiance;
    /* HDR presentation for tone-mapped 2D scenes. */
    render_post_2d *hdr;
    HRC2D lighting;
    render_reflections *reflections;
    render_reflection *reflectors;
    CF_Canvas output, background;
    CF_Shader background_shader, unlit_shader;
    Starfield2D background_style;
    engine_viewport_camera background_camera;
    bool background_valid, gpu_clock;
    /* A host's grid under the scene (app_scene_renderer_set_grid), and the
       one the background shows. */
    float grid, background_grid;
    bool unlit, fogless; /* app_scene_renderer_set_view */
    /* Whether the last frame was lit: the view is lit and the scene has
     * HRC2D settings. HRC lights it where the backend has HRC. */
    bool lit;
    /* The scene's GPU work each frame, for adaptive lighting resolution. */
    render_gpu_span span;
    /* Without GPU timestamps, the previous frame's start in ticks. */
    uint64_t frame_start;
    /* Tile groups numbered so far this frame. */
    int tile_groups;
    render_material_entity *entities;
    render_mesh_material_actor *meshes;
    int *mesh_surface_slots;
    render_mesh_surface *mesh_surfaces;
    render_material_border_cache *border;
    render_material_lod *lod;
    render_material_mesh_cache *material_meshes;
    draw_actor *actors;
    draw_tilemap *tilemaps;
    render_skip skip; /* the entities the current draw leaves out */
    ecs_world *world;
    engine_viewport_camera camera;
    int width, height;
    int raster_width, raster_height;
    float pixel_aa, time;
    render_programs *fragment;
    int *fragment_programs;
    char error[256];
    /* Lighting settings adjusted to what HRC supports, for authors. */
    char warning[512];
};

void app_scene_renderer_initialize()
{
    cf_fs_mount(render_shader_dir(), "/chibi-render", true);
    cf_shader_directory("/chibi-render");
}

app_scene_renderer *app_scene_renderer_new()
{
    app_scene_renderer *renderer = xcalloc(1, sizeof(app_scene_renderer));
    renderer->hidden_rejection = true;
    renderer->spatial_scratch = true;
    renderer->lab = render_radiance_lab_defaults();
    return renderer;
}

static void initialize_resources(app_scene_renderer *renderer)
{
    if (renderer->resources_attempted) {
        return;
    }
    renderer->resources_attempted = true;
    renderer->fog = render_fog_new();
    renderer->raster = render_material_raster_new();
    if (renderer->raster) {
        render_material_raster_set_sdf_batch_limit(renderer->raster, renderer->sdf_batch_limit);
        render_material_raster_set_hidden_rejection(renderer->raster, renderer->hidden_rejection);
        render_material_raster_set_spatial_scratch(renderer->raster, renderer->spatial_scratch);
        render_material_raster_set_model_inspection(renderer->raster, &renderer->inspection);
    }
    renderer->radiance = render_radiance_new();
    /* Adaptive lighting resolution times lighting with GPU timestamps. */
    renderer->gpu_clock = cf_query_backend() == CF_BACKEND_TYPE_METAL &&
                          render_gpu_clock_install();
}

static void clear_tilemaps(app_scene_renderer *renderer)
{
    for (int i = 0; i < acount(renderer->tilemaps); i++) {
        tilemap_grid_free(&renderer->tilemaps[i].grid);
    }
    afree(renderer->tilemaps);
}

void app_scene_renderer_free(app_scene_renderer *renderer)
{
    if (!renderer) {
        return;
    }
    if (renderer->material_meshes) {
        render_material_mesh_cache_free(renderer->material_meshes, renderer->raster);
    }
    render_material_raster_free(renderer->raster);
    afree(renderer->meshes);
    afree(renderer->mesh_surface_slots);
    for (int i = 0; i < acount(renderer->mesh_surfaces); i++) {
        render_mesh_surface_free(&renderer->mesh_surfaces[i]);
    }
    afree(renderer->mesh_surfaces);
    render_fluid_raster_free(renderer->fluid);
    render_scene3d_free(renderer->scene3d);
    render_scene3d_post_free(renderer->post);
    render_post_2d_free(renderer->hdr);
    if (renderer->fog) {
        render_fog_free(renderer->fog);
    }
    render_radiance_free(renderer->radiance);
    render_gpu_span_discard(&renderer->span);
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
    if (renderer->unlit_shader.id) {
        cf_destroy_shader(renderer->unlit_shader);
    }
    render_programs_free(renderer->fragment);
    afree(renderer->fragment_programs);
    afree(renderer->entities);
    render_material_border_cache_free(renderer->border);
    render_material_lod_free(renderer->lod);
    afree(renderer->actors);
    clear_tilemaps(renderer);
    free(renderer);
}

static bool scene3d_ready(app_scene_renderer *renderer)
{
    if (!renderer->scene3d_attempted) {
        renderer->scene3d_attempted = true;
        renderer->scene3d = render_scene3d_new();
        render_scene3d_set_parallel(renderer->scene3d, !renderer->parallel_disabled);
        renderer->post = render_scene3d_post_new();
    }
    return renderer->scene3d && renderer->post;
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
    return renderer && renderer->scene3d
             ? render_scene3d_particle_count(renderer->scene3d)
             : 0;
}

void app_scene_renderer_set_view(app_scene_renderer *renderer, bool unlit, bool fogless)
{
    renderer->unlit = unlit;
    renderer->fogless = fogless;
}

/* Starts or stops HRC. Its light and timing from before it stopped no longer
 * describe the scene. */
static void set_lit(app_scene_renderer *renderer, bool lit)
{
    if (renderer->lit == lit) {
        return;
    }
    renderer->lit = lit;
    render_gpu_span_discard(&renderer->span);
    renderer->frame_start = 0;
    renderer->background_valid = false;
    if (renderer->radiance) {
        render_radiance_reset(renderer->radiance);
    }
}

/* The fog of war over the frame, unless the view leaves it out. */
static bool draw_fog(
    app_scene_renderer *renderer,
    const engine *runtime,
    const engine_viewport_camera *frame,
    const render_scene3d_camera *camera
)
{
    return renderer->fogless || render_fog_draw(
                                    renderer->fog,
                                    engine_visibility(runtime),
                                    frame,
                                    camera,
                                    renderer->time
                                );
}

/* Without fog, only an object's own hidden state hides it. */
static bool entity_shown(
    const app_scene_renderer *renderer,
    const engine *runtime,
    ecs_world *world,
    ecs_entity entity
)
{
    if (!renderer->fogless) {
        return engine_entity_visible(runtime, world, entity);
    }
    const RenderState *state = RenderState_get_const(world, entity);
    return !runtime || !state || !state->hidden;
}

static bool scene3d_environment(app_scene_renderer *renderer, ecs_world *world, Environment *settings)
{
    *settings = environment_defaults();
    if (!ecs_world_component(world, COMPONENT_Environment.id)) {
        return true;
    }
    ecs_component_id required[] = { COMPONENT_Environment.id };
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
        *settings = *Environment_get_const(world, entity);
        found = true;
    }
    ecs_query_end(&query);
    return valid;
}

static void scene3d_present(app_scene_renderer *renderer)
{
    CF_Canvas output = render_scene3d_post_output(renderer->post);
    if (!output.id) {
        return;
    }
    cf_draw_push();
    cf_draw_TSR_absolute((CF_V2){ 0 }, (CF_V2){ 1, 1 }, 0);
    cf_draw_projection(cf_ortho_2d(0, 0, 2, 2));
    cf_draw_canvas(output, (CF_V2){ 0 }, (CF_V2){ 2, 2 });
    cf_draw_pop();
}

static bool scene3d_prepare(app_scene_renderer *renderer, int width, int height, const Environment *settings)
{
    if (render_scene3d_post_prepare(renderer->post, width, height, settings)) {
        return true;
    }
    snprintf(
        renderer->error,
        sizeof renderer->error,
        "%s",
        render_scene3d_post_error(renderer->post)
    );
    scene3d_present(renderer);
    return false;
}

static bool scene3d_finish(app_scene_renderer *renderer, bool submitted)
{
    snprintf(
        renderer->error,
        sizeof renderer->error,
        "%s",
        render_scene3d_error(renderer->scene3d)
    );
    bool processed = false;
    if (submitted) {
        cf_render_to(render_scene3d_post_target(renderer->post), true);
        render_gpu_timing_mark("3D surfaces");
        processed = render_scene3d_post_process(renderer->post, renderer->time);
        render_gpu_timing_mark("3D post-processing");
        if (!processed) {
            snprintf(
                renderer->error,
                sizeof renderer->error,
                "%s",
                render_scene3d_post_error(renderer->post)
            );
        }
    }
    scene3d_present(renderer);
    return submitted && processed;
}

bool app_scene_renderer_draw3d(
    app_scene_renderer *renderer,
    ecs_world *world,
    const particles *particles,
    const render_scene3d_camera *camera,
    engine_viewport *viewport,
    const engine *runtime,
    render_skip skip
)
{
    if (!renderer) {
        return false;
    }
    initialize_resources(renderer);
    if (!scene3d_ready(renderer)) {
        return false;
    }
    renderer->error[0] = 0;
    Environment settings;
    if (!scene3d_environment(renderer, world, &settings)) {
        scene3d_present(renderer);
        return false;
    }
    int width, height;
    engine_viewport_pixel_size(viewport, &width, &height);
    if (!scene3d_prepare(renderer, width, height, &settings)) {
        return false;
    }
    render_scene3d_set_size(renderer->scene3d, width, height);
    /* The post canvas still holds the last frame until this one renders. */
    render_scene3d_set_history(
        renderer->scene3d,
        cf_canvas_get_target(render_scene3d_post_target(renderer->post))
    );
    render_gpu_timing_start();
    bool drawn = scene3d_finish(
        renderer,
        render_scene3d_draw(
            renderer->scene3d,
            world,
            particles,
            camera,
            renderer->time,
            runtime,
            &settings,
            skip
        )
    );
    engine_viewport_camera frame;
    engine_viewport_get_camera(viewport, &frame);
    return drawn && draw_fog(renderer, runtime, &frame, camera);
}

void app_scene_renderer_reset(app_scene_renderer *renderer)
{
    if (renderer && renderer->material_meshes) {
        render_material_mesh_cache_clear(renderer->material_meshes, renderer->raster);
    }
    if (renderer) {
        clear_tilemaps(renderer);
    }
    if (renderer && renderer->border) {
        render_material_border_cache_clear(renderer->border);
    }
    if (renderer && renderer->scene3d) {
        render_scene3d_reset(renderer->scene3d);
    }
    if (renderer && renderer->post) {
        render_scene3d_post_reset(renderer->post);
    }
    if (renderer && renderer->radiance) {
        render_radiance_reset(renderer->radiance);
    }
}

int app_scene_renderer_material_pieces(const app_scene_renderer *renderer)
{
    return acount(renderer->entities);
}

bool app_scene_renderer_lighting_status(const app_scene_renderer *renderer, render_radiance_status *status)
{
    if (!renderer || !renderer->radiance || !renderer->lit) {
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

static void copy_material(render_material_entity *entity, const material_coefficients *material)
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
    const Transform *position,
    const Appearance *appearance,
    float angle,
    const Material *material
)
{
    CF_V2 min, max;
    if (!isfinite(position->x) || !isfinite(position->y) ||
        !appearance_rotated_bounds(appearance, angle, &min, &max)) {
        return false;
    }
    material_coefficients coefficients;
    Material fallback = { .scene = appearance->color,
                          .albedo = appearance->color,
                          .emission_color = appearance->border_color,
                          .border_scene = appearance->border_color,
                          .border_albedo = appearance->border_color,
                          .border_emission_color = appearance->border_color,
                          .emission_intensity = 0.035f,
                          .emission_multiplier = 1 };
    if (!material_extract(material ? material : &fallback, &coefficients)) {
        return false;
    }
    float aa = isfinite(appearance->antialias) ? fmaxf(appearance->antialias, 0) : 1;
    float padding = aa > 0 ? aa : renderer->pixel_aa;
    render_material_entity entity = {
        .pos = { position->x, -position->y },
        .extent = { fmaxf(fabsf(min.x), fabsf(max.x)) + padding,
                    fmaxf(fabsf(min.y), fabsf(max.y)) + padding },
        .inv_rot = { cosf(angle), sinf(angle), -sinf(angle), cosf(angle) },
        .aa = aa,
        .layer = appearance->layer,
        .relief = appearance->relief,
        .reserved = { 0, 0, material ? material->edge_light : 0 },
        .border_thickness = fmaxf(0, appearance->border_width)
    };
    float half_x = appearance->width * 0.5f, half_y = appearance->height * 0.5f;
    switch (appearance->shape) {
        case SHAPE_CIRCLE:
            entity.shape_type = RENDER_SHAPE_CIRCLE;
            entity.radius = fminf(half_x, half_y);
            break;
        case SHAPE_CAPSULE: {
            entity.shape_type = RENDER_SHAPE_CAPSULE;
            entity.radius = fminf(half_x, half_y);
            entity.points[0] = -(half_x - entity.radius);
            entity.points[1] = -(half_y - entity.radius);
            entity.points[2] = -entity.points[0];
            entity.points[3] = -entity.points[1];
            break;
        }
        case SHAPE_DIAMOND:
        case SHAPE_CHEVRON:
        case SHAPE_STROKED_CHEVRON:
        case SHAPE_TRIANGLE:
        case SHAPE_STROKED_TRIANGLE: {
            CF_V2 points[4];
            int count = appearance_points(appearance, points);
            entity.point_count = (unsigned)count;
            entity.shape_type = appearance->shape == SHAPE_STROKED_CHEVRON ||
                                        appearance->shape == SHAPE_STROKED_TRIANGLE
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
    entity.time[0] = renderer->time;
    copy_material(&entity, &coefficients);
    apush(renderer->entities, entity);
    return true;
}

typedef struct
{
    int image, normal_map, height_map;
    int width, height, cell_width, cell_height, columns, frames;
} sprite_atlas;

/* The resident variant of image `key` in `color_space`, sampled texel by
 * texel. */
static int sprite_image(const char *key, shader_image_color_space color_space, shader_error *error)
{
    shader_image_source source = { .key = (char *)key,
                                   .color_space = color_space,
                                   .filter = SHADER_IMAGE_NEAREST,
                                   .wrap = SHADER_IMAGE_CLAMP };
    return render_image_variant_resident(&source, error);
}

/* Resolves a sprite's linear map `key` into `map`, zero when it has none. The
 * map must be laid out like the atlas's image. */
static bool load_sprite_map(
    app_scene_renderer *renderer,
    const char *key,
    const sprite_atlas *atlas,
    int *map
)
{
    *map = 0;
    if (!key || !*key) {
        return true;
    }
    shader_error error = { 0 };
    int width, height;
    *map = sprite_image(key, SHADER_IMAGE_LINEAR, &error);
    if (!*map || !render_image_info(key, &width, &height)) {
        snprintf(
            renderer->error,
            sizeof renderer->error,
            "Sprite map: %s",
            error.message
        );
        return false;
    }
    if (width != atlas->width || height != atlas->height) {
        snprintf(
            renderer->error,
            sizeof renderer->error,
            "Sprite map %s is %dx%d, not the %dx%d of its image",
            key,
            width,
            height,
            atlas->width,
            atlas->height
        );
        return false;
    }
    return true;
}

static bool load_sprite_atlas(app_scene_renderer *renderer, const Sprite *sprite, sprite_atlas *atlas)
{
    if (!isfinite(sprite->relief)) {
        snprintf(
            renderer->error,
            sizeof renderer->error,
            "Sprite %s has no finite relief",
            sprite->image
        );
        return false;
    }
    shader_error error = { 0 };
    atlas->image = sprite_image(sprite->image, SHADER_IMAGE_SRGB, &error);
    if (!atlas->image ||
        !render_image_info(sprite->image, &atlas->width, &atlas->height)) {
        snprintf(renderer->error, sizeof renderer->error, "Sprite: %s", error.message);
        return false;
    }
    if (!load_sprite_map(renderer, sprite->normal_map, atlas, &atlas->normal_map) ||
        !load_sprite_map(renderer, sprite->height_map, atlas, &atlas->height_map)) {
        return false;
    }
    atlas->cell_width = sprite->frame_width > 0 ? sprite->frame_width : atlas->width;
    atlas->cell_height = sprite->frame_height > 0 ? sprite->frame_height : atlas->height;
    atlas->columns = atlas->width / atlas->cell_width;
    atlas->frames = atlas->columns * (atlas->height / atlas->cell_height);
    return true;
}

/* A sprite is a white box whose raster multiplies its material by the frame's
 * texels, so a Material tints it. Tilemaps resolve their atlas once per draw. */
static bool append_sprite_frame(
    app_scene_renderer *renderer,
    const Transform *position,
    const Sprite *sprite,
    float angle,
    const Material *material,
    const sprite_atlas *atlas
)
{
    if (sprite->frame < 0 || sprite->frame >= atlas->frames) {
        snprintf(
            renderer->error,
            sizeof renderer->error,
            "Sprite frame %d is outside the %d frames of %s",
            sprite->frame,
            atlas->frames,
            sprite->image
        );
        return false;
    }
    Appearance box = { .width = (float)atlas->cell_width,
                       .height = (float)atlas->cell_height,
                       .color = 0xffffffffu,
                       .layer = sprite->layer };
    if (!append_entity(renderer, position, &box, angle, material)) {
        return true;
    }
    float u0 = (float)(sprite->frame % atlas->columns * atlas->cell_width) /
               (float)atlas->width;
    float v0 = (float)(sprite->frame / atlas->columns * atlas->cell_height) /
               (float)atlas->height;
    float u1 = u0 + (float)atlas->cell_width / (float)atlas->width;
    render_material_entity *draw = &alast(renderer->entities);
    draw->shape_type = RENDER_SHAPE_SPRITE;
    draw->points[8] = sprite->flip_x ? u1 : u0;
    draw->points[9] = v0;
    draw->points[10] = sprite->flip_x ? u0 : u1;
    draw->points[11] = v0 + (float)atlas->cell_height / (float)atlas->height;
    draw->points[12] = (float)atlas->image;
    draw->points[13] = (float)atlas->normal_map;
    draw->points[14] = (float)atlas->height_map;
    draw->relief = sprite->relief;
    /* Maps shade a sprite through the surface light a Material's edge_light
     * scales, so without a Material it takes that light in full. */
    if (!material && (atlas->normal_map || atlas->height_map)) {
        draw->reserved[2] = 1;
    }
    return true;
}

static bool append_sprite(
    app_scene_renderer *renderer,
    const Transform *position,
    const Sprite *sprite,
    float angle,
    const Material *material
)
{
    sprite_atlas atlas;
    return load_sprite_atlas(renderer, sprite, &atlas) &&
           append_sprite_frame(renderer, position, sprite, angle, material, &atlas);
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
    Transform origin,
    float heading,
    Reflection style,
    const Material *material
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

/* Disjoint rectangles cover the cells within the mask's border width. */
static void mask_edges(
    app_scene_renderer *renderer,
    const material_mask *mask,
    Transform position,
    float heading,
    const Appearance *look,
    const Material *material
)
{
    if (!material || look->border_width <= 0) {
        return;
    }
    Material edge = *material;
    edge.scene = material->border_scene;
    edge.albedo = material->border_albedo;
    edge.filter = material->border_filter;
    edge.emission_color = material->border_emission_color;
    float cells = ceilf(look->border_width / mask->cell_size);
    int maximum = mask->width > mask->height ? mask->width : mask->height;
    int reach = cells >= maximum ? maximum : (int)cells;
    render_material_border_view border = render_material_border_cache_get(
        renderer->border,
        material_mask_occupancy(mask),
        reach
    );
    float c = cosf(heading), s = sinf(heading);
    for (int i = 0; i < border.count; i++) {
        render_material_border_rect rect = border.rects[i];
        float px = (rect.x + rect.width * .5f - mask->width * .5f) * mask->cell_size;
        float py = (rect.y + rect.height * .5f - mask->height * .5f) * mask->cell_size;
        Transform p = { position.x + c * px - s * py, position.y + s * px + c * py };
        Appearance cells = { .shape = SHAPE_BOX,
                             .width = rect.width * mask->cell_size,
                             .height = rect.height * mask->cell_size,
                             .antialias = .1f,
                             .color = look->border_color };
        append_entity(renderer, &p, &cells, -heading, &edge);
    }
}

/* The coarsest level up to level that keeps a mask's border as thick as
 * authored: whole cells of that level must still make up the border's. */
static int border_level(const material_mask *mask, float border_width, int level)
{
    int cells = (int)ceilf(border_width / mask->cell_size);
    int coarsest = 0;
    while (coarsest < level && cells % 2 == 0) {
        cells /= 2;
        coarsest++;
    }
    return coarsest;
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
    Transform origin,
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
    if (view.yaw != 0 || view.pitch != 0 || view.roll != 0 ||
        view.target_elevation != 0) {
        CF_V2 lo, hi;
        return render_mesh_project_bounds(
                   view,
                   (CF_V3){ x - rx, 0, -y - ry },
                   (CF_V3){ x + rx, 0, -y + ry },
                   &lo,
                   &hi
               ) &&
               hi.x >= view.min_x && lo.x <= view.min_x + view.width &&
             -lo.y >= view.max_y - view.height && -hi.y <= view.max_y;
    }
    return x + rx >= view.min_x && x - rx <= view.min_x + view.width &&
           y + ry >= view.max_y - view.height && y - ry <= view.max_y;
}

static bool append_tilemap(
    app_scene_renderer *renderer,
    ecs_entity entity,
    GlobalTransform2D shown,
    const Tilemap *map,
    const Material *material,
    render_material_view view
)
{
    draw_tilemap *cached = NULL;
    for (int i = 0; i < acount(renderer->tilemaps); i++) {
        if (ecs_entity_to_u64(renderer->tilemaps[i].entity) ==
            ecs_entity_to_u64(entity)) {
            cached = &renderer->tilemaps[i];
            break;
        }
    }
    if (!cached) {
        apush(renderer->tilemaps, ((draw_tilemap){ .entity = entity }));
        cached = &alast(renderer->tilemaps);
    }
    cached->used = true;
    if (!tilemap_grid_update(&cached->grid, map->cells, map->columns, map->rows, map->frame_width, map->frame_height)) {
        snprintf(
            renderer->error,
            sizeof renderer->error,
            "Tilemap: %s",
            cached->grid.error
        );
        return false;
    }
    Sprite sprite = { .image = map->image,
                      .frame_width = map->frame_width,
                      .frame_height = map->frame_height,
                      .layer = map->layer,
                      .normal_map = map->normal_map,
                      .height_map = map->height_map,
                      .relief = map->relief };
    sprite_atlas atlas;
    if (!load_sprite_atlas(renderer, &sprite, &atlas)) {
        return false;
    }
    float c = cosf(shown.radians), s = sinf(shown.radians);
    material_rect bounds = { .width = (float)map->frame_width,
                             .height = (float)map->frame_height };
    for (int row = 0; row < map->rows; row++) {
        for (int column = 0; column < map->columns; column++) {
            sprite.frame = tilemap_grid_frame(&cached->grid, column, row);
            if (sprite.frame < 0) {
                continue;
            }
            float x = (column + .5f) * map->frame_width;
            float y = (row + .5f) * map->frame_height;
            Transform center = { shown.position.x + c * x - s * y,
                                 shown.position.y + s * x + c * y };
            if (!mask_visible(view, center, bounds, shown.radians, renderer->pixel_aa)) {
                continue;
            }
            if (!append_sprite_frame(renderer, &center, &sprite, -shown.radians, material, &atlas)) {
                return false;
            }
        }
    }
    return true;
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
    ecs_component_id component = COMPONENT_Transform.id;
    ecs_query query;
    if (ecs_query_begin(world, &query, &component, 1) != ECS_OK) {
        return;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const Appearance *appearance = Appearance_get_const(world, entity);
        const Sprite *sprite = Sprite_get_const(world, entity);
        const Tilemap *tilemap = Tilemap_get_const(world, entity);
        int layer = tilemap    ? tilemap->layer
                  : sprite     ? sprite->layer
                  : appearance ? appearance->layer
                               : 0;
        apush(renderer->actors, ((draw_actor){ entity, layer }));
    }
    ecs_query_end(&query);
    if (acount(renderer->actors) > 1) {
        asort(renderer->actors, compare_actors);
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
    const material_chunk *chunks = materials_chunks(engine_materials(runtime), &count);
    uint64_t cached = 0;
    const Material *material = NULL;
    const Appearance *look = NULL;
    const MaterialBreakProfile *profile = NULL;
    int shader = 0;
    float parameters[32][4] = { 0 };
    for (int i = 0; i < count; i++) {
        const material_chunk *chunk = &chunks[i];
        float scale = material_chunk_scale(chunk), radius = chunk->radius * scale;
        if (radius <= .05f ||
            !mask_visible(
                view,
                (Transform){ chunk->position.x, chunk->position.y },
                (material_rect){ .width = radius * 2, .height = radius * 2 },
                0,
                0
            )) {
            continue;
        }
        if (chunk->source != cached) {
            cached = chunk->source;
            ecs_entity source = ecs_entity_from_u64(chunk->source);
            bool alive = ecs_entity_is_alive(world, source);
            material = alive ? Material_get_const(world, source) : NULL;
            look = alive ? Appearance_get_const(world, source) : NULL;
            profile = alive ? MaterialBreakProfile_get_const(world, source) : NULL;
            const Shader *binding = alive ? Shader_get_const(world, source) : NULL;
            shader = binding ? render_shader_find(binding->material) : 0;
            if (shader && render_shader_is_shape(shader)) {
                shader = 0;
            }
            if (shader) {
                render_shader_parameters(shader, world, source, binding->parameters, parameters);
            }
        }
        Transform position = { chunk->position.x, chunk->position.y };
        Appearance part = { .width = 2 * radius,
                            .height = 2 * radius,
                            .shape = SHAPE_BOX,
                            .color = look ? look->color : 0x4a4744ffu,
                            .antialias = look ? look->antialias : 0 };
        /* A hot chunk glows in its material's spark color until it cools
         * enough to show the material's pattern. */
        bool hot = profile && chunk->heat > .05f;
        Material glow = material ? *material : (Material){ 0 };
        if (hot) {
            glow.emission_color = profile->spark_color;
            glow.emission_intensity = chunk->heat * profile->spark_emission;
            glow.emission_multiplier = 1;
        }
        if (!append_entity(renderer, &position, &part, -chunk->angle, hot ? &glow : material)) {
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

static bool collect(
    app_scene_renderer *renderer,
    ecs_world *world,
    const particles *particles,
    const engine *runtime,
    render_material_view view
)
{
    if (!renderer->border) {
        renderer->border = render_material_border_cache_new(65536, 16u * 1024 * 1024);
    }
    render_material_border_cache_begin(renderer->border);
    if (!renderer->lod) {
        renderer->lod = render_material_lod_new();
    }
    render_material_lod_begin(renderer->lod);
    if (!renderer->material_meshes) {
        renderer->material_meshes = render_material_mesh_cache_new();
    }
    render_material_mesh_cache_begin(renderer->material_meshes);
    asetlen(renderer->entities, 0);
    aclear(renderer->meshes);
    aclear(renderer->mesh_surface_slots);
    renderer->tile_groups = 0;
    aclear(renderer->reflectors);
    for (int i = 0; i < acount(renderer->tilemaps);) {
        draw_tilemap *cached = &renderer->tilemaps[i];
        if (!cached->used) {
            tilemap_grid_free(&cached->grid);
            adel(renderer->tilemaps, i);
            continue;
        }
        cached->used = false;
        i++;
    }
    collect_order(renderer, world);
    for (int actor = 0; actor < acount(renderer->actors); actor++) {
        ecs_entity entity = renderer->actors[actor].entity;
        if (!entity_shown(renderer, runtime, world, entity) ||
            render_skip_has(renderer->skip, entity)) {
            continue;
        }
        GlobalTransform2D shown = engine_render_transform(runtime, world, entity);
        const Shader *binding = Shader_get_const(world, entity);
        const MeshSurface2D *mesh = MeshSurface2D_get_const(world, entity);
        const SdfSurface3D *sdf = SdfSurface3D_get_const(world, entity);
        if ((mesh && mesh->enabled) || (sdf && sdf->enabled)) {
            const VisualTransform *pose = VisualTransform_get_const(world, entity);
            if (pose && pose->scale == 0) {
                continue;
            }
            Transform position = shown.position;
            model_bounds selected_bounds, *override = NULL;
            if (ecs_entity_to_u64(entity) ==
                    ecs_entity_to_u64(renderer->inspection.entity) &&
                renderer->inspection.part >= 0) {
                const Shader *binding = Shader_get_const(world, entity);
                shader_error diagnostic = { 0 };
                if (!binding || !render_shader_model_preview_snapshot(
                                    render_shader_find(binding->shape),
                                    world,
                                    entity,
                                    binding->parameters,
                                    renderer->inspection.part,
                                    &selected_bounds,
                                    &diagnostic
                                )) {
                    snprintf(
                        renderer->error,
                        sizeof renderer->error,
                        "%s",
                        diagnostic.message
                    );
                    return false;
                }
                override = &selected_bounds;
            }

            int index = acount(renderer->meshes);
            while (index >= acount(renderer->mesh_surfaces)) {
                apush(renderer->mesh_surfaces, ((render_mesh_surface){ 0 }));
            }
            render_mesh_surface *surface = &renderer->mesh_surfaces[index];
            if (!render_mesh_surface_build_with_bounds(
                    world,
                    entity,
                    &position,
                    NULL,
                    override,
                    surface,
                    renderer->error,
                    sizeof renderer->error
                )) {
                return false;
            }
            if (!acount(surface->parts)) {
                continue;
            }
            CF_V2 min, max;
            bool visible = render_mesh_project_bounds(
                view,
                surface->min,
                surface->max,
                &min,
                &max
            );
            if (!visible || max.x < view.min_x || min.x > view.min_x + view.width ||
                -min.y < view.max_y - view.height || -max.y > view.max_y) {
                continue;
            }
            render_material_entity draw = { .shape_type = RENDER_SHAPE_MESH,
                                            .layer = renderer->actors[actor].layer,
                                            .points = { (float)acount(renderer->meshes) } };
            float pixels = fmaxf(
                (max.x - min.x) * renderer->raster_width / view.width,
                (max.y - min.y) * renderer->raster_height / view.height
            );
            const char *detail = render_mesh_surface_lod_shape(
                sdf,
                pixels,
                renderer->surface_quality.disable_sdf_lod
            );
            apush(
                renderer->meshes,
                ((render_mesh_material_actor){ .entity = entity,
                                               .position = position,
                                               .surface = surface,
                                               .sdf_shape = detail })
            );
            apush(renderer->mesh_surface_slots, index);
            apush(renderer->entities, draw);
            continue;
        }
        const MaterialSurface3D *settings = MaterialSurface3D_get_const(world, entity);
        const material_mask *surface_mask = settings && settings->enabled
                                              ? materials_mask(engine_materials(runtime), entity)
                                              : NULL;
        if (surface_mask) {
            MaterialFrame source = { 0 };
            const MaterialFrame *authored_frame = MaterialFrame_get_const(world, entity);
            if (authored_frame) {
                source = *authored_frame;
            }
            const render_mesh_surface *surface = render_material_mesh_cache_get(
                renderer->material_meshes,
                material_mask_occupancy(surface_mask),
                *settings,
                source,
                shown,
                world,
                entity,
                renderer->error,
                sizeof renderer->error
            );
            if (!surface) {
                return false;
            }
            if (!acount(surface->parts)) {
                continue;
            }
            CF_V2 min, max;
            if (!render_mesh_project_bounds(view, surface->min, surface->max, &min, &max) ||
                max.x < view.min_x || min.x > view.min_x + view.width ||
                -min.y < view.max_y - view.height || -max.y > view.max_y) {
                continue;
            }
            render_material_entity draw = { .shape_type = RENDER_SHAPE_MESH,
                                            .layer = renderer->actors[actor].layer,
                                            .points = { (float)acount(renderer->meshes) } };
            apush(
                renderer->meshes,
                ((render_mesh_material_actor){ .entity = entity,
                                               .position = shown.position,
                                               .surface = surface })
            );
            apush(renderer->mesh_surface_slots, -1);
            apush(renderer->entities, draw);
            /* This mesh is the visible and optical material layer. Do not
             * submit the same mask again as a planar colored fill. */
            continue;
        }
        const Tilemap *tilemap = Tilemap_get_const(world, entity);
        if (tilemap) {
            if (!append_tilemap(renderer, entity, shown, tilemap, Material_get_const(world, entity), view)) {
                return false;
            }
            continue;
        }
        const Sprite *sprite = Sprite_get_const(world, entity);
        if (sprite) {
            Transform position = shown.position;
            if (!append_sprite(renderer, &position, sprite, -shown.radians, Material_get_const(world, entity))) {
                return false;
            }
            continue;
        }
        const Appearance *authored = Appearance_get_const(world, entity);
        const Geometry3D *geometry = Geometry3D_get_const(world, entity);
        if (!authored && !geometry) {
            continue;
        }
        Appearance fallback = authored ? *authored
                                       : (Appearance){ .width = geometry->width,
                                                       .height = geometry->depth,
                                                       .color = 0xffffffffu,
                                                       .shape = SHAPE_BOX,
                                                       .antialias = 1 };
        const VisualTransform *visual = VisualTransform_get_const(world, entity);
        float heading = shown.radians;
        Appearance appearance;
        float angle;
        if (!render_geometry_2d_project(authored ? NULL : geometry, &fallback, visual, heading, &appearance, &angle)) {
            continue;
        }
        int shape = !authored && binding ? render_shader_find(binding->shape) : 0;
        float bounds[2];
        if (!authored && render_shader_bounds(shape, bounds)) {
            float scale = visual ? visual->scale : 1;
            appearance.shape = SHAPE_BOX;
            appearance.width = 2 *
                               (binding->bounds_x > 0 ? binding->bounds_x : bounds[0]) *
                               scale;
            appearance.height = 2 *
                                (binding->bounds_y > 0 ? binding->bounds_y
                                                       : bounds[1]) *
                                scale;
        }
        Transform position = shown.position;
        const material_mask *mask = materials_mask(engine_materials(runtime), entity);
        if (mask) {
            /* Distant masks draw a coarser level, about a piece per pixel. */
            int level = render_material_lod_level(
                renderer->lod,
                mask->cell_size,
                .5f / renderer->pixel_aa
            );
            if (appearance.border_width > 0 && Material_get_const(world, entity)) {
                level = border_level(mask, appearance.border_width, level);
            }
            mask = render_material_lod_get(renderer->lod, mask, level);
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
            const MaterialFrame *frame = MaterialFrame_get_const(world, entity);
            const Reflection *reflection = Reflection_get_const(world, entity);
            if (reflection) {
                for (int i = 0; i < material_mask_collision(mask).count; i++) {
                    reflect_rect(
                        renderer,
                        material_mask_collision(mask).rects[i],
                        position,
                        heading,
                        *reflection,
                        Material_get_const(world, entity)
                    );
                }
            }
            /* The border comes first as the group's half, which the raster
             * lays over the fill that follows. */
            float group = (float)++renderer->tile_groups;
            int first = acount(renderer->entities);
            mask_edges(renderer, mask, position, heading, &appearance, Material_get_const(world, entity));
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
                Transform p = {
                    position.x + cosf(heading) * rect.x - sinf(heading) * rect.y,
                    position.y + sinf(heading) * rect.x + cosf(heading) * rect.y
                };
                Appearance part = { .width = rect.width,
                                    .height = rect.height,
                                    .shape = SHAPE_BOX,
                                    .color = appearance.color,
                                    .antialias = appearance.antialias };
                if (!append_entity(renderer, &p, &part, -heading, Material_get_const(world, entity))) {
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
        if (!append_entity(renderer, &position, &appearance, angle, Material_get_const(world, entity))) {
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
    const particle *items = particles_data(particles);
    for (int i = 0; i < particles_count(particles); i++) {
        const particle *particle = &items[i];
        float life = 1 - particle->age / particle->lifetime;
        if (particle->size > 0) {
            float age = 1 - life;
            float diameter = 2 * particle_curve_value(
                                     particle->size,
                                     particle->size_end,
                                     particle->size_exponent,
                                     age
                                 );
            bool ring = particle->shape == PARTICLE_RING;
            bool triangle = particle->shape == PARTICLE_TRIANGLE ||
                            particle->shape == PARTICLE_TRIANGLE_OUTLINE;
            if (ring) {
                diameter = 2 * particle->size * (.45f + .55f * age);
            }
            unsigned color = particle_color(particle->color, particle->end_color, age);
            float length = particle->stretch *
                           hypotf(particle->velocity.x, particle->velocity.y);
            float perspective = PARTICLE_FOCAL_LENGTH /
                                fmaxf(1, PARTICLE_FOCAL_LENGTH + particle->elevation);
            diameter *= perspective;
            length *= perspective;
            Appearance appearance = {
                .width = diameter + (particle->shape == PARTICLE_STREAK ? length : 0),
                .height = diameter,
                .color = ring ? color & 0xffffff00u : color,
                .border_color = color,
                .border_width = ring ? diameter * .07f : 0,
                .shape = particle->shape == PARTICLE_STREAK ? SHAPE_CAPSULE
                       : triangle                           ? SHAPE_BOX
                                                            : SHAPE_CIRCLE,
                .antialias = particle_curve_value(
                    particle->antialias,
                    particle->antialias_end,
                    particle->antialias_exponent,
                    age
                )
            };
            /* Particles are matter: they reflect received light as well as
             * emitting their own. */
            Material material = { .scene = appearance.color,
                                  .albedo = appearance.color,
                                  .emission_color = appearance.color,
                                  .border_scene = color,
                                  .border_emission_color = color,
                                  .emission_intensity = particle_curve_value(
                                      particle->emission,
                                      particle->emission_end,
                                      particle->emission_exponent,
                                      age
                                  ),
                                  .emission_multiplier = 1,
                                  .absorption = particle->absorption };
            Transform position = { particle->position.x, particle->position.y };
            if (diameter <= 0) {
                continue;
            }
            if (!append_entity(renderer, &position, &appearance, -particle->angle, &material)) {
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
                bool outline = particle->shape == PARTICLE_TRIANGLE_OUTLINE;
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
        Appearance appearance = { .width = 1 + 3 * life,
                                  .height = 1 + 3 * life,
                                  .color = color,
                                  .shape = SHAPE_BOX,
                                  .antialias = 1 };
        Transform position = { particle->position.x, particle->position.y };
        append_entity(renderer, &position, &appearance, 0, NULL);
    }
    for (int i = 0; i < acount(renderer->meshes); i++) {
        int slot = renderer->mesh_surface_slots[i];
        if (slot >= 0) {
            renderer->meshes[i].surface = &renderer->mesh_surfaces[slot];
        }
    }
    render_material_mesh_cache_end(renderer->material_meshes, renderer->raster);
    return true;
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
    return (
        render_view_2d
    ){ .min_x = camera->center_x - width * 0.5f,
       .max_y = camera->center_y + height * 0.5f,
       .width = width,
       .height = height,
       .crop_min_u = pad_pixels / (float)(output_width + 2 * pad_pixels),
       .crop_min_v = pad_pixels / (float)(output_height + 2 * pad_pixels),
       .crop_max_u = (output_width + pad_pixels) / (float)(output_width + 2 * pad_pixels),
       .crop_max_v = (output_height + pad_pixels) / (float)(output_height + 2 * pad_pixels),
       .focal_distance = camera->focal_distance,
       .yaw = camera->yaw,
       .pitch = camera->pitch,
       .roll = camera->roll,
       .target_elevation = camera->target_elevation,
       .center_x = camera->center_x,
       .center_y = camera->center_y };
}

/* The first HRC2D's settings. A scene without one draws its materials
 * unlit, at the defaults' ambient and intensity. */
static bool scene_lighting(app_scene_renderer *renderer, ecs_world *world, CF_V2 *flat)
{
    HRC2D lighting = hrc2d_defaults();
    ecs_component_id component = COMPONENT_HRC2D.id;
    ecs_query query;
    int settings = 0;
    if (ecs_world_component(world, component) &&
        ecs_query_begin(world, &query, &component, 1) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            if (!ecs_entity_is_retiring(world, entity) && settings++ == 0) {
                lighting = *HRC2D_get_const(world, entity);
            }
        }
        ecs_query_end(&query);
    }
    set_lit(renderer, !renderer->unlit && settings > 0);
    if (renderer->shadow_override) {
        lighting.surface_shadow_mode = renderer->shadow_mode;
    }
    render_radiance_lab lab = renderer->lab;
    if (renderer->sdf_trace_debug) {
        lab.debug_view = RENDER_RADIANCE_DEBUG_SCENE;
        lighting.tone_mapping = HRC_TONE_CLAMP;
        lighting.bloom = 0;
    }
    char notes[400] = "";
    if (settings > 0) {
        lighting = render_radiance_normalize(lighting, notes, sizeof notes);
    }
    renderer->warning[0] = '\0';
    if (settings > 1) {
        snprintf(renderer->warning, sizeof renderer->warning, "Lighting uses the first of %d HRC2D entities. ", settings);
    }
    if (notes[0]) {
        size_t used = strlen(renderer->warning);
        snprintf(renderer->warning + used, sizeof renderer->warning - used, "Lighting adjusted: %s.", notes);
    }
    *flat = (CF_V2){ lighting.ambient, lighting.intensity };
    renderer->lighting = lighting;
    if (renderer->lit && renderer->radiance &&
        !render_radiance_set_lighting(renderer->radiance, lighting, lab)) {
        snprintf(renderer->error, sizeof renderer->error, "HRC could not allocate its lighting resources.");
        return false;
    }
    return true;
}

static bool scene_surface_quality(app_scene_renderer *renderer, ecs_world *world)
{
    SurfaceQuality quality = renderer->surface_quality;
    if (!renderer->quality_override) {
        quality = (SurfaceQuality){ 0 };
        ecs_component_id component = COMPONENT_SurfaceQuality.id;
        ecs_query query;
        if (ecs_world_component(world, component) &&
            ecs_query_begin(world, &query, &component, 1) == ECS_OK) {
            ecs_entity entity;
            while (ecs_query_next(&query, &entity)) {
                if (!ecs_entity_is_retiring(world, entity)) {
                    quality = *SurfaceQuality_get_const(world, entity);
                    break;
                }
            }
            ecs_query_end(&query);
        }
    }
    if (quality.resolution < SURFACE_SUPERSAMPLED || quality.resolution > SURFACE_FULL ||
        !isfinite(quality.sdf_pixel_error) || quality.sdf_pixel_error < 0 ||
        quality.sdf_pixel_error > 1 || quality.sdf_normal_mode < SDF_NORMAL_CENTRAL ||
        quality.sdf_normal_mode > SDF_NORMAL_ANALYTIC) {
        snprintf(renderer->error, sizeof renderer->error, "Invalid surface quality settings.");
        return false;
    }
    renderer->surface_quality = quality;
    render_material_raster_set_quality(renderer->raster, quality);
    render_material_raster_set_sdf_trace_debug(renderer->raster, renderer->sdf_trace_debug);
    return true;
}

static bool starfield(ecs_world *world, Starfield2D *style)
{
    ecs_component_id component = COMPONENT_Starfield2D.id;
    if (!ecs_world_component(world, component)) {
        return false;
    }
    ecs_query query;
    int result = ecs_query_begin(world, &query, &component, 1);
    assert(result == ECS_OK);
    (void)result;
    ecs_entity entity;
    bool found = ecs_query_next(&query, &entity);
    if (found) {
        *style = *Starfield2D_get_const(world, entity);
    }
    ecs_query_end(&query);
    return found;
}

static void draw_starfield(app_scene_renderer *renderer, Starfield2D *style, CF_Aabb screen)
{
    if (!renderer->background_shader.id) {
        renderer->background_shader = cf_make_draw_shader("starfield.shd");
    }
    CF_V2 center = cf_center(screen);
    CF_V2 span = { screen.max.x - screen.min.x, screen.max.y - screen.min.y };
    cf_draw_push_shader(renderer->background_shader);
    cf_draw_set_uniform("camera_pos", &center, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_draw_set_uniform("world_size", &span, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_draw_set_uniform("far_layer", &style->far_layer, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("middle_layer", &style->middle_layer, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("near_layer", &style->near_layer, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("parallax", &style->parallax, CF_UNIFORM_TYPE_FLOAT3, 1);
    const unsigned colors[] = { style->color_a,
                                style->color_b,
                                style->color_c,
                                style->color_d };
    const char *names[] = { "palette_a", "palette_b", "palette_c", "palette_d" };
    for (int i = 0; i < 4; i++) {
        float rgba[] = { (colors[i] >> 24) / 255.f,
                         ((colors[i] >> 16) & 255) / 255.f,
                         ((colors[i] >> 8) & 255) / 255.f,
                         (colors[i] & 255) / 255.f };
        cf_draw_set_uniform(names[i], rgba, CF_UNIFORM_TYPE_FLOAT4, 1);
    }
    cf_draw_quad_fill(screen, 0);
    cf_draw_pop_shader();
}

/* What shows where no material covers the view: the world's starfield and
   the host's grid, or none. */
static CF_Texture scene_background(
    app_scene_renderer *renderer,
    ecs_world *world,
    const engine_viewport_camera *camera
)
{
    Starfield2D style = { 0 };
    bool stars = starfield(world, &style);
    if (!stars && !renderer->grid) {
        return (CF_Texture){ 0 };
    }
    if (!renderer->background.id) {
        renderer->background = cf_make_canvas(
            cf_canvas_defaults(renderer->width, renderer->height)
        );
        renderer->background_valid = false;
    }
    if (renderer->background_valid &&
        !memcmp(&style, &renderer->background_style, sizeof style) &&
        !memcmp(camera, &renderer->background_camera, sizeof *camera) &&
        renderer->background_grid == renderer->grid) {
        return cf_canvas_get_target(renderer->background);
    }
    renderer->background_style = style;
    renderer->background_camera = *camera;
    renderer->background_grid = renderer->grid;
    renderer->background_valid = true;
    CF_V2 center = { camera->center_x, camera->center_y };
    CF_V2 span = { camera->span_width, camera->span_height };
    CF_Aabb screen = { .min = { center.x - span.x * .5f, center.y - span.y * .5f },
                       .max = { center.x + span.x * .5f, center.y + span.y * .5f } };
    if (stars) {
        draw_starfield(renderer, &style, screen);
    } else {
        cf_draw_push_color(cf_color_black());
        cf_draw_quad_fill(screen, 0);
        cf_draw_pop_color();
    }
    if (renderer->grid > 0) {
        /* The backdrop is linear light the presentation exposes. */
        float exposure = renderer->lit && renderer->lighting.tone_mapping > 0
                           ? renderer->lighting.exposure
                           : 0;
        render_grid_2d_draw(renderer->grid, screen, span.x / (float)renderer->width, exp2f(-exposure));
    }
    cf_render_to(renderer->background, true);
    return cf_canvas_get_target(renderer->background);
}

/* The same batched materials as the lit view, with no light transport. */
static bool draw_unlit(app_scene_renderer *renderer, render_radiance_inputs inputs, CF_V2 flat)
{
    if (!renderer->unlit_shader.id) {
        renderer->unlit_shader = cf_make_draw_shader("unlit.shd");
    }
    if (!renderer->unlit_shader.id) {
        snprintf(renderer->error, sizeof renderer->error, "Unlit material composite could not compile");
        return false;
    }
    CF_RenderState state = cf_render_state_defaults();
    state.blend.enabled = false;
    cf_draw_push_render_state(state);
    cf_draw_push_shader(renderer->unlit_shader);
    cf_draw_set_texture("scene_tex", inputs.material.scene);
    cf_draw_set_texture("source_tex", inputs.material.source);
    cf_draw_set_texture("cover_tex", inputs.material.cover);
    cf_draw_set_texture(
        "background_tex",
        inputs.background.id ? inputs.background : inputs.material.scene
    );
    cf_draw_set_uniform_v2("lighting", flat);
    cf_draw_set_uniform_float("background_present", inputs.background.id != 0);
    cf_draw_push_filter(CF_DRAW_FILTER_LINEAR);
    render_draw_fullscreen(render_material_raster_canvas(renderer->raster));
    cf_draw_pop_filter();
    cf_draw_pop_shader();
    cf_canvas_set_clear_color(renderer->output, cf_make_color_rgba_f(0, 0, 0, 1));
    cf_render_to(renderer->output, true);
    RENDER_DRAW_UNBIND_TEXTURES("scene_tex", "source_tex", "cover_tex", "background_tex");
    cf_draw_pop_render_state();
    render_gpu_timing_mark("Unlit composite");
    return true;
}

void app_scene_renderer_set_grid(app_scene_renderer *renderer, float increment)
{
    assert(isfinite(increment) && increment >= 0);
    renderer->grid = increment;
}

bool app_scene_renderer_draw(
    app_scene_renderer *renderer,
    ecs_world *world,
    const particles *particles,
    engine_viewport *viewport,
    const engine *runtime,
    render_skip skip
)
{
    if (!renderer) {
        return false;
    }
    initialize_resources(renderer);
    renderer->skip = skip;
    double gpu_ms;
    while (renderer->lit && renderer->radiance &&
           render_gpu_span_take(&renderer->span, &gpu_ms)) {
        render_radiance_frame_time(renderer->radiance, gpu_ms);
    }
    /* Without GPU timestamps, as on the web, adaptive lighting judges the
     * whole frame's interval, which includes the rest of the frame's work
     * and the wait for the display. */
    if (renderer->lit && renderer->radiance && !render_gpu_span_available()) {
        uint64_t now = cf_get_ticks();
        if (renderer->frame_start) {
            double ms = (double)(now - renderer->frame_start) * 1000 /
                        (double)cf_get_tick_frequency();
            render_radiance_frame_interval(renderer->radiance, ms);
        }
        renderer->frame_start = now;
    }
    engine_viewport_camera camera;
    engine_viewport_get_camera(viewport, &camera);
    int width, height;
    renderer->error[0] = 0;
    /* Hosts may draw edits, loads and hand-built worlds that no engine frame
     * has propagated yet, so drawing starts from current global transforms. */
    int propagated = transform_propagate(world);
    if (propagated != ECS_OK) {
        snprintf(renderer->error, sizeof renderer->error, "Transform propagation failed (%d).", propagated);
        return false;
    }
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
    if (!renderer->raster || (!renderer->radiance && renderer->lit)) {
        /* This is a supported rendering path, not a scene error. Its status is
         * available in the renderer diagnostics without covering the game. */
        renderer->error[0] = '\0';
        renderer->pixel_aa = fmaxf(camera.span_width / width, camera.span_height / height) *
                             0.5f;
        if (!collect(renderer, world, particles, runtime, padded_view(&camera, width, height, 0))) {
            return false;
        }
        if (acount(renderer->meshes)) {
            snprintf(renderer->error, sizeof renderer->error, "3D surfaces require the HRC material renderer");
            return false;
        }
        if (!prepare_fragments(renderer)) {
            return false;
        }
        if (renderer->grid > 0) {
            CF_Aabb view = { .min = { camera.center_x - camera.span_width * .5f,
                                      camera.center_y - camera.span_height * .5f },
                             .max = { camera.center_x + camera.span_width * .5f,
                                      camera.center_y + camera.span_height * .5f } };
            render_grid_2d_draw(renderer->grid, view, camera.span_width / (float)width, 0);
        }
        for (int i = 0; i < acount(renderer->entities); i++) {
            render_shader_fragment_draw(
                renderer->fragment,
                renderer->fragment_programs[i],
                &renderer->entities[i],
                flat
            );
        }
        return draw_fog(renderer, runtime, &camera, NULL);
    }
    /* An unlit frame needs only the visible materials. No offscreen lighting
     * margin or work-grid snapping, and no lighting target allocation. */
    render_view_2d_frame frame = {
        .raster_width = width,
        .raster_height = height,
        .view = {
            .min_x = camera.center_x - camera.span_width * .5f,
            .max_y = camera.center_y + camera.span_height * .5f,
            .width = camera.span_width,
            .height = camera.span_height,
            .crop_max_u = 1,
            .crop_max_v = 1,
        },
    };
    render_view_2d_camera camera_view = {
        .min_x = camera.center_x - camera.span_width * .5f,
        .max_y = camera.center_y + camera.span_height * .5f,
        .width = camera.span_width,
        .height = camera.span_height,
        .output_width = width,
        .output_height = height,
    };
    if (renderer->lit) {
        render_radiance_settings radiance = render_radiance_get_settings(
            renderer->radiance
        );
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
    }
    int raster_width = frame.raster_width;
    int raster_height = frame.raster_height;
    /* Unlit frames can resize the materials without resizing HRC. */
    if (renderer->lit &&
        !render_radiance_resize(renderer->radiance, raster_width, raster_height)) {
        return false;
    }
    if (renderer->width != width || renderer->height != height ||
        renderer->raster_width != raster_width ||
        renderer->raster_height != raster_height) {
        if (!render_material_raster_resize(renderer->raster, raster_width, raster_height)) {
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
    view.focal_distance = camera.focal_distance;
    view.yaw = camera.yaw;
    view.pitch = camera.pitch;
    view.roll = camera.roll;
    view.target_elevation = camera.target_elevation;
    view.center_x = camera.center_x;
    view.center_y = camera.center_y;
    CHIBI_TRACE_ZONE_BEGIN(collect_zone, "Material collect");
    bool collected = scene_surface_quality(renderer, world) &&
                     collect(renderer, world, particles, runtime, view);
    CHIBI_TRACE_ZONE_END(collect_zone);
    if (!collected) {
        return false;
    }
    CHIBI_TRACE_ZONE_BEGIN(raster_zone, "Material raster submission");
    if (renderer->lit) {
        render_gpu_span_begin(&renderer->span);
    }
    render_gpu_timing_start();
    bool drawn = render_material_raster_draw_mixed(
        renderer->raster,
        renderer->entities,
        acount(renderer->entities),
        view,
        world,
        renderer->meshes,
        acount(renderer->meshes),
        renderer->time
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

    const materials *materials = engine_materials(runtime);
    if (materials && materials_fluid(materials)) {
        if (!renderer->fluid) {
            renderer->fluid = render_fluid_raster_new();
        }
        if (!renderer->fluid || !render_fluid_raster_draw(
                                    renderer->fluid,
                                    materials_fluid(materials),
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
    render_spot_lights spots = { 0 };
    if (renderer->lit && !render_spot_lights_collect(
                             world,
                             runtime,
                             view,
                             &spots,
                             renderer->error,
                             sizeof renderer->error
                         )) {
        return false;
    }
    render_radiance_inputs inputs = { .material = textures,
                                      .spots = spots.lights,
                                      .spot_count = spots.count };
    inputs.background = scene_background(renderer, world, &camera);
    render_gpu_timing_mark("Background");
    bool hdr = renderer->lit && renderer->lighting.tone_mapping > 0;
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
    bool composed = renderer->lit
                      ? render_radiance_draw(renderer->radiance, inputs, view, composite)
                      : draw_unlit(renderer, inputs, flat);
    if (!composed) {
        return false;
    }
    if (hdr) {
        render_post_2d_present(
            renderer->hdr,
            renderer->output,
            renderer->lighting.exposure,
            renderer->lighting.bloom,
            fmodf(renderer->time * 60, 64)
        );
        render_gpu_timing_mark("Tone map and bloom");
    }
    CF_Canvas output = renderer->output;
    if (acount(renderer->reflectors)) {
        if (!renderer->reflections) {
            renderer->reflections = render_reflections_new();
        }
        render_view_2d screen = { .min_x = camera.center_x - camera.span_width * .5f,
                                  .max_y = camera.center_y + camera.span_height * .5f,
                                  .width = camera.span_width,
                                  .height = camera.span_height };
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
    if (renderer->lit) {
        render_gpu_span_end(&renderer->span);
    }
    cf_draw_canvas(
        output,
        (CF_V2){ camera.center_x, camera.center_y },
        (CF_V2){ camera.span_width, camera.span_height }
    );
    return draw_fog(renderer, runtime, &camera, NULL);
}

void app_scene_renderer_set_parallel(app_scene_renderer *renderer, bool enabled)
{
    renderer->parallel_disabled = !enabled;
    if (renderer->scene3d) {
        render_scene3d_set_parallel(renderer->scene3d, enabled);
    }
}

render_scene3d_work app_scene_renderer_last_work(const app_scene_renderer *renderer)
{
    return renderer->scene3d ? render_scene3d_last_work(renderer->scene3d)
                             : (render_scene3d_work){ 0 };
}

void app_scene_renderer_set_model_inspection(
    app_scene_renderer *r,
    ecs_entity entity,
    int part,
    bool section,
    const float normal[3],
    float offset
)
{
    assert(part >= -1 && isfinite(offset));
    r->inspection = (render_model_inspection){ .entity = entity,
                                               .part = part,
                                               .section = section,
                                               .offset = offset };
    if (section) {
        float length = sqrtf(
            normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]
        );
        assert(isfinite(length) && length > 0);
        for (int i = 0; i < 3; i++) {
            r->inspection.normal[i] = normal[i] / length;
        }
    }
    if (r->raster) {
        render_material_raster_set_model_inspection(r->raster, &r->inspection);
    }
}

void app_scene_renderer_set_sdf_batch_limit(app_scene_renderer *renderer, int limit)
{
    assert(limit >= 0 && limit <= RENDER_SDF_BATCH_LIMIT);
    renderer->sdf_batch_limit = limit;
    if (renderer->raster) {
        render_material_raster_set_sdf_batch_limit(renderer->raster, limit);
    }
}

void app_scene_renderer_set_hidden_rejection(app_scene_renderer *renderer, bool enabled)
{
    renderer->hidden_rejection = enabled;
    if (renderer->raster) {
        render_material_raster_set_hidden_rejection(renderer->raster, enabled);
    }
}

void app_scene_renderer_set_spatial_scratch(app_scene_renderer *renderer, bool enabled)
{
    renderer->spatial_scratch = enabled;
    if (renderer->raster) {
        render_material_raster_set_spatial_scratch(renderer->raster, enabled);
    }
}

void app_scene_renderer_set_surface_quality(app_scene_renderer *renderer, SurfaceQuality quality)
{
    renderer->surface_quality = quality;
    renderer->quality_override = true;
}
void app_scene_renderer_set_sdf_trace_debug(app_scene_renderer *renderer, bool enabled)
{
    renderer->sdf_trace_debug = enabled;
}
void app_scene_renderer_set_shadow_mode(app_scene_renderer *renderer, HRCShadowMode mode)
{
    assert(mode >= HRC_SHADOW_REFERENCE && mode <= HRC_SHADOW_HORIZON8);
    renderer->shadow_mode = mode;
    renderer->shadow_override = true;
}
void app_scene_renderer_set_lab(app_scene_renderer *renderer, render_radiance_lab lab)
{
    renderer->lab = lab;
}

render_surface_work app_scene_renderer_surface_work(const app_scene_renderer *renderer)
{
    return renderer->radiance && renderer->lit
             ? render_radiance_surface_work(renderer->radiance)
             : (render_surface_work){ 0 };
}

render_horizon_work app_scene_renderer_horizon_work(const app_scene_renderer *renderer)
{
    return renderer->radiance && renderer->lit
             ? render_radiance_horizon_work(renderer->radiance)
             : (render_horizon_work){ 0 };
}
render_mesh_material_work app_scene_renderer_mesh_work(const app_scene_renderer *renderer)
{
    return renderer->raster ? render_material_raster_mesh_work(renderer->raster)
                            : (render_mesh_material_work){ 0 };
}
