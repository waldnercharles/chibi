#include "render/scene3d_shadow.h"

#include "platform/cute.h"
#include "render/shader_program.h"
#include "support/xalloc.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    SHADOW_SIZE = 2048,
    /* A lookup's reach past its surface point: a 2.5-texel normal offset
     * plus its 1.5-texel filter. */
    MARGIN_TEXELS = 4,
    /* Keeps the depth pass apart from anything else queued for drawing. */
    SHADOW_LAYER = INT_MIN / 2
};

/* Built-in meshes reach 2/3 of a unit from their center: the triangle's tip. */
static const float mesh_extent = 2.0f / 3;

struct render_scene3d_shadow
{
    CF_Texture map;
    CF_Canvas canvas;
    CF_Shader shader;
    int size;
    CF_M4x4 view, projection;
    CF_Aabb area; /* Light-space XY the map covers. */
    float texel;  /* World units per map texel. */
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
    destroy_map(shadow);
    if (shadow->shader.id) {
        cf_destroy_shader(shadow->shader);
    }
    free(shadow);
}

/* A depth map of at least `size` texels per side, cleared to the far plane. */
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
    /* The placeholder map is only ever bound, never drawn into. */
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

static CF_V3 box_corner(CF_M4x4 box, int corner)
{
    CF_V3 unit = { corner & 1 ? mesh_extent : -mesh_extent,
                   corner & 2 ? mesh_extent : -mesh_extent,
                   corner & 4 ? mesh_extent : -mesh_extent };
    return cf_m4_transform_point(box, unit);
}

static void light_bounds(const render_scene3d_shadow *shadow, CF_M4x4 box, CF_V3 *low, CF_V3 *high)
{
    CF_M4x4 matrix = cf_mul_m4(shadow->view, box);
    *low = (CF_V3){ FLT_MAX, FLT_MAX, FLT_MAX };
    *high = (CF_V3){ -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (int corner = 0; corner < 8; corner++) {
        CF_V3 p = box_corner(matrix, corner);
        *low = cf_min_v3(*low, p);
        *high = cf_max_v3(*high, p);
    }
}

static bool covers(const render_scene3d_shadow *shadow, CF_V3 low, CF_V3 high)
{
    return low.x <= shadow->area.max.x && high.x >= shadow->area.min.x &&
           low.y <= shadow->area.max.y && high.y >= shadow->area.min.y;
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

/* The light-space XY of the receivers the camera can see, with a margin for
 * the lookups. Quarter-octave sizes and a texel-aligned origin keep shadow
 * edges still while the camera moves within one size. */
static bool fit_area(
    render_scene3d_shadow *shadow,
    const CF_M4x4 *boxes,
    int receivers,
    const render_scene3d_camera *camera
)
{
    CF_V2 low = { FLT_MAX, FLT_MAX }, high = { -FLT_MAX, -FLT_MAX };
    float near = FLT_MAX, far = -FLT_MAX;
    for (int i = 0; i < receivers; i++) {
        for (int corner = 0; corner < 8; corner++) {
            CF_V3 world = box_corner(boxes[i], corner);
            CF_V3 light = cf_m4_transform_point(shadow->view, world);
            low = cf_min_v2(low, (CF_V2){ light.x, light.y });
            high = cf_max_v2(high, (CF_V2){ light.x, light.y });
            near = fminf(near, view_depth(camera, world));
            far = fmaxf(far, view_depth(camera, world));
        }
    }
    CF_Aabb slab = frustum_slab(shadow, camera, near, far);
    CF_V2 min = cf_max_v2(slab.min, low), max = cf_min_v2(slab.max, high);
    float size = fmaxf(max.x - min.x, max.y - min.y);
    if (!(size > 0) || min.x > max.x || min.y > max.y) {
        return false;
    }
    float margin = size * MARGIN_TEXELS / SHADOW_SIZE;
    size = exp2f(ceilf(log2f(size + 2 * margin) * 4) / 4);
    shadow->texel = size / SHADOW_SIZE;
    min.x = floorf((min.x - margin) / shadow->texel) * shadow->texel;
    min.y = floorf((min.y - margin) / shadow->texel) * shadow->texel;
    shadow->area = (CF_Aabb){ min, { min.x + size, min.y + size } };
    return true;
}

bool render_scene3d_shadow_fit(
    render_scene3d_shadow *shadow,
    CF_V3 toward_light,
    const CF_M4x4 *boxes,
    int count,
    int receivers,
    const render_scene3d_camera *camera
)
{
    CF_V3 up = fabsf(toward_light.y) > .99f ? (CF_V3){ 0, 0, 1 } : (CF_V3){ 0, 1, 0 };
    shadow->view = cf_look_at((CF_V3){ 0 }, cf_neg_v3(toward_light), up);
    if (!fit_area(shadow, boxes, receivers, camera)) {
        return false;
    }
    /* Depth spans every box over the area, so casters outside the view count. */
    float low = FLT_MAX, high = -FLT_MAX;
    for (int i = 0; i < count; i++) {
        CF_V3 a, b;
        light_bounds(shadow, boxes[i], &a, &b);
        if (covers(shadow, a, b)) {
            low = fminf(low, a.z);
            high = fmaxf(high, b.z);
        }
    }
    if (low > high) {
        return false;
    }
    float pad = shadow->texel * MARGIN_TEXELS;
    CF_Aabb area = shadow->area;
    shadow->projection = cf_ortho(area.min.x, area.max.x, area.min.y, area.max.y, -high - pad, -low + pad);
    return true;
}

bool render_scene3d_shadow_reaches(const render_scene3d_shadow *shadow, CF_M4x4 box)
{
    CF_V3 low, high;
    light_bounds(shadow, box, &low, &high);
    return covers(shadow, low, high);
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
    cf_draw_push_layer(SHADOW_LAYER);
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
    cf_draw_pop_layer();
    cf_render_layers_to(shadow->canvas, SHADOW_LAYER, SHADOW_LAYER, true);
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
