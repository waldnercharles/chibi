#ifndef CHIBI_AUTHORING_RENDER_PREVIEW_H
#define CHIBI_AUTHORING_RENDER_PREVIEW_H
#include "graph/model/behavior_asset.h"
#include "render/shader_preview.h"
bool authoring_render_scene3d_preview(
    render_scene3d *,
    const behavior_asset *,
    const render_scene3d_camera *,
    const char *,
    float,
    const render_scene3d_activity *
);
render_shader_preview *authoring_shader_preview_new(
    const behavior_asset *,
    int,
    int,
    const render_shader_preview_options *,
    shader_error *
);
#endif
