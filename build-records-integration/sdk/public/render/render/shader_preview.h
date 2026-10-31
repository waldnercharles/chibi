#ifndef CHIBI_RENDER_SHADER_PREVIEW_H
#define CHIBI_RENDER_SHADER_PREVIEW_H

#include "render/scene3d.h"
#include "render/shader_catalog.h"

#include <stdbool.h>

typedef struct render_shader_preview render_shader_preview;

typedef struct
{
    bool has_camera;
    float center_x, center_y;
    float span_width, span_height;
    render_scene3d_activity activity; /* Materials only; shapes read no scene. */
} render_shader_preview_options;

/* GPU preview of a registered immutable shader. Materials use the production 3D surface and
 * post-processing path; shapes use the production fragment renderer. */
render_shader_preview *render_shader_preview_new(
    const char *name,
    int width,
    int height,
    const render_shader_preview_options *options,
    shader_error *error
);
/* The catalog's shader `name`, as it is registered, without registering
 * anything: the editor's asset thumbnails draw what the scene draws. */
render_shader_preview *render_shader_preview_find(const char *name, int width, int height, shader_error *error);
bool render_shader_preview_draw(render_shader_preview *preview, float time, const char *parameters_json);
const char *render_shader_preview_error(const render_shader_preview *preview);
void render_shader_preview_free(render_shader_preview *preview);

#endif
