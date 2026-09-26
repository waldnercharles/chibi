#include "render/post_2d.h"

#include "render/draw_textures.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>

enum
{
    LEVELS = 7,
    SMALLEST = 8
};

struct render_post_2d
{
    CF_Canvas target;
    /* down[i] halves the previous level; up[i] adds the blurred level below. */
    CF_Canvas down[LEVELS], up[LEVELS];
    int width, height, count;
    CF_Shader downsample, upsample, present;
};

static void destroy(CF_Canvas *canvas)
{
    if (canvas->id) {
        cf_destroy_canvas(*canvas);
    }
    *canvas = (CF_Canvas){ 0 };
}

static void destroy_targets(render_post_2d *post)
{
    destroy(&post->target);
    for (int i = 0; i < LEVELS; i++) {
        destroy(&post->down[i]);
        destroy(&post->up[i]);
    }
    post->width = post->height = post->count = 0;
}

render_post_2d *render_post_2d_new()
{
    render_post_2d *post = xcalloc(1, sizeof *post);
    post->downsample = cf_make_draw_shader("post_2d_down.shd");
    post->upsample = cf_make_draw_shader("post_2d_up.shd");
    post->present = cf_make_draw_shader("post_2d_present.shd");
    if (!post->downsample.id || !post->upsample.id || !post->present.id) {
        render_post_2d_free(post);
        return NULL;
    }
    return post;
}

void render_post_2d_free(render_post_2d *post)
{
    if (!post) {
        return;
    }
    destroy_targets(post);
    CF_Shader shaders[] = { post->downsample, post->upsample, post->present };
    for (int i = 0; i < 3; i++) {
        if (shaders[i].id) {
            cf_destroy_shader(shaders[i]);
        }
    }
    free(post);
}

static CF_Canvas hdr_canvas(int width, int height)
{
    CF_CanvasParams params = cf_canvas_defaults(width, height);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    params.target.filter = CF_FILTER_LINEAR;
    params.target.wrap_u = params.target.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    CF_Canvas canvas = cf_make_canvas(params);
    if (canvas.id) {
        cf_canvas_set_clear_color(canvas, (CF_Color){ 0, 0, 0, 1 });
    }
    return canvas;
}

bool render_post_2d_resize(render_post_2d *post, int width, int height)
{
    assert(post && width > 0 && height > 0);
    if (post->width == width && post->height == height) {
        return true;
    }
    destroy_targets(post);
    CF_PixelFormat format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    if (!cf_texture_supports_format(format, CF_TEXTURE_USAGE_SAMPLER_BIT | CF_TEXTURE_USAGE_COLOR_TARGET_BIT) ||
        !cf_query_pixel_format(format, CF_PIXELFORMAT_OP_BILINEAR_FILTER)) {
        return false;
    }
    post->target = hdr_canvas(width, height);
    bool ok = post->target.id != 0;
    int w = width, h = height;
    while (ok && post->count < LEVELS && w / 2 >= SMALLEST && h / 2 >= SMALLEST) {
        w = (w + 1) / 2;
        h = (h + 1) / 2;
        post->down[post->count] = hdr_canvas(w, h);
        post->up[post->count] = hdr_canvas(w, h);
        ok = post->down[post->count].id && post->up[post->count].id;
        post->count++;
    }
    if (!ok || !post->count) {
        destroy_targets(post);
        return false;
    }
    post->width = width;
    post->height = height;
    return true;
}

CF_Canvas render_post_2d_target(const render_post_2d *post)
{
    return post->target;
}

static void pass(CF_Canvas source, CF_Canvas destination)
{
    cf_draw_canvas(source, (CF_V2){ 0 }, (CF_V2){ 2, 2 });
    cf_render_to(destination, true);
}

static CF_V2 texel(CF_Canvas canvas)
{
    int width, height;
    cf_canvas_get_size(canvas, &width, &height);
    return (CF_V2){ 1.0f / width, 1.0f / height };
}

void render_post_2d_present(render_post_2d *post, CF_Canvas output, float exposure, float bloom, float dither_phase)
{
    assert(post && post->count && output.id);
    cf_push_gpu_label("2D HDR present");
    cf_draw_push();
    cf_draw_TSR_absolute((CF_V2){ 0 }, (CF_V2){ 1, 1 }, 0);
    cf_draw_projection(cf_ortho_2d(0, 0, 2, 2));
    CF_RenderState state = cf_render_state_defaults();
    state.blend.enabled = false;
    cf_draw_push_render_state(state);
    cf_draw_push_viewport((CF_Rect){ 0, 0, -1, -1 });
    cf_draw_push_scissor((CF_Rect){ 0, 0, -1, -1 });
    cf_draw_push_filter(CF_DRAW_FILTER_LINEAR);
    cf_draw_push_alpha_discard(false);
    if (bloom > 0) {
        /* Downsample to the smallest level, then add each blurred level back
         * up the chain; the first level suppresses isolated fireflies. */
        cf_draw_push_shader(post->downsample);
        CF_Canvas source = post->target;
        for (int i = 0; i < post->count; i++) {
            cf_draw_set_uniform_v2("source_texel", texel(source));
            cf_draw_set_uniform_float("karis", i == 0);
            pass(source, post->down[i]);
            source = post->down[i];
        }
        cf_draw_pop_shader();
        cf_draw_push_shader(post->upsample);
        CF_Canvas below = post->down[post->count - 1];
        for (int i = post->count - 2; i >= 0; i--) {
            cf_draw_set_uniform_v2("source_texel", texel(below));
            cf_draw_set_texture("current", cf_canvas_get_target(post->down[i]));
            pass(below, post->up[i]);
            below = post->up[i];
        }
        cf_draw_pop_shader();
    }
    CF_Canvas glow = post->count > 1 ? post->up[0] : post->down[0];
    cf_draw_push_shader(post->present);
    cf_draw_set_texture("bloom_tex", cf_canvas_get_target(glow));
    cf_draw_set_uniform_float("exposure", exposure);
    cf_draw_set_uniform_float("bloom", bloom);
    /* Each level carries the frame's full energy; average them. */
    cf_draw_set_uniform_float("bloom_scale", 1.0f / post->count);
    cf_draw_set_uniform_float("dither_phase", dither_phase);
    cf_draw_push_filter(CF_DRAW_FILTER_NEAREST);
    pass(post->target, output);
    RENDER_DRAW_UNBIND_TEXTURES("current", "bloom_tex");
    cf_draw_pop_filter();
    cf_draw_pop_shader();
    cf_draw_pop_alpha_discard();
    cf_draw_pop_filter();
    cf_draw_pop_scissor();
    cf_draw_pop_viewport();
    cf_draw_pop_render_state();
    cf_draw_pop();
    cf_pop_gpu_label();
}
