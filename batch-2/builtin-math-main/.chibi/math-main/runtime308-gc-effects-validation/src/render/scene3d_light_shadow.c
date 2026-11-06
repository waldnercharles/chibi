#include "render/scene3d_light_shadow.h"

#include "render/scene3d_shadow.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum
{
    /* Texels per side of one face in the atlas. */
    FACE = 512,
    /* Texels kept clear around the angle each face holds, as far as the lit
     * pass's 3x3 lookups reach past their point. */
    GUARD = 3,
    /* Keeps the depth pass apart from anything else queued for drawing,
     * including the sun's. */
    LIGHT_SHADOW_LAYER = INT_MIN / 2 + 1
};

/* Each face's near plane as a share of its light's radius; solids nearer the
 * light cast nothing. */
static const float near_share = .01f;

/* A point light's faces each look along one axis direction, with up along +y,
 * or along +z for the faces along y. scene3d.frag picks faces the same way. */
static const CF_V3 forwards[RENDER_SCENE3D_LIGHT_SHADOW_FACES] = {
    { 1, 0, 0 },  { -1, 0, 0 }, { 0, 1, 0 },
    { 0, -1, 0 }, { 0, 0, 1 },  { 0, 0, -1 },
};

struct render_scene3d_light_shadow
{
    render_scene3d_depth_map atlas;
    CF_Shader shader;
    CF_V4 lights[RENDER_SCENE3D_LIGHT_SHADOW_LIMIT];
    CF_V4 cones[RENDER_SCENE3D_LIGHT_SHADOW_LIMIT];
    int count;
};

/* Where one face looks from its light, and the tangent of half its field of
 * view. */
typedef struct
{
    CF_V3 forward, up;
    float spread;
} face_view;

render_scene3d_light_shadow *render_scene3d_light_shadow_new()
{
    return xcalloc(1, sizeof(render_scene3d_light_shadow));
}

void render_scene3d_light_shadow_free(render_scene3d_light_shadow *shadow)
{
    render_scene3d_depth_map_free(&shadow->atlas);
    if (shadow->shader.id) {
        cf_destroy_shader(shadow->shader);
    }
    free(shadow);
}

void render_scene3d_light_shadow_set(
    render_scene3d_light_shadow *shadow,
    const CF_V4 *lights,
    const CF_V4 *cones,
    int count
)
{
    assert(count <= RENDER_SCENE3D_LIGHT_SHADOW_LIMIT);
    memcpy(shadow->lights, lights, (size_t)count * sizeof *lights);
    memcpy(shadow->cones, cones, (size_t)count * sizeof *cones);
    shadow->count = count;
}

static CF_V3 cone_axis(CF_V4 cone)
{
    return (CF_V3){ cone.x, cone.y, cone.z };
}

/* Whether a light is a spot light, whose cone has an axis. */
static bool aimed(const render_scene3d_light_shadow *shadow, int light)
{
    CF_V3 axis = cone_axis(shadow->cones[light]);
    return cf_dot_v3(axis, axis) > 0;
}

int render_scene3d_light_shadow_faces(const render_scene3d_light_shadow *shadow, int light)
{
    assert(light < shadow->count);
    return aimed(shadow, light) ? 1 : RENDER_SCENE3D_LIGHT_SHADOW_FACES;
}

/* How much wider than the angle it holds a face looks, so that the guard
 * texels lie past that angle's edge. */
static float guard_share()
{
    return (float)FACE / (FACE - 2 * GUARD);
}

static face_view view_of(const render_scene3d_light_shadow *shadow, int light, int face)
{
    if (!aimed(shadow, light)) {
        CF_V3 up = face >> 1 == 1 ? (CF_V3){ 0, 0, 1 } : (CF_V3){ 0, 1, 0 };
        return (face_view){ forwards[face], up, guard_share() };
    }
    /* The axis carries the cone's fade width, and the offset its outer edge's
     * cosine; scene3d.frag reads them back the same way. */
    CF_V4 cone = shadow->cones[light];
    float scale = cf_len_v3(cone_axis(cone));
    CF_V3 forward = cf_div_v3_f(cone_axis(cone), scale);
    float cosine = -cone.w / scale;
    CF_V3 up = fabsf(forward.y) > .99f ? (CF_V3){ 0, 0, 1 } : (CF_V3){ 0, 1, 0 };
    return (face_view){ forward, up, sqrtf(1 - cosine * cosine) / cosine * guard_share() };
}

static CF_V3 light_center(CF_V4 light)
{
    return (CF_V3){ light.x, light.y, light.z };
}

static bool near_box(const render_scene3d_light_shadow *shadow, int light, CF_M4x4 box, bool bounds_only)
{
    assert(light < shadow->count);
    CF_V4 at = shadow->lights[light];
    CF_V3 offset = cf_sub_v3(
        light_center(at),
        (CF_V3){ box.elements[12], box.elements[13], box.elements[14] }
    );
    /* Along each of the box's edges, it spans half the edge either way. */
    bool around = true;
    float diagonal = 0;
    for (int axis = 0; axis < 3; axis++) {
        const float *m = box.elements + 4 * axis;
        CF_V3 edge = { m[0], m[1], m[2] };
        float length = cf_dot_v3(edge, edge);
        around = around && fabsf(cf_dot_v3(offset, edge)) <= .5f * length;
        diagonal += length;
    }
    return (bounds_only || !around) &&
           cf_len_v3(offset) < at.w + .5f * sqrtf(diagonal);
}

bool render_scene3d_light_shadow_near(const render_scene3d_light_shadow *shadow, int light, CF_M4x4 box)
{
    return near_box(shadow, light, box, false);
}

bool render_scene3d_light_shadow_mesh_near(const render_scene3d_light_shadow *shadow, int light, CF_M4x4 box)
{
    return near_box(shadow, light, box, true);
}

bool render_scene3d_light_shadow_reaches(
    const render_scene3d_light_shadow *shadow,
    int light,
    int face,
    CF_M4x4 box
)
{
    assert(light < shadow->count);
    CF_V4 at = shadow->lights[light];
    face_view view = view_of(shadow, light, face);
    CF_V3 side = cf_norm_v3(cf_cross_v3(view.forward, view.up));
    CF_V3 above = cf_cross_v3(side, view.forward);
    CF_V3 lean = cf_mul_v3_f(view.forward, view.spread);
    /* The face's view is a pyramid from the light. Outward normals of its near
     * plane and its four sides, and how far past the light each one lies. */
    CF_V3 normals[] = {
        cf_neg_v3(view.forward),           cf_sub_v3(side, lean),
        cf_sub_v3(cf_neg_v3(side), lean),  cf_sub_v3(above, lean),
        cf_sub_v3(cf_neg_v3(above), lean),
    };
    float offsets[] = { -near_share * at.w, 0, 0, 0, 0 };
    /* Planes with every corner seen so far outside them. */
    unsigned outside = 31;
    for (int corner = 0; corner < 8; corner++) {
        CF_V3 unit = { corner & 1 ? .5f : -.5f,
                       corner & 2 ? .5f : -.5f,
                       corner & 4 ? .5f : -.5f };
        CF_V3 point = cf_sub_v3(cf_m4_transform_point(box, unit), light_center(at));
        for (int i = 0; i < 5; i++) {
            if (cf_dot_v3(point, normals[i]) <= offsets[i]) {
                outside &= ~(1u << i);
            }
        }
    }
    return !outside;
}

bool render_scene3d_light_shadow_begin(
    render_scene3d_light_shadow *shadow,
    int light,
    int face,
    char *error,
    size_t size
)
{
    assert(light < shadow->count && face < render_scene3d_light_shadow_faces(shadow, light));
    if (!render_scene3d_depth_shader(&shadow->shader, error, size)) {
        return false;
    }
    /* Each light takes one row of faces. */
    render_scene3d_depth_map_prepare(
        &shadow->atlas,
        RENDER_SCENE3D_LIGHT_SHADOW_FACES * FACE,
        shadow->count * FACE
    );
    CF_V4 at = shadow->lights[light];
    CF_V3 eye = light_center(at);
    face_view view = view_of(shadow, light, face);
    CF_RenderState state = cf_render_state_3d_defaults();
    /* Open meshes such as the flat triangle cast from either side. */
    state.cull_mode = CF_CULL_MODE_NONE;
    cf_draw_push_layer(LIGHT_SHADOW_LAYER);
    cf_draw_push_viewport((CF_Rect){ face * FACE, light * FACE, FACE, FACE });
    cf_draw3d_push_render_state(state);
    cf_draw3d_push_shader(shadow->shader);
    cf_draw3d_push_projection(
        cf_perspective(2 * atanf(view.spread), 1, near_share * at.w, at.w)
    );
    /* A target a radius away keeps the face's direction exact far from the
     * origin. */
    CF_V3 target = cf_add_v3(eye, cf_mul_v3_f(view.forward, at.w));
    cf_draw3d_push_view(cf_look_at(eye, target, view.up));
    return true;
}

void render_scene3d_light_shadow_end(render_scene3d_light_shadow *shadow, int light, int face)
{
    cf_draw3d_pop_view();
    cf_draw3d_pop_projection();
    cf_draw3d_pop_shader();
    cf_draw3d_pop_render_state();
    cf_draw_pop_viewport();
    cf_draw_pop_layer();
    if (light < shadow->count - 1 ||
        face < render_scene3d_light_shadow_faces(shadow, light) - 1) {
        return;
    }
    cf_render_layers_to(shadow->atlas.canvas, LIGHT_SHADOW_LAYER, LIGHT_SHADOW_LAYER, true);
}

void render_scene3d_light_shadow_bind(render_scene3d_light_shadow *shadow)
{
    render_scene3d_depth_map_prepare(&shadow->atlas, 1, 1);
    /* Casting lights, how much wider a face looks than the angle it holds,
     * its near plane's share of the radius, and its texels per side. */
    CF_V4 settings = { (float)shadow->count, guard_share(), near_share, (float)FACE };
    cf_draw3d_set_uniform("scene_light_shadow", &settings, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_texture("scene_light_shadow_map", shadow->atlas.map);
}

void render_scene3d_light_shadow_unbind()
{
    cf_draw3d_set_texture("scene_light_shadow_map", (CF_Texture){ 0 });
}

render_scene3d_light_shadow_snapshot render_scene3d_light_shadow_read(render_scene3d_light_shadow *shadow)
{
    render_scene3d_depth_map_prepare(&shadow->atlas, 1, 1);
    return (render_scene3d_light_shadow_snapshot){
        .map = shadow->atlas.map,
        .settings = { (float)shadow->count, guard_share(), near_share, (float)FACE },
    };
}
