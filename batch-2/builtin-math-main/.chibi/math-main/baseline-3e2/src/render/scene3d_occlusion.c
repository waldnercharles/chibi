#include "render/scene3d_occlusion.h"

#include "platform/cute.h"
#include "render/fullscreen.h"
#include "render/scene3d_shadow.h"
#include "render/shader_program.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    /* Keeps the depth pass apart from anything else queued for drawing. */
    OCCLUSION_LAYER = INT_MIN / 2 + 2
};

/* Each quality's slices, steps along each side of a slice, and frame pixels
 * per occlusion texel along each axis. */
typedef struct
{
    int slices, steps, scale;
} search_quality;

static const search_quality qualities[] = {
    [OCCLUSION_LOW] = { 2, 4, 2 },
    [OCCLUSION_MEDIUM] = { 3, 6, 2 },
    [OCCLUSION_HIGH] = { 4, 8, 1 },
};

/* The widest search as a share of the frame's height. A radius that reaches
 * farther across the screen, close to the camera, searches only this far, so
 * that its few samples stay near enough to agree. */
static const float widest_search = .25f;

struct render_scene3d_occlusion
{
    render_scene3d_depth_map depth;
    /* The occlusion at its quality's scale, then blurred at the frame's. */
    CF_Canvas raw, blurred;
    CF_Texture open; /* One unoccluded texel, bound where none was computed. */
    CF_Shader depth_shader, search, blur;
    CF_Material material;
    CF_Sampler point;
    CF_Mesh triangle;
    Occlusion quality;
    float radius;
    int width, height, scale;
    CF_M4x4 projection;
};

render_scene3d_occlusion *render_scene3d_occlusion_new()
{
    return xcalloc(1, sizeof(render_scene3d_occlusion));
}

static void free_targets(render_scene3d_occlusion *occlusion)
{
    if (!occlusion->width) {
        return;
    }
    render_scene3d_depth_map_free(&occlusion->depth);
    occlusion->depth = (render_scene3d_depth_map){ 0 };
    cf_destroy_canvas(occlusion->raw);
    cf_destroy_canvas(occlusion->blurred);
    occlusion->width = occlusion->height = 0;
}

void render_scene3d_occlusion_free(render_scene3d_occlusion *occlusion)
{
    free_targets(occlusion);
    if (occlusion->open.id) {
        cf_destroy_texture(occlusion->open);
    }
    if (occlusion->depth_shader.id) {
        cf_destroy_shader(occlusion->depth_shader);
    }
    if (occlusion->search.id) {
        cf_destroy_shader(occlusion->search);
        cf_destroy_shader(occlusion->blur);
        cf_destroy_material(occlusion->material);
        cf_destroy_sampler(occlusion->point);
        cf_destroy_mesh(occlusion->triangle);
    }
    free(occlusion);
}

static CF_Canvas make_target(int width, int height, CF_PixelFormat format)
{
    CF_CanvasParams params = cf_canvas_defaults(width, height);
    /* Every device the HDR pass runs on can render half floats. */
    params.target.pixel_format = format;
    params.target.filter = CF_FILTER_NEAREST;
    params.target.wrap_u = params.target.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    return cf_make_canvas(params);
}

/* Targets exactly the frame's size, so the passes and the lit pass address
 * the same pixels by their fragment coordinates. */
static void resize(render_scene3d_occlusion *occlusion, int width, int height, int scale)
{
    if (occlusion->width == width && occlusion->height == height &&
        occlusion->scale == scale) {
        return;
    }
    free_targets(occlusion);
    render_scene3d_depth_map_prepare(&occlusion->depth, width, height);
    occlusion->raw = make_target((width + scale - 1) / scale, (height + scale - 1) / scale, CF_PIXEL_FORMAT_R16_FLOAT);
    /* The lit pass reads screen light in rgb and occlusion in alpha. */
    occlusion->blurred = make_target(width, height, CF_PIXEL_FORMAT_R16G16B16A16_FLOAT);
    occlusion->width = width;
    occlusion->height = height;
    occlusion->scale = scale;
}

bool render_scene3d_occlusion_prepare(
    render_scene3d_occlusion *occlusion,
    const Environment *environment,
    int width,
    int height,
    char *error,
    size_t size
)
{
    occlusion->quality = OCCLUSION_OFF;
    if ((unsigned)environment->occlusion > OCCLUSION_HIGH) {
        snprintf(error, size, "Environment occlusion must be Off, Low, Medium or High.");
        return false;
    }
    if (environment->occlusion == OCCLUSION_OFF) {
        return true;
    }
    float radius = environment->occlusion_radius;
    if (!isfinite(radius) || radius <= 0) {
        snprintf(error, size, "Environment occlusion radius must be positive and finite.");
        return false;
    }
    assert(width > 0 && height > 0);
    resize(occlusion, width, height, qualities[environment->occlusion].scale);
    occlusion->quality = environment->occlusion;
    occlusion->radius = radius;
    return true;
}

bool render_scene3d_occlusion_active(const render_scene3d_occlusion *occlusion)
{
    return occlusion->quality != OCCLUSION_OFF;
}

static bool load_passes(render_scene3d_occlusion *occlusion, char *error, size_t size)
{
    if (occlusion->search.id) {
        return true;
    }
    CF_Shader search = render_pass_shader("scene3d_occlusion.frag", error, size);
    if (!search.id) {
        return false;
    }
    CF_Shader blur = render_pass_shader("scene3d_occlusion_blur.frag", error, size);
    if (!blur.id) {
        cf_destroy_shader(search);
        return false;
    }
    occlusion->search = search;
    occlusion->blur = blur;
    occlusion->material = cf_make_material();
    CF_SamplerParams point = cf_sampler_defaults();
    point.filter = CF_FILTER_NEAREST;
    point.mip_filter = CF_MIP_FILTER_NEAREST;
    point.wrap_u = point.wrap_v = point.wrap_w = CF_WRAP_MODE_CLAMP_TO_EDGE;
    occlusion->point = cf_make_sampler(point);
    occlusion->triangle = render_make_pass_mesh();
    return true;
}

bool render_scene3d_occlusion_begin(
    render_scene3d_occlusion *occlusion,
    const render_scene3d_camera *camera,
    char *error,
    size_t size
)
{
    assert(render_scene3d_occlusion_active(occlusion));
    if (!render_scene3d_depth_shader(&occlusion->depth_shader, error, size) ||
        !load_passes(occlusion, error, size)) {
        return false;
    }
    occlusion->projection = camera->projection;
    cf_draw_push_layer(OCCLUSION_LAYER);
    cf_draw_push_viewport((CF_Rect){ 0, 0, occlusion->width, occlusion->height });
    cf_draw3d_push_shader(occlusion->depth_shader);
    cf_draw3d_push_projection(camera->projection);
    cf_draw3d_push_view(camera->view);
    return true;
}

/* Runs one pass over every texel of a target. */
static void pass(render_scene3d_occlusion *occlusion, CF_Canvas target, CF_Shader shader)
{
    int width, height;
    cf_canvas_get_size(target, &width, &height);
    /* Every texel is rewritten, so nothing need load. */
    cf_apply_canvas(target, true);
    cf_apply_mesh(occlusion->triangle);
    cf_apply_shader(shader, occlusion->material);
    cf_apply_viewport(0, 0, width, height);
    cf_apply_scissor(0, 0, width, height);
    cf_draw_elements();
}

/* Searches the depth for occlusion at the quality's scale, then blurs it
 * back to the frame's size. */
static void compute(render_scene3d_occlusion *occlusion)
{
    const search_quality *quality = &qualities[occlusion->quality];
    const float *e = occlusion->projection.elements;
    CF_M4x4 unproject = cf_m4_invert(occlusion->projection);
    /* The depth map's size, frame pixels per occlusion texel, and the widest
     * search in pixels. */
    CF_V4 frame = { (float)occlusion->width,
                    (float)occlusion->height,
                    (float)quality->scale,
                    widest_search * (float)occlusion->height };
    /* Pixels per view unit where clip w is one, clip w's change with view z
     * and its constant, and the radius in view units. */
    CF_V4 lens = { e[0] * .5f * (float)occlusion->width, e[11], e[15], occlusion->radius };
    CF_V4 search = { (float)quality->slices, (float)quality->steps, 0, 0 };
    CF_Material material = occlusion->material;
    cf_material_set_uniform_fs(material, "occlusion_unproject", &unproject, CF_UNIFORM_TYPE_MAT4, 1);
    cf_material_set_uniform_fs(material, "occlusion_frame", &frame, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(material, "occlusion_lens", &lens, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(material, "occlusion_search", &search, CF_UNIFORM_TYPE_FLOAT4, 1);
    /* The map compares depths for shadow lookups; these passes read them. */
    cf_material_set_texture_fs_sampler(
        material,
        "depth_map",
        occlusion->depth.map,
        occlusion->point
    );
    cf_material_set_texture_fs_sampler(
        material,
        "occlusion_map",
        cf_canvas_get_target(occlusion->raw),
        occlusion->point
    );
    pass(occlusion, occlusion->raw, occlusion->search);
    pass(occlusion, occlusion->blurred, occlusion->blur);
}

void render_scene3d_occlusion_end(render_scene3d_occlusion *occlusion)
{
    cf_draw3d_pop_view();
    cf_draw3d_pop_projection();
    cf_draw3d_pop_shader();
    cf_draw_pop_viewport();
    cf_draw_pop_layer();
    cf_render_layers_to(occlusion->depth.canvas, OCCLUSION_LAYER, OCCLUSION_LAYER, true);
    compute(occlusion);
}

void render_scene3d_occlusion_bind(render_scene3d_occlusion *occlusion, bool computed)
{
    if (!occlusion->open.id) {
        CF_TextureParams params = cf_texture_defaults(1, 1);
        params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT;
        params.filter = CF_FILTER_NEAREST;
        occlusion->open = cf_make_texture(params);
        CF_Pixel open = { .val = 0xff000000u };
        cf_texture_update(occlusion->open, &open, sizeof open);
    }
    bool shown = computed && render_scene3d_occlusion_active(occlusion);
    cf_draw3d_set_texture(
        "scene_occlusion",
        shown ? cf_canvas_get_target(occlusion->blurred) : occlusion->open
    );
}

CF_Texture render_scene3d_occlusion_depth(const render_scene3d_occlusion *occlusion)
{
    return occlusion->depth.map;
}

CF_Texture render_scene3d_occlusion_map(const render_scene3d_occlusion *occlusion)
{
    return cf_canvas_get_target(occlusion->blurred);
}

void render_scene3d_occlusion_unbind()
{
    cf_draw3d_set_texture("scene_occlusion", (CF_Texture){ 0 });
}
