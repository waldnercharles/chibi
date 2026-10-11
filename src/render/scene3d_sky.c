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

enum
{
    /* The prefiltered cube's edge, and its mips from roughness 0 to 1. */
    SPECULAR_SIZE = 128,
    SPECULAR_MIPS = 6,
    SPLIT_SUM_SIZE = 32
};

struct render_scene3d_sky
{
    CF_Shader shader, filter;
    /* The sky draws one triangle through draw3d, which adds instance lanes to
     * it, so the filter passes draw another. */
    CF_Mesh triangle, pass;
    CF_Material filter_material;
    CF_Texture cube;     /* The sky at full detail, with mips. */
    CF_Texture specular; /* The sky prefiltered for one roughness per mip. */
    CF_Canvas faces[SPECULAR_MIPS][6];
    CF_Texture split_sum;
    CF_V4 irradiance[9];
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
    if (sky->filter.id) {
        cf_destroy_shader(sky->filter);
    }
    if (sky->cube.id) {
        cf_destroy_texture(sky->cube);
    }
    if (sky->split_sum.id) {
        cf_destroy_mesh(sky->triangle);
        cf_destroy_mesh(sky->pass);
        cf_destroy_material(sky->filter_material);
        for (int mip = 0; mip < SPECULAR_MIPS; mip++) {
            for (int face = 0; face < 6; face++) {
                cf_destroy_canvas(sky->faces[mip][face]);
            }
        }
        cf_destroy_texture(sky->specular);
        cf_destroy_texture(sky->split_sum);
    }
    free(sky);
}

static CF_Shader make_shader(const char *vertex_name, const char *fragment_name, char *error, size_t size)
{
    char *vertex = render_shader_source(vertex_name);
    char *fragment = render_shader_source(fragment_name);
    CF_Shader shader = { 0 };
    if (vertex && fragment) {
        shader = cf_make_shader_from_source(vertex, fragment);
    }
    if (!shader.id) {
        snprintf(
            error,
            size,
            "Sky shader %s: %s",
            fragment_name,
            vertex && fragment ? cf_shader_compile_error() : "cannot read its sources"
        );
    }
    free(vertex);
    free(fragment);
    return shader;
}

static bool make_shaders(render_scene3d_sky *sky, char *error, size_t size)
{
    if (!sky->shader.id) {
        sky->shader = make_shader("scene3d_sky.vert", "scene3d_sky.frag", error, size);
    }
    if (sky->shader.id && !sky->filter.id) {
        sky->filter = make_shader("hrc/pass.vert", "scene3d_sky_filter.frag", error, size);
    }
    return sky->shader.id && sky->filter.id;
}

/* What the scene's shaders bind for sky light whether a sky shows or not:
 * the split-sum table and the prefiltered cube with a canvas per face and
 * mip to render it. */
static void make_light(render_scene3d_sky *sky)
{
    size_t bytes = sizeof(uint16_t) * 2 * SPLIT_SUM_SIZE * SPLIT_SUM_SIZE;
    uint16_t *texels = xmalloc(bytes);
    render_sky_split_sum(SPLIT_SUM_SIZE, texels);
    CF_TextureParams params = cf_texture_defaults(SPLIT_SUM_SIZE, SPLIT_SUM_SIZE);
    params.pixel_format = CF_PIXEL_FORMAT_R16G16_FLOAT;
    params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT;
    params.filter = CF_FILTER_LINEAR;
    params.wrap_u = params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    sky->split_sum = cf_make_texture(params);
    cf_texture_update(sky->split_sum, texels, (int)bytes);
    free(texels);
    params = cf_texture_defaults(SPECULAR_SIZE, SPECULAR_SIZE);
    params.texture_type = CF_TEXTURE_TYPE_CUBE;
    params.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT | CF_TEXTURE_USAGE_COLOR_TARGET_BIT;
    params.filter = CF_FILTER_LINEAR;
    params.mip_filter = CF_MIP_FILTER_LINEAR;
    params.allocate_mipmaps = true;
    params.mip_count = SPECULAR_MIPS;
    params.wrap_u = params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    sky->specular = cf_make_texture(params);
    for (int mip = 0; mip < SPECULAR_MIPS; mip++) {
        for (int face = 0; face < 6; face++) {
            CF_CanvasParams canvas = cf_canvas_defaults(SPECULAR_SIZE >> mip, SPECULAR_SIZE >> mip);
            canvas.attach_target = sky->specular;
            canvas.attach_layer = face;
            canvas.attach_mip = mip;
            sky->faces[mip][face] = cf_make_canvas(canvas);
        }
    }
    sky->triangle = render_make_pass_mesh();
    sky->pass = render_make_pass_mesh();
    sky->filter_material = cf_make_material();
}

/* Renders each specular mip from the sky's cube, with roughness rising
 * evenly from 0 at the first mip to 1 at the last. */
static void prefilter(render_scene3d_sky *sky, int edge)
{
    cf_material_set_texture_fs(sky->filter_material, "sky_cube", sky->cube);
    for (int mip = 0; mip < SPECULAR_MIPS; mip++) {
        int size = SPECULAR_SIZE >> mip;
        for (int face = 0; face < 6; face++) {
            /* Roughness, the face's edge and the sky's in texels, and the face. */
            CF_V4 settings = { (float)mip / (SPECULAR_MIPS - 1),
                               (float)size,
                               (float)edge,
                               (float)face };
            cf_material_set_uniform_fs(sky->filter_material, "filter_settings", &settings, CF_UNIFORM_TYPE_FLOAT4, 1);
            cf_apply_canvas(sky->faces[mip][face], true);
            cf_apply_mesh(sky->pass);
            cf_apply_shader(sky->filter, sky->filter_material);
            cf_apply_viewport(0, 0, size, size);
            cf_apply_scissor(0, 0, size, size);
            cf_draw_elements();
        }
    }
    cf_material_set_texture_fs(sky->filter_material, "sky_cube", (CF_Texture){ 0 });
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
    /* GPU blits fill the mips, rendering into them. */
    params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT | CF_TEXTURE_USAGE_COLOR_TARGET_BIT;
    params.filter = CF_FILTER_LINEAR;
    params.mip_filter = CF_MIP_FILTER_LINEAR;
    params.allocate_mipmaps = true;
    params.wrap_u = params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    sky->cube = cf_make_texture(params);
    size_t bytes = (size_t)edge * edge * 4 * sizeof(uint16_t);
    uint16_t *texels = xmalloc(bytes);
    for (int face = 0; face < 6; face++) {
        render_sky_face(source, face, edge, texels);
        cf_texture_update_layer(sky->cube, texels, (int)bytes, face);
    }
    free(texels);
    cf_generate_mipmaps(sky->cube);
    float coefficients[9][3];
    render_sky_irradiance(source, coefficients);
    for (int i = 0; i < 9; i++) {
        sky->irradiance[i] = (CF_V4){ coefficients[i][0],
                                      coefficients[i][1],
                                      coefficients[i][2],
                                      0 };
    }
    prefilter(sky, edge);
    sky->identity = source->identity;
}

bool render_scene3d_sky_prepare(
    render_scene3d_sky *sky,
    const Environment *environment,
    char *error,
    size_t size
)
{
    sky->shown = false;
    if (!sky->split_sum.id) {
        make_light(sky);
    }
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
    if (!make_shaders(sky, error, size)) {
        return false;
    }
    if (source->identity != sky->identity) {
        make_cube(sky, source);
    }
    sky->rotation = environment->sky_rotation * radians_per_degree;
    sky->scale = exp2f(environment->sky_exposure);
    sky->shown = true;
    return true;
}

CF_V4 render_scene3d_sky_light(const render_scene3d_sky *sky)
{
    return (CF_V4){ sky->shown ? sky->scale : 0,
                    SPECULAR_MIPS - 1,
                    cosf(sky->rotation),
                    sinf(sky->rotation) };
}

CF_V4 *render_scene3d_sky_irradiance(render_scene3d_sky *sky)
{
    return sky->irradiance;
}

void render_scene3d_sky_bind(render_scene3d_sky *sky)
{
    CF_V4 light = render_scene3d_sky_light(sky);
    cf_draw3d_set_uniform("scene_sky_light", &light, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_sky_irradiance", sky->irradiance, CF_UNIFORM_TYPE_FLOAT4, 9);
    cf_draw3d_set_texture("scene_sky_specular", sky->specular);
    cf_draw3d_set_texture("scene_sky_brdf", sky->split_sum);
}

void render_scene3d_sky_unbind()
{
    cf_draw3d_set_texture("scene_sky_specular", (CF_Texture){ 0 });
    cf_draw3d_set_texture("scene_sky_brdf", (CF_Texture){ 0 });
}

void render_scene3d_sky_draw(const render_scene3d_sky *sky, const render_scene3d_camera *camera)
{
    if (!sky->shown) {
        return;
    }
    /* Clip space back to sky directions: the camera turned to the origin, then
     * the sky turned the other way so it appears turned by its rotation. */
    CF_M4x4 rays = cf_mul_m4(cf_m4_rotate_y(-sky->rotation), render_scene3d_eye_rays(camera));
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
