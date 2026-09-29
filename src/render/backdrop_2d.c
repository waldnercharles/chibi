#include "render/backdrop_2d.h"

#include "render/draw_textures.h"
#include "render/fullscreen.h"
#include "render/shader_catalog.h"
#include "render/shader_program.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int material, layer;
    uint32_t order;
    float parallax, intensity;
    bool lit;
    float values[SHADER_PARAMETER_MAX][4];
} backdrop_layer;

/* Everything besides the layers that a drawn image depends on. */
typedef struct
{
    float camera[4], crop[4], time;
    unsigned revision;
} backdrop_key;

struct render_backdrops
{
    render_programs *programs;
    backdrop_layer *layers, *drawn;
    /* The drawn image, and a canvas that only sizes fullscreen quads. */
    CF_Canvas canvas, quad;
    int width, height;
    backdrop_key key;
    char error[256];
};

render_backdrops *render_backdrops_new()
{
    render_backdrops *b = xcalloc(1, sizeof *b);
    b->programs = render_programs_new(RENDER_PROGRAM_BACKDROP);
    b->quad = cf_make_canvas(cf_canvas_defaults(2, 2));
    return b;
}

void render_backdrops_free(render_backdrops *b)
{
    if (!b) {
        return;
    }
    render_programs_free(b->programs);
    if (b->canvas.id) {
        cf_destroy_canvas(b->canvas);
    }
    if (b->quad.id) {
        cf_destroy_canvas(b->quad);
    }
    afree(b->layers);
    afree(b->drawn);
    free(b);
}

const char *render_backdrops_error(const render_backdrops *b)
{
    return b->error;
}

static int compare_layers(const void *left, const void *right)
{
    const backdrop_layer *a = left, *b = right;
    if (a->layer != b->layer) {
        return (a->layer > b->layer) - (a->layer < b->layer);
    }
    return (a->order > b->order) - (a->order < b->order);
}

bool render_backdrops_collect(render_backdrops *b, ecs_world *world)
{
    aclear(b->layers);
    ecs_component_id component = GAME_COMPONENT_GameBackdrop2D.id;
    ecs_query query;
    if (!ecs_world_component(world, component) ||
        ecs_query_begin(world, &query, &component, 1) != ECS_OK) {
        return false;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const GameBackdrop2D *backdrop = GAME_GameBackdrop2D_get_const(world, entity);
        /* Layers compare bytewise, padding included. */
        backdrop_layer layer;
        memset(&layer, 0, sizeof layer);
        layer.material = render_shader_find(backdrop->material);
        layer.layer = backdrop->layer;
        layer.order = entity.slot;
        layer.parallax = backdrop->parallax;
        layer.intensity = backdrop->intensity;
        layer.lit = backdrop->lit;
        if (!layer.material || render_shader_is_shape(layer.material)) {
            continue;
        }
        render_shader_parameters(layer.material, world, entity, NULL, layer.values);
        apush(b->layers, layer);
    }
    ecs_query_end(&query);
    if (acount(b->layers) > 1) {
        qsort(b->layers, (size_t)acount(b->layers), sizeof *b->layers, compare_layers);
    }
    return acount(b->layers) > 0;
}

static bool resize(render_backdrops *b, int width, int height)
{
    if (b->canvas.id && b->width == width && b->height == height) {
        return true;
    }
    if (b->canvas.id) {
        cf_destroy_canvas(b->canvas);
    }
    CF_CanvasParams params = cf_canvas_defaults(width, height);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    params.target.filter = CF_FILTER_LINEAR;
    params.target.wrap_u = params.target.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    b->canvas = cf_make_canvas(params);
    b->width = width;
    b->height = height;
    aclear(b->drawn);
    if (b->canvas.id) {
        cf_canvas_set_clear_color(b->canvas, (CF_Color){ 0, 0, 0, 0 });
    }
    return b->canvas.id != 0;
}

/* Whether the image drawn last still shows these layers in this frame. */
static bool unchanged(const render_backdrops *b, const backdrop_key *key)
{
    if (acount(b->drawn) != acount(b->layers) ||
        memcmp(b->drawn, b->layers, (size_t)acount(b->layers) * sizeof *b->layers) ||
        memcmp(&b->key, key, sizeof *key)) {
        return false;
    }
    for (int i = 0; i < acount(b->layers); i++) {
        if (b->layers[i].lit) {
            return false;
        }
    }
    return true;
}

static backdrop_key frame_key(const render_backdrops *b, const render_backdrop_frame *frame)
{
    render_view_2d c = frame->camera;
    /* Keys compare bytewise, padding included. */
    backdrop_key key;
    memset(&key, 0, sizeof key);
    memcpy(
        key.camera,
        (float[]){ c.min_x, c.max_y, c.width, c.height },
        sizeof key.camera
    );
    memcpy(
        key.crop,
        (float[]){ c.crop_min_u, c.crop_min_v, c.crop_max_u, c.crop_max_v },
        sizeof key.crop
    );
    key.revision = render_shader_revision();
    for (int i = 0; i < acount(b->layers); i++) {
        if (render_shader_animated(b->layers[i].material)) {
            key.time = frame->time;
        }
    }
    return key;
}

static bool draw_layer(render_backdrops *b, const backdrop_layer *layer, const render_backdrop_frame *frame)
{
    render_shader_set set = { 0 };
    shader_error error = { 0 };
    int program = render_shader_set_add(&set, layer->material, &error)
                    ? render_programs_prepare(b->programs, &set)
                    : 0;
    if (!program) {
        snprintf(
            b->error,
            sizeof b->error,
            "Backdrop: %s",
            error.message[0] ? error.message : render_programs_error(b->programs)
        );
        return false;
    }
    render_view_2d c = frame->camera;
    float camera[] = { c.min_x + c.width * .5f, c.max_y - c.height * .5f, c.width, c.height };
    float settings[] = { layer->parallax,
                         layer->intensity,
                         frame->time,
                         (float)layer->material };
    float crop[] = { c.crop_min_u, c.crop_min_v, c.crop_max_u, c.crop_max_v };
    float light[] = { frame->light.map[0],
                      frame->light.map[1],
                      frame->light.openness,
                      layer->lit };
    render_programs_bind2d(b->programs, program);
    cf_draw_set_uniform("sg_camera", camera, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("sg_layer", settings, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("sg_crop", crop, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("sg_light_map", light, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("sg_values", (void *)layer->values, CF_UNIFORM_TYPE_FLOAT4, SHADER_PARAMETER_MAX);
    cf_draw_set_texture("sg_light", frame->light.texture);
    render_draw_fullscreen(b->quad);
    RENDER_DRAW_UNBIND_TEXTURES("sg_light");
    render_programs_unbind2d(b->programs, program);
    return true;
}

CF_Texture render_backdrops_draw(render_backdrops *b, const render_backdrop_frame *frame)
{
    b->error[0] = 0;
    if (!b->quad.id || !resize(b, (frame->width + 1) / 2, (frame->height + 1) / 2)) {
        snprintf(b->error, sizeof b->error, "Backdrop layers need an RGBA16F canvas");
        return (CF_Texture){ 0 };
    }
    backdrop_key key = frame_key(b, frame);
    if (unchanged(b, &key)) {
        return cf_canvas_get_target(b->canvas);
    }
    cf_push_gpu_label("Backdrop layers");
    CF_RenderState state = cf_render_state_defaults();
    state.blend.rgb_src_blend_factor = CF_BLENDFACTOR_ONE;
    state.blend.rgb_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    state.blend.alpha_src_blend_factor = CF_BLENDFACTOR_ONE;
    state.blend.alpha_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    cf_draw_push_render_state(state);
    bool ok = true;
    for (int i = 0; ok && i < acount(b->layers); i++) {
        ok = draw_layer(b, &b->layers[i], frame);
    }
    cf_draw_pop_render_state();
    cf_render_to(b->canvas, true);
    cf_pop_gpu_label();
    if (!ok) {
        aclear(b->drawn);
        return (CF_Texture){ 0 };
    }
    acopy(b->drawn, b->layers);
    b->key = key;
    return cf_canvas_get_target(b->canvas);
}
