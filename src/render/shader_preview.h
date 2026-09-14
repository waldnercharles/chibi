#ifndef CHIBI_RENDER_SHADER_PREVIEW_H
#define CHIBI_RENDER_SHADER_PREVIEW_H

#include "graph/model/behavior_asset.h"
#include "render/shader_catalog.h"

#include <stdbool.h>

typedef struct render_shader_preview render_shader_preview;

/* Tool-owned GPU preview state. Materials use the production arena surface and
 * post-processing path; shapes use the production fragment renderer. */
render_shader_preview *render_shader_preview_new(
    const behavior_asset *asset,
    int width,
    int height,
    shader_error *error
);
bool render_shader_preview_draw(render_shader_preview *preview, float time);
const char *render_shader_preview_error(const render_shader_preview *preview);
void render_shader_preview_free(render_shader_preview *preview);

#endif
