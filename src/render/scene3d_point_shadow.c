#include "render/scene3d_point_shadow.h"

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
    /* Texels kept clear around the 90 degrees each face holds, as far as
     * the lit pass's 3x3 lookups reach past their point. */
    GUARD = 3,
    /* Keeps the depth pass apart from anything else queued for drawing,
     * including the sun's. */
    POINT_SHADOW_LAYER = INT_MIN / 2 + 1
};

/* Each face's near plane as a share of its light's radius; solids nearer the
 * light cast nothing. */
static const float near_share = .01f;

/* Each face looks along one axis direction, with up along +y, or along +z
 * for the faces along y. scene3d.frag picks faces the same way. */
static const CF_V3 forwards[RENDER_SCENE3D_POINT_SHADOW_FACES] = {
    { 1, 0, 0 },  { -1, 0, 0 }, { 0, 1, 0 },
    { 0, -1, 0 }, { 0, 0, 1 },  { 0, 0, -1 },
};

struct render_scene3d_point_shadow
{
    render_scene3d_depth_map atlas;
    CF_Shader shader;
    CF_V4 lights[RENDER_SCENE3D_POINT_SHADOW_LIMIT];
    int count;
};

render_scene3d_point_shadow *render_scene3d_point_shadow_new()
{
    return xcalloc(1, sizeof(render_scene3d_point_shadow));
}

void render_scene3d_point_shadow_free(render_scene3d_point_shadow *shadow)
{
    render_scene3d_depth_map_free(&shadow->atlas);
    if (shadow->shader.id) {
        cf_destroy_shader(shadow->shader);
    }
    free(shadow);
}

void render_scene3d_point_shadow_set(render_scene3d_point_shadow *shadow, const CF_V4 *lights, int count)
{
    assert(count <= RENDER_SCENE3D_POINT_SHADOW_LIMIT);
    memcpy(shadow->lights, lights, (size_t)count * sizeof *lights);
    shadow->count = count;
}

/* The tangent of half a face's field of view, a little over 45 degrees so
 * that the guard texels lie past the face's own 90. */
static float spread()
{
    return (float)FACE / (FACE - 2 * GUARD);
}

static CF_V3 face_up(int face)
{
    return face >> 1 == 1 ? (CF_V3){ 0, 0, 1 } : (CF_V3){ 0, 1, 0 };
}

static CF_V3 light_center(CF_V4 light)
{
    return (CF_V3){ light.x, light.y, light.z };
}

bool render_scene3d_point_shadow_near(const render_scene3d_point_shadow *shadow, int light, CF_M4x4 box)
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
    return !around && cf_len_v3(offset) < at.w + .5f * sqrtf(diagonal);
}

bool render_scene3d_point_shadow_reaches(
    const render_scene3d_point_shadow *shadow,
    int light,
    int face,
    CF_M4x4 box
)
{
    assert(light < shadow->count);
    CF_V4 at = shadow->lights[light];
    CF_V3 forward = forwards[face];
    CF_V3 side = cf_cross_v3(forward, face_up(face));
    CF_V3 above = cf_cross_v3(side, forward);
    CF_V3 lean = cf_mul_v3_f(forward, spread());
    /* The face's view is a pyramid from the light. Outward normals of its near
     * plane and its four sides, and how far past the light each one lies. */
    CF_V3 normals[] = {
        cf_neg_v3(forward),
        cf_sub_v3(side, lean),
        cf_sub_v3(cf_neg_v3(side), lean),
        cf_sub_v3(above, lean),
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

bool render_scene3d_point_shadow_begin(
    render_scene3d_point_shadow *shadow,
    int light,
    int face,
    char *error,
    size_t size
)
{
    assert(light < shadow->count && face < RENDER_SCENE3D_POINT_SHADOW_FACES);
    if (!render_scene3d_depth_shader(&shadow->shader, error, size)) {
        return false;
    }
    /* Each light takes one row of six faces. */
    render_scene3d_depth_map_prepare(
        &shadow->atlas,
        RENDER_SCENE3D_POINT_SHADOW_FACES * FACE,
        shadow->count * FACE
    );
    CF_V4 at = shadow->lights[light];
    CF_V3 eye = light_center(at);
    CF_RenderState state = cf_render_state_3d_defaults();
    /* Open meshes such as the flat triangle cast from either side. */
    state.cull_mode = CF_CULL_MODE_NONE;
    cf_draw_push_layer(POINT_SHADOW_LAYER);
    cf_draw_push_viewport((CF_Rect){ face * FACE, light * FACE, FACE, FACE });
    cf_draw3d_push_render_state(state);
    cf_draw3d_push_shader(shadow->shader);
    cf_draw3d_push_projection(
        cf_perspective(2 * atanf(spread()), 1, near_share * at.w, at.w)
    );
    cf_draw3d_push_view(cf_look_at(eye, cf_add_v3(eye, forwards[face]), face_up(face)));
    return true;
}

void render_scene3d_point_shadow_end(render_scene3d_point_shadow *shadow, int light, int face)
{
    cf_draw3d_pop_view();
    cf_draw3d_pop_projection();
    cf_draw3d_pop_shader();
    cf_draw3d_pop_render_state();
    cf_draw_pop_viewport();
    cf_draw_pop_layer();
    if (light < shadow->count - 1 || face < RENDER_SCENE3D_POINT_SHADOW_FACES - 1) {
        return;
    }
    cf_render_layers_to(shadow->atlas.canvas, POINT_SHADOW_LAYER, POINT_SHADOW_LAYER, true);
}

void render_scene3d_point_shadow_bind(render_scene3d_point_shadow *shadow)
{
    render_scene3d_depth_map_prepare(&shadow->atlas, 1, 1);
    /* Casting lights, a face's spread, and its near plane's share of the radius. */
    CF_V4 settings = { (float)shadow->count, spread(), near_share, 0 };
    cf_draw3d_set_uniform("scene_light_shadow", &settings, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_texture("scene_light_shadow_map", shadow->atlas.map);
}

void render_scene3d_point_shadow_unbind()
{
    cf_draw3d_set_texture("scene_light_shadow_map", (CF_Texture){ 0 });
}
