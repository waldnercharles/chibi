#include "render/scene3d_volume.h"

#include "render/draw_textures.h"
#include "render/fullscreen.h"
#include "render/gpu_timing.h"
#include "render/shader_program.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    VOLUME_LAYER = INT_MIN / 2 + 3,
    LIGHT_LIMIT = 32
};

struct render_scene3d_volume
{
    render_scene3d_depth_map depth;
    CF_Canvas field;
    CF_Shader depth_shader, integrate, composite;
    CF_Material material;
    CF_Mesh triangle;
    CF_Sampler point;
    Environment settings;
    int width, height, field_width, field_height;
    bool active;
};

render_scene3d_volume *render_scene3d_volume_new()
{
    return xcalloc(1, sizeof(render_scene3d_volume));
}

static void free_targets(render_scene3d_volume *volume)
{
    if (!volume->width) {
        return;
    }
    render_scene3d_depth_map_free(&volume->depth);
    volume->depth = (render_scene3d_depth_map){ 0 };
    cf_destroy_canvas(volume->field);
    volume->width = volume->height = 0;
}

void render_scene3d_volume_free(render_scene3d_volume *volume)
{
    free_targets(volume);
    if (volume->depth_shader.id) {
        cf_destroy_shader(volume->depth_shader);
    }
    if (volume->integrate.id) {
        cf_destroy_shader(volume->integrate);
        cf_destroy_shader(volume->composite);
        cf_destroy_material(volume->material);
        cf_destroy_mesh(volume->triangle);
        cf_destroy_sampler(volume->point);
    }
    free(volume);
}

bool render_scene3d_volume_valid(const Environment *e, char *error, size_t size)
{
    const char *problem = NULL;
    if (!isfinite(e->fog_density) || e->fog_density < 0 || e->fog_density > 1) {
        problem = "Environment fog density must be finite and in [0,1].";
    } else if (e->fog_density == 0) {
        return true;
    } else if (!isfinite(e->fog_start) || e->fog_start < 0 || !isfinite(e->fog_distance) || e->fog_distance <= e->fog_start) {
        problem = "Environment fog distance must be finite and greater than "
                  "its nonnegative start.";
    } else if (!isfinite(e->fog_anisotropy) || fabsf(e->fog_anisotropy) > .9f) {
        problem = "Environment fog anisotropy must be finite and in "
                  "[-0.9,0.9].";
    } else if (e->fog_steps < 4 || e->fog_steps > 64 || !isfinite(e->fog_resolution) || e->fog_resolution < .25f || e->fog_resolution > 1) {
        problem = "Environment fog needs 4..64 steps and resolution in "
                  "[0.25,1].";
    }
    if (problem) {
        snprintf(error, size, "%s", problem);
        return false;
    }
    return true;
}

bool render_scene3d_volume_prepare(
    render_scene3d_volume *volume,
    const Environment *environment,
    int width,
    int height,
    char *error,
    size_t size
)
{
    volume->active = false;
    if (!render_scene3d_volume_valid(environment, error, size)) {
        return false;
    }
    if (environment->fog_density == 0) {
        return true;
    }
    assert(width > 0 && height > 0);
    int fw = (int)ceilf(width * environment->fog_resolution);
    int fh = (int)ceilf(height * environment->fog_resolution);
    if (width != volume->width || height != volume->height ||
        fw != volume->field_width || fh != volume->field_height) {
        free_targets(volume);
        render_scene3d_depth_map_prepare(&volume->depth, width, height);
        CF_CanvasParams p = cf_canvas_defaults(fw, fh);
        p.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
        p.target.filter = CF_FILTER_NEAREST;
        p.target.wrap_u = p.target.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
        volume->field = cf_make_canvas(p);
        volume->width = width;
        volume->height = height;
        volume->field_width = fw;
        volume->field_height = fh;
    }
    volume->settings = *environment;
    volume->active = true;
    return true;
}

bool render_scene3d_volume_active(const render_scene3d_volume *volume)
{
    return volume->active;
}

static bool load_passes(render_scene3d_volume *volume, char *error, size_t size)
{
    if (volume->integrate.id) {
        return true;
    }
    CF_Shader integrate = render_pass_shader("scene3d_volume.frag", error, size);
    if (!integrate.id) {
        return false;
    }
    CF_Shader composite = cf_make_draw_shader("scene3d_volume_composite.shd");
    if (!composite.id) {
        cf_destroy_shader(integrate);
        snprintf(error, size, "3D volume composite could not compile.");
        return false;
    }
    volume->integrate = integrate;
    volume->composite = composite;
    volume->material = cf_make_material();
    volume->triangle = render_make_pass_mesh();
    CF_SamplerParams p = cf_sampler_defaults();
    p.filter = CF_FILTER_NEAREST;
    p.mip_filter = CF_MIP_FILTER_NEAREST;
    p.wrap_u = p.wrap_v = p.wrap_w = CF_WRAP_MODE_CLAMP_TO_EDGE;
    volume->point = cf_make_sampler(p);
    return true;
}

bool render_scene3d_volume_begin_depth(
    render_scene3d_volume *volume,
    const render_scene3d_camera *camera,
    char *error,
    size_t size
)
{
    assert(volume->active);
    if (!render_scene3d_depth_shader(&volume->depth_shader, error, size) ||
        !load_passes(volume, error, size)) {
        return false;
    }
    cf_draw_push_layer(VOLUME_LAYER);
    cf_draw_push_viewport((CF_Rect){ 0, 0, volume->width, volume->height });
    cf_draw3d_push_shader(volume->depth_shader);
    cf_draw3d_push_projection(camera->projection);
    cf_draw3d_push_view(camera->view);
    return true;
}

void render_scene3d_volume_end_depth(render_scene3d_volume *volume)
{
    cf_draw3d_pop_view();
    cf_draw3d_pop_projection();
    cf_draw3d_pop_shader();
    cf_draw_pop_viewport();
    cf_draw_pop_layer();
    cf_render_layers_to(volume->depth.canvas, VOLUME_LAYER, VOLUME_LAYER, true);
}

static CF_V4 albedo(unsigned color)
{
    return (CF_V4){ ((color >> 24) & 255) / 255.0f,
                    ((color >> 16) & 255) / 255.0f,
                    ((color >> 8) & 255) / 255.0f,
                    0 };
}

bool render_scene3d_volume_draw(
    render_scene3d_volume *volume,
    const render_scene3d_camera *camera,
    const render_scene3d_volume_lights *lights,
    char *error,
    size_t size
)
{
    if (!volume->active) {
        return true;
    }
    assert(lights->count >= 0 && lights->count <= LIGHT_LIMIT);
    if (!load_passes(volume, error, size)) {
        return false;
    }
    render_scene3d_volume_lights current = *lights;
    CF_M4x4 rays = render_scene3d_eye_rays(camera);
    CF_V4 eye = { camera->eye.x, camera->eye.y, camera->eye.z, 0 };
    CF_V4 frame = { (float)volume->width,
                    (float)volume->height,
                    (float)volume->field_width,
                    (float)volume->field_height };
    CF_V4 medium = { volume->settings.fog_density,
                     volume->settings.fog_start,
                     volume->settings.fog_distance,
                     volume->settings.fog_anisotropy };
    CF_V4 tint = albedo(volume->settings.fog_color);
    CF_V4 counts = { (float)volume->settings.fog_steps, (float)lights->count, 0, 0 };
    CF_V4 positions[LIGHT_LIMIT] = { 0 }, colors[LIGHT_LIMIT] = { 0 },
          cones[LIGHT_LIMIT] = { 0 };
    for (int i = 0; i < lights->count; i++) {
        positions[i] = lights->positions[i];
        colors[i] = lights->colors[i];
        cones[i] = lights->cones[i];
    }
    CF_Material m = volume->material;
    cf_material_set_uniform_fs(m, "volume_rays", &rays, CF_UNIFORM_TYPE_MAT4, 1);
    cf_material_set_uniform_fs(m, "volume_eye", &eye, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(m, "volume_frame", &frame, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(m, "volume_medium", &medium, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(m, "volume_albedo", &tint, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(m, "volume_counts", &counts, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(m, "volume_sun_direction", &current.sun_direction, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(m, "volume_sun_color", &current.sun_color, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(m, "volume_light_positions", positions, CF_UNIFORM_TYPE_FLOAT4, LIGHT_LIMIT);
    cf_material_set_uniform_fs(m, "volume_light_colors", colors, CF_UNIFORM_TYPE_FLOAT4, LIGHT_LIMIT);
    cf_material_set_uniform_fs(m, "volume_light_cones", cones, CF_UNIFORM_TYPE_FLOAT4, LIGHT_LIMIT);
    cf_material_set_uniform_fs(m, "volume_sun_matrices", current.sun_shadow.matrices, CF_UNIFORM_TYPE_MAT4, 4);
    cf_material_set_uniform_fs(m, "volume_sun_shadow", &current.sun_shadow.settings, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(m, "volume_local_shadow", &current.local_shadow.settings, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_texture_fs_sampler(
        m,
        "volume_depth",
        volume->depth.map,
        volume->point
    );
    cf_material_set_texture_fs(m, "volume_sun_map", lights->sun_shadow.map);
    cf_material_set_texture_fs(m, "volume_local_map", lights->local_shadow.map);
    cf_apply_canvas(volume->field, true);
    cf_apply_mesh(volume->triangle);
    cf_apply_shader(volume->integrate, m);
    cf_apply_viewport(0, 0, volume->field_width, volume->field_height);
    cf_apply_scissor(0, 0, volume->field_width, volume->field_height);
    cf_draw_elements();
    render_gpu_timing_mark("3D volumetric fog");
    CF_RenderState state = cf_render_state_defaults();
    state.blend.enabled = true;
    state.blend.rgb_src_blend_factor = CF_BLENDFACTOR_ONE;
    state.blend.rgb_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    state.blend.alpha_src_blend_factor = CF_BLENDFACTOR_ONE;
    state.blend.alpha_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    cf_draw_push_render_state(state);
    cf_draw_push_shader(volume->composite);
    cf_draw_set_texture("volume_field", cf_canvas_get_target(volume->field));
    cf_draw_set_texture("volume_depth", volume->depth.map);
    cf_draw_set_uniform_float("frame_width", (float)volume->width);
    cf_draw_set_uniform_float("frame_height", (float)volume->height);
    render_draw_fullscreen(volume->field);
    RENDER_DRAW_UNBIND_TEXTURES("volume_field", "volume_depth");
    cf_draw_pop_shader();
    cf_draw_pop_render_state();
    return true;
}
