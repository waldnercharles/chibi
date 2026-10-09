#include "render/scene3d_shadow.h"

#include "platform/cute.h"
#include "render/shader_program.h"
#include "support/xalloc.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    SHADOW_SIZE = 2048
};

struct render_scene3d_shadow
{
    CF_Texture map;
    CF_Canvas canvas;
    CF_Shader shader;
    int size;
    CF_M4x4 view, projection;
    float texel; /* World units per map texel. */
};

render_scene3d_shadow *render_scene3d_shadow_new()
{
    return xcalloc(1, sizeof(render_scene3d_shadow));
}

static void destroy_map(render_scene3d_shadow *shadow)
{
    if (!shadow->size) {
        return;
    }
    cf_destroy_canvas(shadow->canvas);
    cf_destroy_texture(shadow->map);
}

void render_scene3d_shadow_free(render_scene3d_shadow *shadow)
{
    if (!shadow) {
        return;
    }
    destroy_map(shadow);
    if (shadow->shader.id) {
        cf_destroy_shader(shadow->shader);
    }
    free(shadow);
}

/* A cleared depth map of at least `size` texels per side. */
static void prepare_map(render_scene3d_shadow *shadow, int size)
{
    if (shadow->size >= size) {
        return;
    }
    destroy_map(shadow);
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    CF_TextureParams map = params.depth_stencil_target;
    map.width = map.height = size;
    map.usage |= CF_TEXTURE_USAGE_SAMPLER_BIT;
    map.wrap_u = map.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    map.compare_enable = true;
    map.compare_function = CF_COMPARE_FUNCTION_LESS_THAN_OR_EQUAL;
    shadow->map = cf_make_texture(map);
    params.attach_target = shadow->map;
    shadow->canvas = cf_make_canvas(params);
    cf_canvas_set_clear_depth_stencil(shadow->canvas, 1, 0);
    cf_clear_canvas(shadow->canvas);
    shadow->size = size;
}

static bool load_shader(render_scene3d_shadow *shadow, char *error, size_t size)
{
    if (shadow->shader.id) {
        return true;
    }
    char *vertex = render_shader_source("scene3d_shadow.vert");
    char *fragment = render_shader_source("scene3d_shadow.frag");
    if (vertex && fragment) {
        shadow->shader = cf_make_shader_from_source(vertex, fragment);
    }
    if (!shadow->shader.id) {
        snprintf(
            error,
            size,
            "Shadow shader: %s",
            vertex && fragment ? cf_shader_compile_error() : "cannot read depth shader sources"
        );
    }
    free(vertex);
    free(fragment);
    return shadow->shader.id;
}

static float view_depth(const render_scene3d_camera *camera, CF_V3 world)
{
    return -cf_m4_transform_point(camera->view, world).z;
}

/* Light-space XY bounds of the camera frustum between two view depths; empty
 * when that range misses the frustum. */
static CF_Aabb frustum_slab(
    const render_scene3d_shadow *shadow,
    const render_scene3d_camera *camera,
    float near,
    float far
)
{
    CF_M4x4 inverse = cf_m4_invert(cf_mul_m4(camera->projection, camera->view));
    CF_Aabb bounds = { { FLT_MAX, FLT_MAX }, { -FLT_MAX, -FLT_MAX } };
    for (int i = 0; i < 4; i++) {
        CF_V3 ndc = { i & 1 ? 1.0f : -1.0f, i & 2 ? 1.0f : -1.0f, 0 };
        CF_V3 a = cf_m4_transform_point(inverse, ndc);
        ndc.z = 1;
        CF_V3 b = cf_m4_transform_point(inverse, ndc);
        float da = view_depth(camera, a), db = view_depth(camera, b);
        float depths[] = { fmaxf(near, da), fminf(far, db) };
        if (depths[0] > depths[1]) {
            return bounds;
        }
        for (int j = 0; j < 2; j++) {
            CF_V3 p = cf_lerp_v3(a, b, (depths[j] - da) / (db - da));
            CF_V3 light = cf_m4_transform_point(shadow->view, p);
            bounds.min = cf_min_v2(bounds.min, (CF_V2){ light.x, light.y });
            bounds.max = cf_max_v2(bounds.max, (CF_V2){ light.x, light.y });
        }
    }
    return bounds;
}

bool render_scene3d_shadow_fit(
    render_scene3d_shadow *shadow,
    CF_V3 toward_light,
    const CF_M4x4 *boxes,
    int count,
    const render_scene3d_camera *camera
)
{
    CF_V3 up = fabsf(toward_light.y) > .99f ? (CF_V3){ 0, 0, 1 } : (CF_V3){ 0, 1, 0 };
    shadow->view = cf_look_at((CF_V3){ 0 }, cf_neg_v3(toward_light), up);
    CF_V3 low = { FLT_MAX, FLT_MAX, FLT_MAX }, high = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    float near = FLT_MAX, far = -FLT_MAX;
    for (int i = 0; i < count; i++) {
        for (int corner = 0; corner < 8; corner++) {
            CF_V3 unit = { corner & 1 ? .5f : -.5f,
                           corner & 2 ? .5f : -.5f,
                           corner & 4 ? .5f : -.5f };
            CF_V3 world = cf_m4_transform_point(boxes[i], unit);
            CF_V3 light = cf_m4_transform_point(shadow->view, world);
            low = cf_min_v3(low, light);
            high = cf_max_v3(high, light);
            near = fminf(near, view_depth(camera, world));
            far = fmaxf(far, view_depth(camera, world));
        }
    }
    /* Only boxes within the camera's depth range can show a shadow. */
    CF_Aabb slab = frustum_slab(shadow, camera, near, far);
    CF_V2 min = cf_max_v2(slab.min, (CF_V2){ low.x, low.y });
    CF_V2 max = cf_min_v2(slab.max, (CF_V2){ high.x, high.y });
    float size = fmaxf(max.x - min.x, max.y - min.y);
    if (!(size > 0) || min.x > max.x || min.y > max.y) {
        return false;
    }
    /* Quarter-octave sizes and a texel-aligned origin keep shadow edges still
     * while the camera moves. */
    size = exp2f(ceilf(log2f(size) * 4) / 4);
    shadow->texel = size / SHADOW_SIZE;
    min.x = floorf(min.x / shadow->texel) * shadow->texel;
    min.y = floorf(min.y / shadow->texel) * shadow->texel;
    float pad = shadow->texel * 4;
    shadow->projection = cf_ortho(min.x, min.x + size, min.y, min.y + size, -high.z - pad, -low.z + pad);
    return true;
}

bool render_scene3d_shadow_begin(render_scene3d_shadow *shadow, char *error, size_t size)
{
    if (!load_shader(shadow, error, size)) {
        return false;
    }
    prepare_map(shadow, SHADOW_SIZE);
    CF_RenderState state = cf_render_state_3d_defaults();
    /* Open meshes such as the flat triangle cast from either side. */
    state.cull_mode = CF_CULL_MODE_NONE;
    cf_draw3d_push_render_state(state);
    cf_draw3d_push_shader(shadow->shader);
    cf_draw3d_push_projection(shadow->projection);
    cf_draw3d_push_view(shadow->view);
    return true;
}

void render_scene3d_shadow_end(render_scene3d_shadow *shadow)
{
    cf_draw3d_pop_view();
    cf_draw3d_pop_projection();
    cf_draw3d_pop_shader();
    cf_draw3d_pop_render_state();
    cf_render_to(shadow->canvas, true);
}

void render_scene3d_shadow_bind(render_scene3d_shadow *shadow, bool enabled)
{
    prepare_map(shadow, 1);
    CF_M4x4 matrix = cf_mul_m4(shadow->projection, shadow->view);
    /* Enabled, map texel in UV, and map texel in world units for the normal offset. */
    CF_V4 settings = { enabled ? 1.0f : 0.0f, 1.0f / (float)shadow->size, shadow->texel, 0 };
    cf_draw3d_set_uniform_m4("scene_shadow_matrix", matrix);
    cf_draw3d_set_uniform("scene_shadow", &settings, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_texture("scene_shadow_map", shadow->map);
}

void render_scene3d_shadow_unbind()
{
    cf_draw3d_set_texture("scene_shadow_map", (CF_Texture){ 0 });
}
