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

bool authoring_render_arena_preview(
    render_arena *arena,
    const behavior_asset *asset,
    const render_arena_camera *camera,
    const char *parameters_json,
    float time,
    const render_arena_activity *activity
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
        render_arena_set_error(arena, error.message);
        return false;
    }
    bool drawn = render_arena_preview_material(
        arena,
        render_shader_find(asset->name),
        camera,
        parameters_json,
        time,
        activity
    );
    if (!registered) {
        render_arena_set_error(arena, error.message);
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
