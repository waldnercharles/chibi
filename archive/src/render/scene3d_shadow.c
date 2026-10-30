#include "render/scene3d_shadow.h"

#include "platform/cute.h"
#include "render/fullscreen.h"
#include "render/shader_program.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    /* Matches the arrays in scene3d_scene.glsl. */
    CASCADE_LIMIT = 4,
    /* Texels per side of one cascade's square in the atlas. */
    TILE = 2048,
    /* A lookup's reach past its surface point: a 2.5-texel normal offset
     * plus its 1.5-texel filter. */
    MARGIN_TEXELS = 4,
    /* The widest soft filter's radius; matches SOFT_TEXELS in scene3d.frag. */
    SOFT_TEXELS = 128,
    /* Keeps the depth pass apart from anything else queued for drawing. */
    SHADOW_LAYER = INT_MIN / 2,
    /* Clipping every edge of a box and a frustum against the other. */
    OVERLAP_POINTS = 48
};

/* Cascades past the first span at most this ratio of view depths each. */
static const float cascade_ratio = 4;

/* A convex solid: corners indexed by one bit per axis, so corners one bit
 * apart share an edge, and six planes facing inward. */
typedef struct
{
    CF_V3 corners[8];
    CF_Plane3 planes[6];
} hull;

/* What a receiver shows of itself: its view depths and their light-space XY. */
typedef struct
{
    float near, far;
    CF_Aabb light;
} shown;

/* One orthographic light view, covering one range of view depths. */
typedef struct
{
    CF_M4x4 projection;
    CF_Aabb area; /* Light-space XY the cascade covers. */
    float texel;  /* World units per map texel. */
    float range;  /* World units from its near depth to its far depth. */
} cascade;

/* The nearest depth in each 2x2 block of the map, read raw by the lit pass's
 * blocker search, which the map's comparison sampler can't serve. */
typedef struct
{
    CF_Canvas canvas;
    int width, height;
    CF_Shader shader;
    CF_Material material;
    CF_Sampler raw;
    CF_Mesh triangle;
} blocker_map;

struct render_scene3d_shadow
{
    render_scene3d_depth_map atlas;
    CF_Shader shader;
    blocker_map blockers;
    float spread;
    float reach; /* How far the widest penumbra the scene allows reaches. */
    CF_M4x4 view;
    cascade cascades[CASCADE_LIMIT];
    int count;
    CF_Aabb3 *extents; /* Every fitted box in light space. */
    shown *shown;      /* Each receiver; empty depths when it shows nothing. */
};

render_scene3d_shadow *render_scene3d_shadow_new()
{
    return xcalloc(1, sizeof(render_scene3d_shadow));
}

void render_scene3d_depth_map_free(render_scene3d_depth_map *depth)
{
    if (!depth->width) {
        return;
    }
    cf_destroy_canvas(depth->canvas);
    cf_destroy_texture(depth->map);
}

static void free_blockers(blocker_map *blockers)
{
    if (blockers->width) {
        cf_destroy_canvas(blockers->canvas);
    }
    if (!blockers->shader.id) {
        return;
    }
    cf_destroy_shader(blockers->shader);
    cf_destroy_material(blockers->material);
    cf_destroy_sampler(blockers->raw);
    cf_destroy_mesh(blockers->triangle);
}

void render_scene3d_shadow_free(render_scene3d_shadow *shadow)
{
    render_scene3d_depth_map_free(&shadow->atlas);
    free_blockers(&shadow->blockers);
    if (shadow->shader.id) {
        cf_destroy_shader(shadow->shader);
    }
    afree(shadow->extents);
    afree(shadow->shown);
    free(shadow);
}

void render_scene3d_depth_map_prepare(render_scene3d_depth_map *depth, int width, int height)
{
    if (depth->width >= width && depth->height >= height) {
        return;
    }
    render_scene3d_depth_map_free(depth);
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    CF_TextureParams map = params.depth_stencil_target;
    map.usage = CF_TEXTURE_USAGE_DEPTH_STENCIL_TARGET_BIT | CF_TEXTURE_USAGE_SAMPLER_BIT;
    /* Depth alone, so no texel pays for a stencil it never uses. Every
     * device can render the last. */
    static const CF_PixelFormat formats[] = { CF_PIXEL_FORMAT_D32_FLOAT,
                                              CF_PIXEL_FORMAT_D24_UNORM,
                                              CF_PIXEL_FORMAT_D16_UNORM };
    int format = 0;
    while (format < 2 && !cf_texture_supports_format(formats[format], map.usage)) {
        format++;
    }
    map.pixel_format = formats[format];
    map.width = depth->width = width > depth->width ? width : depth->width;
    map.height = depth->height = height > depth->height ? height : depth->height;
    map.wrap_u = map.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    map.compare_enable = true;
    map.compare_function = CF_COMPARE_FUNCTION_LESS_THAN_OR_EQUAL;
    depth->map = cf_make_texture(map);
    params.attach_target = depth->map;
    depth->canvas = cf_make_canvas(params);
    cf_canvas_set_clear_depth_stencil(depth->canvas, 1, 0);
    /* The placeholder map is only ever bound, never drawn into. */
    cf_clear_canvas(depth->canvas);
}

/* A blocker map of at least the given size. */
static void prepare_blockers(blocker_map *blockers, int width, int height)
{
    if (blockers->width >= width && blockers->height >= height) {
        return;
    }
    if (blockers->width) {
        cf_destroy_canvas(blockers->canvas);
    }
    blockers->width = width > blockers->width ? width : blockers->width;
    blockers->height = height > blockers->height ? height : blockers->height;
    CF_CanvasParams params = cf_canvas_defaults(blockers->width, blockers->height);
    /* Depths for the blocker search only size penumbrae, so half floats do,
     * and every device the HDR pass runs on can render them. */
    params.target.pixel_format = CF_PIXEL_FORMAT_R16_FLOAT;
    params.target.filter = CF_FILTER_NEAREST;
    blockers->canvas = cf_make_canvas(params);
}

static bool load_blocker_pass(blocker_map *blockers, char *error, size_t size)
{
    if (blockers->shader.id) {
        return true;
    }
    char *vertex = render_shader_source("hrc/pass.vert");
    char *fragment = render_shader_source("scene3d_shadow_blockers.frag");
    bool read = vertex && fragment;
    if (read) {
        blockers->shader = cf_make_shader_from_source(vertex, fragment);
    }
    free(vertex);
    free(fragment);
    if (!blockers->shader.id) {
        snprintf(error, size, "Shadow blocker shader: %s", read ? cf_shader_compile_error() : "cannot read blocker shader sources");
        return false;
    }
    blockers->material = cf_make_material();
    CF_SamplerParams raw = cf_sampler_defaults();
    raw.filter = CF_FILTER_NEAREST;
    raw.mip_filter = CF_MIP_FILTER_NEAREST;
    raw.wrap_u = raw.wrap_v = raw.wrap_w = CF_WRAP_MODE_CLAMP_TO_EDGE;
    blockers->raw = cf_make_sampler(raw);
    blockers->triangle = render_make_pass_mesh();
    return true;
}

/* Writes the nearest of each 2x2 block of the map's depths. */
static void reduce_blockers(render_scene3d_shadow *shadow)
{
    blocker_map *blockers = &shadow->blockers;
    int width = shadow->atlas.width / 2, height = shadow->atlas.height / 2;
    prepare_blockers(blockers, width, height);
    cf_material_set_texture_fs_sampler(
        blockers->material,
        "map",
        shadow->atlas.map,
        blockers->raw
    );
    /* Every texel is rewritten, so nothing need load. */
    cf_apply_canvas(blockers->canvas, true);
    cf_apply_mesh(blockers->triangle);
    cf_apply_shader(blockers->shader, blockers->material);
    cf_apply_viewport(0, 0, width, height);
    cf_apply_scissor(0, 0, width, height);
    cf_draw_elements();
}

bool render_scene3d_depth_shader(CF_Shader *shader, char *error, size_t size)
{
    if (shader->id) {
        return true;
    }
    char *vertex = render_shader_source("scene3d_shadow.vert");
    char *fragment = render_shader_source("scene3d_shadow.frag");
    if (vertex && fragment) {
        *shader = cf_make_shader_from_source(vertex, fragment);
    }
    if (!shader->id) {
        snprintf(
            error,
            size,
            "Shadow shader: %s",
            vertex && fragment ? cf_shader_compile_error() : "cannot read depth shader sources"
        );
    }
    free(vertex);
    free(fragment);
    return shader->id;
}

static CF_V3 box_corner(CF_M4x4 box, int corner)
{
    CF_V3 unit = { corner & 1 ? .5f : -.5f,
                   corner & 2 ? .5f : -.5f,
                   corner & 4 ? .5f : -.5f };
    return cf_m4_transform_point(box, unit);
}

static bool covers(const cascade *cascade, const CF_Aabb3 *box)
{
    return box->min.x <= cascade->area.max.x && box->max.x >= cascade->area.min.x &&
           box->min.y <= cascade->area.max.y && box->max.y >= cascade->area.min.y;
}

static CF_V3 box_center(CF_M4x4 box)
{
    return (CF_V3){ box.elements[12], box.elements[13], box.elements[14] };
}

/* How far a box reaches from its center along a direction. */
static float reach(CF_M4x4 box, CF_V3 direction)
{
    float sum = 0;
    for (int axis = 0; axis < 3; axis++) {
        const float *m = box.elements + 4 * axis;
        sum += fabsf(m[0] * direction.x + m[1] * direction.y + m[2] * direction.z);
    }
    return sum / 2;
}

static hull box_hull(CF_M4x4 box)
{
    hull solid;
    for (int corner = 0; corner < 8; corner++) {
        solid.corners[corner] = box_corner(box, corner);
    }
    const float *m = box.elements;
    CF_V3 axes[] = { { m[0], m[1], m[2] }, { m[4], m[5], m[6] }, { m[8], m[9], m[10] } };
    for (int k = 0; k < 3; k++) {
        CF_V3 normal = cf_cross_v3(axes[(k + 1) % 3], axes[(k + 2) % 3]);
        float middle = cf_dot_v3(normal, box_center(box)), half = reach(box, normal);
        solid.planes[2 * k] = (CF_Plane3){ normal, middle - half };
        solid.planes[2 * k + 1] = (CF_Plane3){ cf_neg_v3(normal), -(middle + half) };
    }
    return solid;
}

static bool inside(CF_M4x4 box, const CF_Plane3 *planes)
{
    for (int i = 0; i < 6; i++) {
        if (cf_dot_v3(planes[i].n, box_center(box)) - planes[i].d <
            reach(box, planes[i].n)) {
            return false;
        }
    }
    return true;
}

static float view_depth(const render_scene3d_camera *camera, CF_V3 world)
{
    const float *view = camera->view.elements;
    return -(view[2] * world.x + view[6] * world.y + view[10] * world.z + view[14]);
}

/* The camera frustum between two view depths, within its own near and far. */
static hull frustum_between(const render_scene3d_camera *camera, float near, float far)
{
    CF_M4x4 view_projection = cf_mul_m4(camera->projection, camera->view);
    CF_M4x4 inverse = cf_m4_invert(view_projection);
    CF_Frustum sides = cf_frustum_from_m4(view_projection);
    /* Each corner ray, from the camera's near plane to its far plane. */
    CF_V3 rays[4][2];
    for (int i = 0; i < 4; i++) {
        for (int end = 0; end < 2; end++) {
            CF_V3 ndc = { i & 1 ? 1.0f : -1.0f, i & 2 ? 1.0f : -1.0f, (float)end };
            rays[i][end] = cf_m4_transform_point(inverse, ndc);
        }
    }
    float da = view_depth(camera, rays[0][0]), db = view_depth(camera, rays[0][1]);
    near = fmaxf(near, da);
    far = fminf(far, db);
    hull frustum;
    for (int i = 0; i < 4; i++) {
        frustum.corners[i] = cf_lerp_v3(rays[i][0], rays[i][1], (near - da) / (db - da));
        frustum.corners[i + 4] = cf_lerp_v3(rays[i][0], rays[i][1], (far - da) / (db - da));
        frustum.planes[i] = sides.planes[i];
    }
    /* View depth is dot(forward, p) - view[14]. */
    const float *view = camera->view.elements;
    CF_V3 forward = { -view[2], -view[6], -view[10] };
    frustum.planes[4] = (CF_Plane3){ forward, near + view[14] };
    frustum.planes[5] = (CF_Plane3){ cf_neg_v3(forward), -far - view[14] };
    return frustum;
}

/* Clips the segment a..b to the planes' inside as a range of t; false when
 * nothing is left. */
static bool clip(CF_V3 a, CF_V3 b, const CF_Plane3 *planes, float t[2])
{
    t[0] = 0;
    t[1] = 1;
    for (int i = 0; i < 6; i++) {
        float da = cf_dot_v3(planes[i].n, a) - planes[i].d;
        float db = cf_dot_v3(planes[i].n, b) - planes[i].d;
        if (da < 0 && db < 0) {
            return false;
        }
        if (da < 0) {
            t[0] = fmaxf(t[0], da / (da - db));
        }
        if (db < 0) {
            t[1] = fminf(t[1], da / (da - db));
        }
    }
    return t[0] <= t[1];
}

static int clip_edges(const hull *edges, const hull *bounds, CF_V3 *points, int count)
{
    for (int i = 0; i < 8; i++) {
        for (int bit = 1; bit < 8; bit <<= 1) {
            if (i & bit) {
                continue;
            }
            CF_V3 a = edges->corners[i], b = edges->corners[i | bit];
            float t[2];
            if (clip(a, b, bounds->planes, t)) {
                points[count++] = cf_lerp_v3(a, b, t[0]);
                points[count++] = cf_lerp_v3(a, b, t[1]);
            }
        }
    }
    return count;
}

/* Points in where a box crossing a frustum's planes overlaps it, including
 * every corner of the overlap, so they bound it along any direction. */
static int overlap(CF_M4x4 box, const hull *frustum, CF_V3 points[OVERLAP_POINTS])
{
    hull solid = box_hull(box);
    return clip_edges(frustum, &solid, points, clip_edges(&solid, frustum, points, 0));
}

static void grow(CF_Aabb *bounds, CF_V2 low, CF_V2 high)
{
    bounds->min = cf_min_v2(bounds->min, low);
    bounds->max = cf_max_v2(bounds->max, high);
}

/* What box `i` shows of itself within a frustum. */
static shown show(
    const render_scene3d_shadow *shadow,
    CF_M4x4 box,
    int i,
    const hull *frustum,
    const render_scene3d_camera *camera
)
{
    if (inside(box, frustum->planes)) {
        float middle = view_depth(camera, box_center(box));
        float half = reach(box, frustum->planes[4].n);
        const CF_Aabb3 *light = &shadow->extents[i];
        return (shown){ middle - half,
                        middle + half,
                        { { light->min.x, light->min.y },
                          { light->max.x, light->max.y } } };
    }
    CF_V3 points[OVERLAP_POINTS];
    int count = overlap(box, frustum, points);
    shown part = { FLT_MAX, -FLT_MAX, { { FLT_MAX, FLT_MAX }, { -FLT_MAX, -FLT_MAX } } };
    for (int j = 0; j < count; j++) {
        CF_V3 light = cf_m4_transform_point(shadow->view, points[j]);
        part.near = fminf(part.near, view_depth(camera, points[j]));
        part.far = fmaxf(part.far, view_depth(camera, points[j]));
        grow(&part.light, (CF_V2){ light.x, light.y }, (CF_V2){ light.x, light.y });
    }
    return part;
}

/* Covers what the receivers show between two view depths with a margin for
 * the lookups, widened for a soft sun by its widest penumbra up to the soft
 * filter's limit. Quarter-octave sizes and a texel-aligned origin keep shadow
 * edges still while the camera moves within one size. */
static void fit_cascade(
    render_scene3d_shadow *shadow,
    const CF_M4x4 *boxes,
    int receivers,
    const render_scene3d_camera *camera,
    float near,
    float far
)
{
    hull slab = frustum_between(camera, near, far);
    CF_Aabb area = { { FLT_MAX, FLT_MAX }, { -FLT_MAX, -FLT_MAX } };
    for (int i = 0; i < receivers; i++) {
        const shown *part = &shadow->shown[i];
        if (part->far < near || part->near > far) {
            continue;
        }
        /* Only a receiver straddling the range shows less of itself here. */
        CF_Aabb light = part->near < near || part->far > far
                          ? show(shadow, boxes[i], i, &slab, camera).light
                          : part->light;
        grow(&area, light.min, light.max);
    }
    CF_V2 min = area.min, max = area.max;
    float size = fmaxf(max.x - min.x, max.y - min.y);
    if (!(size > 0)) {
        return;
    }
    cascade *cascade = &shadow->cascades[shadow->count++];
    /* Room for the margins on both sides and one texel lost to snapping. */
    float reached = (size + 2 * shadow->reach) * TILE / (TILE - 2 * MARGIN_TEXELS - 1);
    float limited = size * TILE / (TILE - 2 * (MARGIN_TEXELS + SOFT_TEXELS) - 1);
    size = exp2f(ceilf(log2f(fminf(reached, limited)) * 4) / 4);
    cascade->texel = size / TILE;
    float margin = MARGIN_TEXELS +
                   fminf(floorf(shadow->reach / cascade->texel), SOFT_TEXELS);
    min.x = (floorf(min.x / cascade->texel) - margin) * cascade->texel;
    min.y = (floorf(min.y / cascade->texel) - margin) * cascade->texel;
    cascade->area = (CF_Aabb){ min, { min.x + size, min.y + size } };
}

/* Depth runs from the box over the area nearest the sun, which may be outside
 * the view, to the receiver farthest from it. */
static void fit_depth(render_scene3d_shadow *shadow, cascade *cascade, int receivers)
{
    float low = FLT_MAX, high = -FLT_MAX;
    for (int i = 0; i < acount(shadow->extents); i++) {
        const CF_Aabb3 *box = &shadow->extents[i];
        if (covers(cascade, box)) {
            low = i < receivers ? fminf(low, box->min.z) : low;
            high = fmaxf(high, box->max.z);
        }
    }
    /* The receivers that set the area cover it. */
    assert(low <= high);
    float pad = cascade->texel * MARGIN_TEXELS;
    CF_Aabb area = cascade->area;
    cascade->projection = cf_ortho(area.min.x, area.max.x, area.min.y, area.max.y, -high - pad, -low + pad);
    cascade->range = high - low + 2 * pad;
}

int render_scene3d_shadow_fit(
    render_scene3d_shadow *shadow,
    CF_V3 toward_light,
    float spread,
    const CF_M4x4 *boxes,
    int count,
    int receivers,
    const render_scene3d_camera *camera
)
{
    shadow->spread = spread;
    CF_V3 up = fabsf(toward_light.y) > .99f ? (CF_V3){ 0, 0, 1 } : (CF_V3){ 0, 1, 0 };
    shadow->view = cf_look_at((CF_V3){ 0 }, cf_neg_v3(toward_light), up);
    shadow->count = 0;
    asetlen(shadow->extents, count);
    CF_Aabb3 unit = { { -.5f, -.5f, -.5f }, { .5f, .5f, .5f } };
    float low = FLT_MAX, high = -FLT_MAX;
    for (int i = 0; i < count; i++) {
        shadow->extents[i] = cf_transform_aabb3(cf_mul_m4(shadow->view, boxes[i]), unit);
        low = fminf(low, shadow->extents[i].min.z);
        high = fmaxf(high, shadow->extents[i].max.z);
    }
    /* No blocker lies farther from its receiver than the scene is deep. */
    shadow->reach = spread * (high - low);
    hull frustum = frustum_between(camera, 0, FLT_MAX);
    float near = FLT_MAX, far = -FLT_MAX;
    asetlen(shadow->shown, receivers);
    for (int i = 0; i < receivers; i++) {
        shadow->shown[i] = show(shadow, boxes[i], i, &frustum, camera);
        near = fminf(near, shadow->shown[i].near);
        far = fmaxf(far, shadow->shown[i].far);
    }
    if (near > far) {
        return 0;
    }
    /* Shown points lie inside the frustum, beyond the camera's near plane. */
    assert(near > 0);
    /* Equal ratios of depth per cascade, anchored at the far end; surfaces
     * nearer than the cascades reach share the first. */
    int splits = (int)ceilf(log2f(far / near) / log2f(cascade_ratio));
    splits = splits < 1 ? 1 : splits > CASCADE_LIMIT ? CASCADE_LIMIT : splits;
    float step = fminf(powf(far / near, 1.0f / splits), cascade_ratio);
    float from = 0, to = far / powf(step, (float)(splits - 1));
    for (int i = 0; i < splits; i++) {
        fit_cascade(shadow, boxes, receivers, camera, from, i + 1 < splits ? to : FLT_MAX);
        from = to;
        to *= step;
    }
    for (int i = 0; i < shadow->count; i++) {
        fit_depth(shadow, &shadow->cascades[i], receivers);
    }
    return shadow->count;
}

bool render_scene3d_shadow_reaches(const render_scene3d_shadow *shadow, int cascade, int box)
{
    assert(cascade < shadow->count && box < acount(shadow->extents));
    return covers(&shadow->cascades[cascade], &shadow->extents[box]);
}

bool render_scene3d_shadow_begin(render_scene3d_shadow *shadow, int cascade, char *error, size_t size)
{
    assert(cascade < shadow->count);
    if (!render_scene3d_depth_shader(&shadow->shader, error, size) ||
        (shadow->spread > 0 && !load_blocker_pass(&shadow->blockers, error, size))) {
        return false;
    }
    /* Cascades fill a 2x2 grid of squares row by row. */
    render_scene3d_depth_map_prepare(
        &shadow->atlas,
        shadow->count > 1 ? 2 * TILE : TILE,
        shadow->count > 2 ? 2 * TILE : TILE
    );
    CF_RenderState state = cf_render_state_3d_defaults();
    /* Open meshes such as the flat triangle cast from either side. */
    state.cull_mode = CF_CULL_MODE_NONE;
    cf_draw_push_layer(SHADOW_LAYER);
    cf_draw_push_viewport(
        (CF_Rect){ (cascade & 1) * TILE, (cascade >> 1) * TILE, TILE, TILE }
    );
    cf_draw3d_push_render_state(state);
    cf_draw3d_push_shader(shadow->shader);
    cf_draw3d_push_projection(shadow->cascades[cascade].projection);
    cf_draw3d_push_view(shadow->view);
    return true;
}

void render_scene3d_shadow_end(render_scene3d_shadow *shadow, int cascade)
{
    cf_draw3d_pop_view();
    cf_draw3d_pop_projection();
    cf_draw3d_pop_shader();
    cf_draw3d_pop_render_state();
    cf_draw_pop_viewport();
    cf_draw_pop_layer();
    if (cascade < shadow->count - 1) {
        return;
    }
    cf_render_layers_to(shadow->atlas.canvas, SHADOW_LAYER, SHADOW_LAYER, true);
    if (shadow->spread > 0) {
        reduce_blockers(shadow);
    }
}

void render_scene3d_shadow_bind(render_scene3d_shadow *shadow, int count)
{
    assert(count <= shadow->count);
    render_scene3d_depth_map_prepare(&shadow->atlas, 1, 1);
    prepare_blockers(&shadow->blockers, 1, 1);
    CF_M4x4 matrices[CASCADE_LIMIT] = { 0 };
    float texels[CASCADE_LIMIT] = { 0 }, ranges[CASCADE_LIMIT] = { 0 };
    for (int i = 0; i < count; i++) {
        matrices[i] = cf_mul_m4(shadow->cascades[i].projection, shadow->view);
        texels[i] = shadow->cascades[i].texel;
        ranges[i] = shadow->cascades[i].range;
    }
    CF_V4 soft = { .x = shadow->spread };
    /* Cascades, a square's texel in its own UV, and a square's size in atlas UV. */
    CF_V4 settings = { (float)count,
                       1.0f / TILE,
                       (float)TILE / (float)shadow->atlas.width,
                       (float)TILE / (float)shadow->atlas.height };
    cf_draw3d_set_uniform("scene_shadow_matrices", matrices, CF_UNIFORM_TYPE_MAT4, CASCADE_LIMIT);
    cf_draw3d_set_uniform("scene_shadow_texels", texels, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_shadow", &settings, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_shadow_ranges", ranges, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_shadow_soft", &soft, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_texture("scene_shadow_map", shadow->atlas.map);
    cf_draw3d_set_texture(
        "scene_shadow_blockers",
        cf_canvas_get_target(shadow->blockers.canvas)
    );
}

void render_scene3d_shadow_unbind()
{
    cf_draw3d_set_texture("scene_shadow_map", (CF_Texture){ 0 });
    cf_draw3d_set_texture("scene_shadow_blockers", (CF_Texture){ 0 });
}

render_scene3d_shadow_snapshot render_scene3d_shadow_read(render_scene3d_shadow *shadow, int count)
{
    assert(count >= 0 && count <= shadow->count && CASCADE_LIMIT == 4);
    render_scene3d_depth_map_prepare(&shadow->atlas, 1, 1);
    render_scene3d_shadow_snapshot result = {
        .map = shadow->atlas.map,
        .settings = { (float)count,
                      1.0f / TILE,
                      (float)TILE / shadow->atlas.width,
                      (float)TILE / shadow->atlas.height },
    };
    for (int i = 0; i < count; i++) {
        result.matrices[i] = cf_mul_m4(shadow->cascades[i].projection, shadow->view);
    }
    return result;
}
