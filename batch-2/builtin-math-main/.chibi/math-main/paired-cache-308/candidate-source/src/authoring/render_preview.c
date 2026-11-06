#include "authoring/render_preview.h"

#include "authoring/shader_prepare.h"

#include <stdio.h>

static void set_error(shader_error *error, const char *message)
{
    if (error) {
        error->node = 0;
        snprintf(error->message, sizeof error->message, "%s", message);
    }
}

bool authoring_render_scene3d_preview(
    render_scene3d *scene3d,
    const behavior_asset *asset,
    const render_scene3d_camera *camera,
    const char *parameters_json,
    float time,
    const render_scene3d_activity *activity
)
{
    shader_error error = { 0 };
    bool registered = render_shader_upsert(asset, &error);
    if (registered) {
        registered = authoring_shader_parameters_prepare(
            render_shader_find(asset->name),
            parameters_json,
            &error
        );
    }
    if (!registered) {
        render_scene3d_set_error(scene3d, error.message);
        return false;
    }
    bool drawn = render_scene3d_preview_material(
        scene3d,
        render_shader_find(asset->name),
        camera,
        parameters_json,
        time,
        activity
    );
    if (!registered) {
        render_scene3d_set_error(scene3d, error.message);
    }
    return drawn;
}

render_shader_preview *authoring_shader_preview_new(
    const behavior_asset *asset,
    int width,
    int height,
    const render_shader_preview_options *options,
    shader_error *error
)
{
    if (error) {
        *error = (shader_error){ 0 };
    }
    if (!asset || (asset->domain != BEHAVIOR_DOMAIN_MATERIAL &&
                   asset->domain != BEHAVIOR_DOMAIN_SHAPE)) {
        set_error(error, "Shader previews require a material or shape asset.");
        return NULL;
    }
    if (!render_shader_upsert(asset, error)) {
        return NULL;
    }
    return render_shader_preview_new(asset->name, width, height, options, error);
}
