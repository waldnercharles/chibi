#ifndef CHIBI_SHADER_TEXT_CREATE_H
#define CHIBI_SHADER_TEXT_CREATE_H
#include "runtime/shaders/source.h"

#include <cute_graphics.h>
/* Vertex already contains the source adapter. Bundled includes are the only
 * authored source available to compilation. Requires a graphics device. */
CF_Shader render_shader_text_create(
    const shader_text *,
    const char *vertex,
    const char *fragment,
    shader_error *
);
#endif
