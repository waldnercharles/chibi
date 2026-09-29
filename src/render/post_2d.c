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
    /* The linear frame, and the spare that passes rewrite it into. */
    CF_Canvas target, spare;
    /* down[i] halves the previous level; up[i] adds the blurred level below. */
    CF_Canvas down[LEVELS], up[LEVELS];
    int width, height, count;
    CF_Shader downsample, upsample, present, distort;
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
    destroy(&post->spare);
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
    post->distort = cf_make_draw_shader("post_2d_distort.shd");
    if (!post->downsample.id || !post->upsample.id || !post->present.id ||
        !post->distort.id) {
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
    CF_Shader shaders[] = { post->downsample, post->upsample, post->present, post->distort };
    for (int i = 0; i < 4; i++) {
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

CF_Canvas render_post_2d_spare(render_post_2d *post)
{
    if (!post->spare.id) {
        post->spare = hdr_canvas(post->width, post->height);
    }
    return post->spare;
}

void render_post_2d_flip(render_post_2d *post)
{
    CF_Canvas frame = post->target;
    post->target = post->spare;
    post->spare = frame;
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

/* Draws with a fullscreen canvas quad in the unit projection, without
 * blending, filtering linearly. */
static void begin_passes()
{
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
}

static void end_passes()
{
    cf_draw_pop_alpha_discard();
    cf_draw_pop_filter();
    cf_draw_pop_scissor();
    cf_draw_pop_viewport();
    cf_draw_pop_render_state();
    cf_draw_pop();
}

bool render_post_2d_distorts(const render_post_2d_distortion *d)
{
    return d->wave_count || d->haze_count || d->fluid_haze > 0 || d->refraction;
}

bool render_post_2d_distort(render_post_2d *post, const render_post_2d_distortion *d)
{
    assert(
        post && post->count && d->wave_count <= RENDER_POST_2D_WAVES &&
        d->haze_count <= RENDER_POST_2D_HAZES
    );
    CF_Canvas spare = render_post_2d_spare(post);
    if (!spare.id) {
        return false;
    }
    cf_push_gpu_label("2D distortion");
    begin_passes();
    cf_draw_push_shader(post->distort);
    cf_draw_set_texture("cover_tex", d->cover);
    render_view_2d v = d->view;
    float view[] = { v.min_x, v.max_y, v.width, v.height };
    float crop[] = { v.crop_min_u, v.crop_min_v, v.crop_max_u, v.crop_max_v };
    cf_draw_set_uniform("view", view, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("crop_uv", crop, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform_v2("output_texel", texel(post->target));
    cf_draw_set_uniform_float("time", d->time);
    cf_draw_set_uniform_float("fluid_haze", d->fluid_haze);
    cf_draw_set_uniform_float("refraction", d->refraction);
    cf_draw_set_uniform_int("wave_count", d->wave_count);
    cf_draw_set_uniform_int("haze_count", d->haze_count);
    cf_draw_set_uniform("waves", (void *)d->waves, CF_UNIFORM_TYPE_FLOAT4, 2 * RENDER_POST_2D_WAVES);
    cf_draw_set_uniform("hazes", (void *)d->hazes, CF_UNIFORM_TYPE_FLOAT4, RENDER_POST_2D_HAZES);
    pass(post->target, spare);
    RENDER_DRAW_UNBIND_TEXTURES("cover_tex");
    cf_draw_pop_shader();
    end_passes();
    cf_pop_gpu_label();
    render_post_2d_flip(post);
    return true;
}

static void bloom_passes(render_post_2d *post)
{
    /* Downsample to the smallest level, then add each blurred level back up
     * the chain; the first level suppresses isolated fireflies. */
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

/* White balance as linear channel gains, from cool/warm and green/magenta. */
static CF_V3 white_balance(float temperature, float tint)
{
    return (CF_V3){ 1 + .2f * temperature + .1f * tint,
                    1 - .2f * tint,
                    1 - .2f * temperature + .1f * tint };
}

void render_post_2d_present(render_post_2d *post, CF_Canvas output, const render_post_2d_settings *s)
{
    assert(post && post->count && output.id);
    cf_push_gpu_label("2D HDR present");
    begin_passes();
    if (s->bloom > 0) {
        bloom_passes(post);
    }
    CF_Canvas glow = post->count > 1 ? post->up[0] : post->down[0];
    const GamePost2D *look = &s->look;
    cf_draw_push_shader(post->present);
    cf_draw_set_texture("bloom_tex", cf_canvas_get_target(glow));
    cf_draw_set_uniform_float("exposure", s->exposure);
    cf_draw_set_uniform_float("bloom", s->bloom);
    /* Each level carries the frame's full energy; average them. */
    cf_draw_set_uniform_float("bloom_scale", 1.0f / post->count);
    cf_draw_set_uniform_float("dither_phase", s->dither_phase);
    CF_V3 balance = white_balance(look->temperature, look->tint);
    float grade[] = { look->vignette, look->chromatic_aberration, look->grain, 0 };
    float tone[] = { 1 + look->contrast, 1 + look->saturation, 0, 0 };
    float lift[] = { look->lift.x, look->lift.y, look->lift.z, 0 };
    float gain[] = { 1 + look->gain.x, 1 + look->gain.y, 1 + look->gain.z, 0 };
    float white[] = { balance.x, balance.y, balance.z, 0 };
    cf_draw_set_uniform("lens", grade, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("tone", tone, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("lift", lift, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("gain", gain, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("white", white, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("flash", (void *)s->flash, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_push_filter(CF_DRAW_FILTER_NEAREST);
    pass(post->target, output);
    RENDER_DRAW_UNBIND_TEXTURES("current", "bloom_tex");
    cf_draw_pop_filter();
    cf_draw_pop_shader();
    end_passes();
    cf_pop_gpu_label();
}
