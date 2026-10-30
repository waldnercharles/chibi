#include "render/scene3d_post.h"

#include "render/draw_textures.h"
#include "render/shader_program.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5_webgl.h>
#endif

enum
{
    LEVELS = 14,
    SIDE_MAX = 16384,
    LUMINANCE_SIDE = 16
};
/* The slowest easing the stored adapted value can follow; see
 * scene3d_post_adapt.shd. */
static const float slowest_adaptation = .1f;

typedef struct
{
    CF_Canvas down, horizontal, blurred;
} bloom_level;

typedef struct
{
    CF_Canvas scene;
    bloom_level levels[LEVELS];
    int width, height, count;
} post_targets;

/* Auto exposure meters log luminance into a small grid, then eases a 1x1
 * value toward its mean; the two 1x1 canvases alternate as previous and next. */
typedef struct
{
    CF_Canvas luminance, adapted[2];
    CF_Shader measure, adapt;
    int latest;
    bool valid;
    float time;
} post_exposure;

struct render_scene3d_post
{
    post_targets targets;
    post_exposure exposure;
    CF_Canvas output, pending_output;
    CF_Shader downsample, blur, composite;
    Environment settings;
    bool ready;
    char error[512];
};

render_scene3d_post *render_scene3d_post_new()
{
    return xcalloc(1, sizeof(render_scene3d_post));
}

static void destroy_canvas(CF_Canvas canvas)
{
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
}

static void destroy_targets(post_targets *targets)
{
    destroy_canvas(targets->scene);
    for (int i = 0; i < targets->count; i++) {
        destroy_canvas(targets->levels[i].down);
        destroy_canvas(targets->levels[i].horizontal);
        destroy_canvas(targets->levels[i].blurred);
    }
}

static void destroy_shader(CF_Shader shader)
{
    if (shader.id) {
        cf_destroy_shader(shader);
    }
}

void render_scene3d_post_free(render_scene3d_post *post)
{
    if (!post) {
        return;
    }
    destroy_targets(&post->targets);
    destroy_canvas(post->output);
    destroy_canvas(post->pending_output);
    destroy_canvas(post->exposure.luminance);
    destroy_canvas(post->exposure.adapted[0]);
    destroy_canvas(post->exposure.adapted[1]);
    destroy_shader(post->downsample);
    destroy_shader(post->blur);
    destroy_shader(post->composite);
    destroy_shader(post->exposure.measure);
    destroy_shader(post->exposure.adapt);
    free(post);
}

#ifdef __EMSCRIPTEN__
/* Cute's GLES capability table assumes float color support. Ask WebGL before
 * framebuffer creation, which otherwise asserts on these devices. WebGPU
 * reports its formats itself. */
static bool webgl_float_targets()
{
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = emscripten_webgl_get_current_context();
    return emscripten_webgl_enable_extension(context, "EXT_color_buffer_float") ||
           emscripten_webgl_enable_extension(context, "EXT_color_buffer_half_float");
}
#endif

static bool supports_hdr()
{
#ifdef __EMSCRIPTEN__
    if (cf_query_backend() == CF_BACKEND_TYPE_GLES3 && !webgl_float_targets()) {
        return false;
    }
#endif
    CF_PixelFormat format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    return cf_texture_supports_format(format, CF_TEXTURE_USAGE_SAMPLER_BIT | CF_TEXTURE_USAGE_COLOR_TARGET_BIT) &&
           cf_query_pixel_format(format, CF_PIXELFORMAT_OP_BILINEAR_FILTER) &&
           cf_query_pixel_format(format, CF_PIXELFORMAT_OP_ALPHA_BLENDING);
}

static bool load_shader(render_scene3d_post *post, CF_Shader *shader, const char *file)
{
    if (shader->id) {
        return true;
    }
    char *source = render_shader_source(file);
    if (!source) {
        snprintf(post->error, sizeof post->error, "Cannot read HDR post shader: %s", file);
        return false;
    }
    *shader = cf_make_draw_shader_from_source(source);
    free(source);
    if (!shader->id) {
        snprintf(post->error, sizeof post->error, "%s: %s", file, cf_shader_compile_error());
        return false;
    }
    return true;
}

static CF_Canvas make_canvas(int width, int height, bool hdr, bool depth)
{
    CF_CanvasParams params = cf_canvas_defaults(width, height);
    if (hdr) {
        params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    }
    params.depth_stencil_enable = depth;
    params.target.filter = CF_FILTER_LINEAR;
    params.target.mip_filter = CF_MIP_FILTER_LINEAR;
    params.target.wrap_u = params.target.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    CF_Canvas canvas = cf_make_canvas(params);
    if (canvas.id) {
        cf_canvas_set_clear_color(canvas, (CF_Color){ 0, 0, 0, 1 });
    }
    return canvas;
}

static bool resize(render_scene3d_post *post, int width, int height)
{
    if (post->targets.width == width && post->targets.height == height) {
        return true;
    }
    post_targets next = { .width = width, .height = height };
    next.scene = make_canvas(width, height, true, true);
    CF_Canvas output = make_canvas(width, height, false, false);
    bool ok = next.scene.id && output.id;
    do {
        width = (width + 1) / 2;
        height = (height + 1) / 2;
        bloom_level *level = &next.levels[next.count++];
        level->down = make_canvas(width, height, true, false);
        level->horizontal = make_canvas(width, height, true, false);
        level->blurred = make_canvas(width, height, true, false);
        ok = ok && level->down.id && level->horizontal.id && level->blurred.id;
    } while (ok && (width > 1 || height > 1) && next.count < LEVELS);
    if (!ok) {
        destroy_targets(&next);
        destroy_canvas(output);
        snprintf(post->error, sizeof post->error, "Cannot allocate HDR post-processing targets.");
        return false;
    }
    destroy_targets(&post->targets);
    destroy_canvas(post->pending_output);
    post->targets = next;
    post->pending_output = output;
    return true;
}

/* The adapted value is a half-float pair; see scene3d_post_adapt.shd. */
static CF_Canvas make_adapted_canvas()
{
    CF_CanvasParams params = cf_canvas_defaults(1, 1);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    params.target.filter = CF_FILTER_NEAREST;
    return cf_make_canvas(params);
}

static bool prepare_exposure(render_scene3d_post *post)
{
    post_exposure *exposure = &post->exposure;
    if (!exposure->luminance.id) {
        exposure->luminance = make_canvas(LUMINANCE_SIDE, LUMINANCE_SIDE, true, false);
    }
    for (int i = 0; i < 2; i++) {
        if (!exposure->adapted[i].id) {
            exposure->adapted[i] = make_adapted_canvas();
        }
    }
    if (!exposure->luminance.id || !exposure->adapted[0].id ||
        !exposure->adapted[1].id) {
        snprintf(post->error, sizeof post->error, "Cannot allocate auto exposure targets.");
        return false;
    }
    return load_shader(post, &exposure->measure, "scene3d_post_luminance.shd") &&
           load_shader(post, &exposure->adapt, "scene3d_post_adapt.shd");
}

static bool in_range(float value, float low, float high)
{
    return isfinite(value) && value >= low && value <= high;
}

bool render_scene3d_post_prepare(render_scene3d_post *post, int width, int height, const Environment *settings)
{
    assert(post && settings);
    post->ready = false;
    post->error[0] = 0;
    if (!in_range(settings->exposure, -16, 16) ||
        !in_range(settings->bloom_strength, 0, 8) ||
        !in_range(settings->bloom_threshold, 0, 65504) ||
        !in_range(settings->bloom_radius, 0, 0.25f) ||
        (settings->adaptation_speed != 0 &&
         !in_range(settings->adaptation_speed, slowest_adaptation, 100)) ||
        (unsigned)settings->tone_mapping > TONE_MAPPING_NEUTRAL) {
        snprintf(
            post->error,
            sizeof post->error,
            "Environment requires finite exposure [-16,16] EV, bloom strength "
            "[0,8], threshold [0,65504], radius [0,0.25], adaptation speed 0 "
            "or [0.1,100], and a known tone mapping."
        );
        return false;
    }
    if (width < 1 || height < 1 || width > SIDE_MAX || height > SIDE_MAX) {
        snprintf(post->error, sizeof post->error, "HDR target dimensions must be 1 through %d.", SIDE_MAX);
        return false;
    }
    if (!supports_hdr()) {
        snprintf(
            post->error,
            sizeof post->error,
            "HDR rendering requires a filterable, blendable RGBA16F render "
            "target "
            "(WebGL: EXT_color_buffer_float or EXT_color_buffer_half_float)."
        );
        return false;
    }
    if (!load_shader(post, &post->downsample, "scene3d_post_downsample.shd") ||
        !load_shader(post, &post->blur, "scene3d_post_blur.shd") ||
        !load_shader(post, &post->composite, "scene3d_post_composite.shd") ||
        (settings->auto_exposure && !prepare_exposure(post)) ||
        !resize(post, width, height)) {
        return false;
    }
    post->settings = *settings;
    post->ready = true;
    return true;
}

CF_Canvas render_scene3d_post_target(const render_scene3d_post *post)
{
    assert(post->ready);
    return post->targets.scene;
}

CF_Canvas render_scene3d_post_output(const render_scene3d_post *post)
{
    return post->output;
}

void render_scene3d_post_reset(render_scene3d_post *post)
{
    post->exposure.valid = false;
}

const char *render_scene3d_post_error(const render_scene3d_post *post)
{
    return post->error;
}

static void pass(CF_Canvas source, CF_Canvas destination)
{
    cf_draw_canvas(source, (CF_V2){ 0 }, (CF_V2){ 2, 2 });
    cf_render_to(destination, true);
}

static void downsample(render_scene3d_post *post, int last)
{
    CF_Canvas source = post->targets.scene;
    cf_draw_push_shader(post->downsample);
    for (int i = 0; i <= last; i++) {
        int width, height;
        cf_canvas_get_size(source, &width, &height);
        cf_draw_set_uniform_v2("source_texel", (CF_V2){ 1.0f / width, 1.0f / height });
        cf_draw_set_uniform_float("threshold", i ? -1 : post->settings.bloom_threshold);
        // Sanitize raw HDR texels before filtering can combine infinities into NaNs.
        cf_draw_push_filter(i ? CF_DRAW_FILTER_LINEAR : CF_DRAW_FILTER_NEAREST);
        pass(source, post->targets.levels[i].down);
        cf_draw_pop_filter();
        source = post->targets.levels[i].down;
    }
    cf_draw_pop_shader();
}

static void blur(render_scene3d_post *post, int index)
{
    bloom_level *level = &post->targets.levels[index];
    float radius = post->settings.bloom_radius;
    float aspect = (float)post->targets.height / post->targets.width;
    cf_draw_push_shader(post->blur);
    cf_draw_set_uniform_v2("step_uv", (CF_V2){ radius * aspect * 0.5f, 0 });
    pass(level->down, level->horizontal);
    cf_draw_set_uniform_v2("step_uv", (CF_V2){ 0, radius * 0.5f });
    pass(level->horizontal, level->blurred);
    cf_draw_pop_shader();
}

static void meter(render_scene3d_post *post, float time)
{
    post_exposure *exposure = &post->exposure;
    float elapsed = time - exposure->time, speed = post->settings.adaptation_speed;
    bool snap = !exposure->valid || elapsed < 0 || speed == 0;
    exposure->time = time;
    exposure->valid = true;
    cf_draw_push_shader(exposure->measure);
    cf_draw_set_uniform_v2("footprint", (CF_V2){ 1.0f / LUMINANCE_SIDE, 1.0f / LUMINANCE_SIDE });
    // Point taps let the shader sanitize each raw HDR texel, as the first bloom downsample does.
    cf_draw_push_filter(CF_DRAW_FILTER_NEAREST);
    pass(post->targets.scene, exposure->luminance);
    cf_draw_pop_filter();
    cf_draw_pop_shader();
    CF_Canvas previous = exposure->adapted[exposure->latest];
    exposure->latest ^= 1;
    cf_draw_push_shader(exposure->adapt);
    cf_draw_set_texture("previous", cf_canvas_get_target(previous));
    cf_draw_set_uniform_float("rate", snap ? 1 : 1 - expf(-elapsed * speed));
    pass(exposure->luminance, exposure->adapted[exposure->latest]);
    RENDER_DRAW_UNBIND_TEXTURES("previous");
    cf_draw_pop_shader();
}

bool render_scene3d_post_process(render_scene3d_post *post, float time)
{
    if (!post->ready) {
        return false;
    }
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

    Environment settings = post->settings;
    bool bloom = settings.bloom_strength > 0 && settings.bloom_radius > 0;
    float lod = fmaxf(0, log2f(fmaxf(settings.bloom_radius * post->targets.height / 4, 1)));
    lod = fminf(lod, post->targets.count - 1);
    int low = (int)floorf(lod), high = (int)ceilf(lod);
    if (bloom) {
        downsample(post, high);
        blur(post, low);
        if (high != low) {
            blur(post, high);
        }
    }
    post->exposure.valid &= settings.auto_exposure;
    if (settings.auto_exposure) {
        meter(post, time);
    }
    CF_Canvas source = post->targets.scene;
    cf_draw_push_shader(post->composite);
    cf_draw_set_texture(
        "bloom_low",
        cf_canvas_get_target(bloom ? post->targets.levels[low].blurred : source)
    );
    cf_draw_set_texture(
        "bloom_high",
        cf_canvas_get_target(bloom ? post->targets.levels[high].blurred : source)
    );
    cf_draw_set_texture(
        "adapted",
        cf_canvas_get_target(
            settings.auto_exposure ? post->exposure.adapted[post->exposure.latest] : source
        )
    );
    cf_draw_set_uniform_float("exposure", settings.exposure);
    cf_draw_set_uniform_float("strength", bloom ? settings.bloom_strength : 0);
    cf_draw_set_uniform_float("level_mix", lod - low);
    cf_draw_set_uniform_int("auto_exposure", settings.auto_exposure);
    cf_draw_set_uniform_int("tone_mapping", settings.tone_mapping);
    cf_draw_push_filter(CF_DRAW_FILTER_NEAREST);
    pass(source, post->pending_output.id ? post->pending_output : post->output);
    RENDER_DRAW_UNBIND_TEXTURES("bloom_low", "bloom_high", "adapted");
    cf_draw_pop_filter();
    cf_draw_pop_shader();

    cf_draw_pop_alpha_discard();
    cf_draw_pop_filter();
    cf_draw_pop_scissor();
    cf_draw_pop_viewport();
    cf_draw_pop_render_state();
    cf_draw_pop();
    if (post->pending_output.id) {
        destroy_canvas(post->output);
        post->output = post->pending_output;
        post->pending_output = (CF_Canvas){ 0 };
    }
    return true;
}
