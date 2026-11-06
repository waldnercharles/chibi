#include "render/shader_preview.h"

#include "ecs/ecs.h"
#include "render/material_raster.h"
#include "render/scene3d.h"
#include "render/scene3d_post.h"
#include "render/shader_fragment.h"
#include "render/shader_program.h"
#include "render/shader_values.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct render_shader_preview
{
    char *name; /* the shader in the catalog */
    render_scene3d *scene3d;
    render_scene3d_post *post;
    render_scene3d_camera camera;
    render_programs *programs;
    render_material_entity entity;
    render_scene3d_activity activity;
    float center_x, center_y;
    float span_width, span_height;
    int width, height;
    int program;
    bool material;
    char error[512];
};

static void set_error(shader_error *error, const char *message)
{
    if (!error) {
        return;
    }
    error->node = 0;
    snprintf(error->message, sizeof error->message, "%s", message ? message : "");
}

static void set_preview_error(render_shader_preview *preview, const char *message)
{
    snprintf(preview->error, sizeof preview->error, "%s", message ? message : "");
}

static bool prepare_material_preview(render_shader_preview *preview, int width, int height, shader_error *error)
{
    preview->scene3d = render_scene3d_new();
    preview->post = render_scene3d_post_new();
    if (!preview->scene3d || !preview->post) {
        set_error(error, "Could not create the 3D shader preview.");
        return false;
    }
    CF_V3 corners[8];
    for (int i = 0; i < 8; i++) {
        float r = RENDER_SCENE3D_PREVIEW_RADIUS;
        corners[i] = (CF_V3){ i & 1 ? r : -r, i & 2 ? r : -r, i & 4 ? r : -r };
    }
    CF_Aabb bounds = render_scene3d_fit(corners, 8, (float)width / height);
    preview->center_x = (bounds.min.x + bounds.max.x) * .5f;
    preview->center_y = (bounds.min.y + bounds.max.y) * .5f;
    preview->span_width = bounds.max.x - bounds.min.x;
    preview->span_height = bounds.max.y - bounds.min.y;
    preview->camera = render_scene3d_camera_make(
        preview->center_x,
        preview->center_y,
        preview->span_width,
        preview->span_height
    );
    Environment settings = environment_defaults();
    if (!render_scene3d_post_prepare(preview->post, width, height, &settings)) {
        set_error(error, render_scene3d_post_error(preview->post));
        return false;
    }
    return true;
}

static bool prepare_shape_preview(render_shader_preview *preview, shader_error *error)
{
    int id = render_shader_find(preview->name);
    if (!id) {
        set_error(error, "The shader asset was not registered.");
        return false;
    }
    float bounds[2];
    if (!render_shader_bounds(id, bounds) || !isfinite(bounds[0]) ||
        !isfinite(bounds[1])) {
        set_error(error, "Shape bounds are unavailable or nonfinite.");
        return false;
    }
    preview->entity.inv_rot[0] = 1;
    preview->entity.inv_rot[3] = 1;
    preview->entity.aa = .5f;
    preview->entity.scene[0] = 1;
    preview->entity.scene[1] = 1;
    preview->entity.scene[2] = 1;
    preview->entity.scene[3] = 1;
    preview->entity.shape_type = RENDER_SHAPE_BOX;
    preview->entity.points[0] = bounds[0];
    preview->entity.points[1] = bounds[1];
    preview->entity.extent[0] = bounds[0] + preview->entity.aa;
    preview->entity.extent[1] = bounds[1] + preview->entity.aa;
    preview->entity.reserved[1] = (float)id;
    float aspect = (float)preview->width / preview->height;
    preview->span_width = fmaxf(bounds[0] * 2.2f, bounds[1] * 2.2f * aspect);
    preview->span_height = fmaxf(bounds[1] * 2.2f, bounds[0] * 2.2f / aspect);
    render_shader_parameters(id, NULL, (ecs_entity){ 0 }, NULL, preview->entity.shape_parameters);
    preview->programs = render_programs_new(RENDER_PROGRAM_FRAGMENT);
    if (!preview->programs) {
        set_error(error, "Could not create the fragment shader program cache.");
        return false;
    }
    preview
        ->program = render_shader_fragment_prepare(preview->programs, &preview->entity, error);
    return preview->program != 0;
}

static render_shader_preview *preview_new(
    const char *name,
    bool material,
    int width,
    int height,
    const render_shader_preview_options *options,
    shader_error *error
)
{
    render_shader_preview *preview = xcalloc(1, sizeof *preview);
    preview->name = xstrdup(name);
    preview->width = width;
    preview->height = height;
    preview->material = material;
    preview->activity = options->activity;
    bool prepared = preview->material
                      ? prepare_material_preview(preview, width, height, error)
                      : prepare_shape_preview(preview, error);
    if (!prepared) {
        render_shader_preview_free(preview);
        return NULL;
    }
    if (options->has_camera) {
        preview->center_x = options->center_x;
        preview->center_y = options->center_y;
        preview->span_width = options->span_width;
        preview->span_height = options->span_height;
        if (preview->material) {
            preview->camera = render_scene3d_camera_make(
                preview->center_x,
                preview->center_y,
                preview->span_width,
                preview->span_height
            );
        }
    }
    return preview;
}

render_shader_preview *render_shader_preview_new(
    const char *name,
    int width,
    int height,
    const render_shader_preview_options *options,
    shader_error *error
)
{
    if (error) {
        *error = (shader_error){ 0 };
    }
    int id = render_shader_find(name);
    if (!id) {
        set_error(error, "The shader asset was not registered.");
        return NULL;
    }
    return preview_new(name, !render_shader_is_shape(id), width, height, options, error);
}

render_shader_preview *render_shader_preview_find(const char *name, int width, int height, shader_error *error)
{
    if (error) {
        *error = (shader_error){ 0 };
    }
    int id = render_shader_find(name);
    if (!id) {
        set_error(error, "The shader asset was not registered.");
        return NULL;
    }
    render_shader_preview_options options = { 0 };
    return preview_new(name, !render_shader_is_shape(id), width, height, &options, error);
}

bool render_shader_preview_draw(render_shader_preview *preview, float time, const char *parameters_json)
{
    if (!preview) {
        return false;
    }
    preview->error[0] = 0;
    int shader = render_shader_find(preview->name);
    shader_error parameter_error = { 0 };
    float prepared_parameters[32][4];
    if (parameters_json && *parameters_json &&
        !shader_values_plain(shader, parameters_json, prepared_parameters, &parameter_error)) {
        set_preview_error(preview, parameter_error.message);
        return false;
    }
    if (preview->material) {
        if (!render_scene3d_preview_material(
                preview->scene3d,
                shader,
                &preview->camera,
                parameters_json,
                time,
                &preview->activity
            )) {
            set_preview_error(preview, render_scene3d_error(preview->scene3d));
            return false;
        }
        cf_render_to(render_scene3d_post_target(preview->post), true);
        if (!render_scene3d_post_process(preview->post, 0)) {
            set_preview_error(preview, render_scene3d_post_error(preview->post));
            return false;
        }
        CF_Canvas output = render_scene3d_post_output(preview->post);
        if (!output.id) {
            set_preview_error(preview, "3D post-processing produced no output canvas.");
            return false;
        }
        cf_draw_push();
        cf_draw_TSR_absolute((CF_V2){ 0 }, (CF_V2){ 1, 1 }, 0);
        cf_draw_projection(cf_ortho_2d(0, 0, 2, 2));
        cf_draw_canvas(output, (CF_V2){ 0 }, (CF_V2){ 2, 2 });
        cf_draw_pop();
        return true;
    }
    preview->entity.time[0] = time;
    render_shader_parameters(
        shader,
        NULL,
        (ecs_entity){ 0 },
        parameters_json,
        preview->entity.shape_parameters
    );
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(
        preview->center_x,
        preview->center_y,
        preview->span_width,
        preview->span_height
    ));
    render_shader_fragment_draw(preview->programs, preview->program, &preview->entity, (CF_V2){ 1, 1 });
    cf_draw_pop();
    return true;
}

const char *render_shader_preview_error(const render_shader_preview *preview)
{
    return preview ? preview->error : "";
}

void render_shader_preview_free(render_shader_preview *preview)
{
    if (!preview) {
        return;
    }
    render_programs_free(preview->programs);
    render_scene3d_free(preview->scene3d);
    render_scene3d_post_free(preview->post);
    free(preview->name);
    free(preview);
}
