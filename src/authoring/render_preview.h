#ifndef CHIBI_AUTHORING_RENDER_PREVIEW_H
#define CHIBI_AUTHORING_RENDER_PREVIEW_H
#include "graph/model/behavior_asset.h"
#include "render/shader_preview.h"
bool authoring_render_arena_preview(
    render_arena *,
    const behavior_asset *,
    const render_arena_camera *,
    const char *,
    float,
    const render_arena_activity *
);
render_shader_preview *authoring_shader_preview_new(
    const behavior_asset *,
    int,
    int,
    const render_shader_preview_options *,
    shader_error *
);
#endif
