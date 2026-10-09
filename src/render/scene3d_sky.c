#include "render/scene3d_sky.h"

#include "render/fullscreen.h"
#include "render/shader_program.h"
#include "runtime/images/sky.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const float exposure_limit = 20;
static const float radians_per_degree = 3.14159265f / 180;

struct render_scene3d_sky
{
    CF_Shader shader;
    CF_Mesh triangle;
    CF_Texture cube;
    uint64_t identity; /* The resident sky the cube holds. */
    bool shown;
    float rotation; /* Radians about +y. */
    float scale;
};

render_scene3d_sky *render_scene3d_sky_new()
{
    return xcalloc(1, sizeof(render_scene3d_sky));
}

void render_scene3d_sky_free(render_scene3d_sky *sky)
{
    if (sky->shader.id) {
        cf_destroy_shader(sky->shader);
    }
    if (sky->triangle.id) {
        cf_destroy_mesh(sky->triangle);
    }
    if (sky->cube.id) {
        cf_destroy_texture(sky->cube);
    }
    free(sky);
}

static bool make_shader(render_scene3d_sky *sky, char *error, size_t size)
{
    char *vertex = render_shader_source("scene3d_sky.vert");
    char *fragment = render_shader_source("scene3d_sky.frag");
    if (vertex && fragment) {
        sky->shader = cf_make_shader_from_source(vertex, fragment);
    }
    if (!sky->shader.id) {
        snprintf(error, size, "Sky shader: %s", vertex && fragment ? cf_shader_compile_error() : "cannot read sky shader sources");
    }
    free(vertex);
    free(fragment);
    return sky->shader.id;
}

static void make_cube(render_scene3d_sky *sky, const render_sky *source)
{
    int edge = render_sky_face_size(source);
    if (sky->cube.id) {
        cf_destroy_texture(sky->cube);
    }
    CF_TextureParams params = cf_texture_defaults(edge, edge);
    params.texture_type = CF_TEXTURE_TYPE_CUBE;
    params.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT;
    params.filter = CF_FILTER_LINEAR;
    params.wrap_u = params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    sky->cube = cf_make_texture(params);
    size_t bytes = (size_t)edge * edge * 4 * sizeof(uint16_t);
    uint16_t *texels = xmalloc(bytes);
    for (int face = 0; face < 6; face++) {
        render_sky_face(source, face, edge, texels);
        cf_texture_update_layer(sky->cube, texels, (int)bytes, face);
    }
    free(texels);
    sky->identity = source->identity;
}

bool render_scene3d_sky_prepare(
    render_scene3d_sky *sky,
    const GameEnvironment *environment,
    char *error,
    size_t size
)
{
    sky->shown = false;
    const char *key = environment->sky;
    if (!key || !key[0]) {
        return true;
    }
    if (!isfinite(environment->sky_rotation)) {
        snprintf(error, size, "Environment sky rotation must be finite.");
        return false;
    }
    if (!(fabsf(environment->sky_exposure) <= exposure_limit)) {
        snprintf(error, size, "Environment sky exposure must be %g to %g stops.", -exposure_limit, exposure_limit);
        return false;
    }
    const render_sky *source = render_sky_find(key);
    if (!source) {
        snprintf(error, size, "Environment sky %s has not been prepared.", key);
        return false;
    }
    if (!sky->shader.id && !make_shader(sky, error, size)) {
        return false;
    }
    if (!sky->triangle.id) {
        sky->triangle = render_make_pass_mesh();
    }
    if (source->identity != sky->identity) {
        make_cube(sky, source);
    }
    sky->rotation = environment->sky_rotation * radians_per_degree;
    sky->scale = exp2f(environment->sky_exposure);
    sky->shown = true;
    return true;
}

void render_scene3d_sky_draw(const render_scene3d_sky *sky, const render_scene3d_camera *camera)
{
    if (!sky->shown) {
        return;
    }
    /* Clip space back to sky directions: the camera turned to the origin, then
     * the sky turned the other way so it appears turned by its rotation. */
    CF_M4x4 view = camera->view;
    view.elements[12] = view.elements[13] = view.elements[14] = 0;
    CF_M4x4 rays = cf_mul_m4(
        cf_m4_rotate_y(-sky->rotation),
        cf_m4_invert(cf_mul_m4(camera->projection, view))
    );
    CF_V4 scale = { .x = sky->scale };
    /* It lies on the far plane, so it fills only pixels no surface covers. It
     * keeps depth writes on so Cute orders it with the opaque draws. */
    CF_RenderState state = cf_render_state_3d_defaults();
    state.depth_compare = CF_COMPARE_FUNCTION_LESS_THAN_OR_EQUAL;
    state.cull_mode = CF_CULL_MODE_NONE;
    cf_draw3d_push_render_state(state);
    cf_draw3d_push_shader(sky->shader);
    cf_draw3d_set_uniform("sky_rays", &rays, CF_UNIFORM_TYPE_MAT4, 1);
    cf_draw3d_set_uniform("sky_scale", &scale, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_texture("sky_cube", sky->cube);
    cf_draw3d_mesh(sky->triangle);
    cf_draw3d_set_texture("sky_cube", (CF_Texture){ 0 });
    cf_draw3d_pop_shader();
    cf_draw3d_pop_render_state();
}
