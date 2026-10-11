#ifndef CHIBI_RENDER_SHADER_TEXT_H
#define CHIBI_RENDER_SHADER_TEXT_H

#include "runtime/shaders/source.h"

/* CPU-only compilation of the actual instanced-particle program to all backend
 * languages. No graphics device, display, or authored filesystem is used. */
bool render_shader_text_validate(
    const shader_text *,
    char *const names[SHADER_PARAMETER_MAX],
    const int widths[SHADER_PARAMETER_MAX],
    shader_error *
);

#endif
