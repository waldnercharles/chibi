#ifndef CHIBI_RENDER_SHADER_PREVIEW_H
#define CHIBI_RENDER_SHADER_PREVIEW_H

#include "render/scene3d.h"
#include "render/shader_catalog.h"

#include <stdbool.h>

typedef struct render_shader_preview render_shader_preview;
#define RENDER_SHADER_PREVIEW_YAW_MAX 3.14159265358979323846f
#define RENDER_SHADER_PREVIEW_PITCH_MAX 1.57079632679489661923f
#define RENDER_SHADER_PREVIEW_ZOOM_MIN .25f
#define RENDER_SHADER_PREVIEW_ZOOM_MAX 4.0f

typedef struct
{
    bool has_camera;
    float center_x, center_y;
    float span_width, span_height;
    float yaw, pitch; /* Material orbit offsets in radians; shapes require zero. */
    float zoom; /* Zero in options selects 1; explicit view zoom is [.25,4]. */
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
/* Absolute offsets from the original framing; invalid settings preserve view.
 * Shapes support zoom only. No recompilation, publication or GPU allocation. */
bool render_shader_preview_set_view(render_shader_preview *, float yaw, float pitch, float zoom);
/* Pure camera planning for a material preview, useful for repeatable evidence.
 * Invalid offsets/zoom preserve output. Framing must have positive finite spans. */
bool render_shader_preview_view_camera(
    const engine_viewport_camera *framing,
    float yaw,
    float pitch,
    float zoom,
    render_scene3d_camera *output
);
const char *render_shader_preview_error(const render_shader_preview *preview);
void render_shader_preview_free(render_shader_preview *preview);

#endif
